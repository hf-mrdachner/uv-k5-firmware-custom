#!/usr/bin/env python3
"""CLI flasher for Quansheng UV-K5 (hardware version 1), no browser required.

Reimplements the serial protocol used by the egzumer uvtools web flasher
(https://egzumer.github.io/uvtools/js/qsSerial.js, tool_patcher.js, fwpack.js)
against a real radio. Takes a *.packed.bin as produced by fw-pack.py.

Usage:
    pip install pyserial
    python tools/k5flash.py --list-ports
    python tools/k5flash.py -p COM10 firmware_uvk5_v1.packed.bin

Radio must be in bootloader mode before flashing: power off, hold PTT while
powering on (white LED must light up), then plug in the programming cable.
"""

import argparse
import sys
import time

import serial
from serial.tools import list_ports

# Two independent XOR obfuscation layers are used by this protocol - don't confuse them:
# - FRAME_XOR_KEY obfuscates each serial packet's payload (qsSerial.js: k5_xor_array)
# - CONTAINER_XOR_KEY obfuscates the whole *.packed.bin container (fw-pack.py: OBFUSCATION)
FRAME_XOR_KEY = bytes([
    0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
    0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80,
])
CONTAINER_XOR_KEY = bytes([
    0x47, 0x22, 0xC0, 0x52, 0x5D, 0x57, 0x48, 0x94, 0xB1, 0x60, 0x60, 0xDB, 0x6F, 0xE3, 0x4C, 0x7C,
    0xD8, 0x4A, 0xD6, 0x8B, 0x30, 0xEC, 0x25, 0xE0, 0x4C, 0xD9, 0x00, 0x7F, 0xBF, 0xE3, 0x54, 0x05,
    0xE9, 0x3A, 0x97, 0x6B, 0xB0, 0x6E, 0x0C, 0xFB, 0xB1, 0x1A, 0xE2, 0xC9, 0xC1, 0x56, 0x47, 0xE9,
    0xBA, 0xF1, 0x42, 0xB6, 0x67, 0x5F, 0x0F, 0x96, 0xF7, 0xC9, 0x3C, 0x84, 0x1B, 0x26, 0xE1, 0x4E,
    0x3B, 0x6F, 0x66, 0xE6, 0xA0, 0x6A, 0xB0, 0xBF, 0xC6, 0xA5, 0x70, 0x3A, 0xBA, 0x18, 0x9E, 0x27,
    0x1A, 0x53, 0x5B, 0x71, 0xB1, 0x94, 0x1E, 0x18, 0xF2, 0xD6, 0x81, 0x02, 0x22, 0xFD, 0x5A, 0x28,
    0x91, 0xDB, 0xBA, 0x5D, 0x64, 0xC6, 0xFE, 0x86, 0x83, 0x9C, 0x50, 0x1C, 0x73, 0x03, 0x11, 0xD6,
    0xAF, 0x30, 0xF4, 0x2C, 0x77, 0xB2, 0x7D, 0xBB, 0x3F, 0x29, 0x28, 0x57, 0x22, 0xD6, 0x92, 0x8B,
])
VERSION_INFO_OFFSET = 0x2000
VERSION_INFO_LENGTH = 16
MAX_FIRMWARE_SIZE = 0xEFFF


def xor(data, key):
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def unpack_firmware(packed):
    body, stored_crc = packed[:-2], packed[-2:]
    if bytes([crc16_xmodem(body) & 0xFF, crc16_xmodem(body) >> 8]) != stored_crc:
        raise ValueError("CRC check failed - not a valid *.packed.bin file")

    decoded = xor(body, CONTAINER_XOR_KEY)
    version = decoded[VERSION_INFO_OFFSET:VERSION_INFO_OFFSET + VERSION_INFO_LENGTH]
    firmware = decoded[:VERSION_INFO_OFFSET] + decoded[VERSION_INFO_OFFSET + VERSION_INFO_LENGTH:]
    return firmware, version


def packetize(data):
    length = bytes([len(data) & 0xFF, (len(data) >> 8) & 0xFF])
    crc = crc16_xmodem(data)
    payload = xor(data + bytes([crc & 0xFF, crc >> 8]), FRAME_XOR_KEY)
    return b"\xab\xcd" + length + payload + b"\xdc\xba"


class Timeout(Exception):
    pass


def read_packet(ser, expected_first_byte, timeout=1.0):
    """Blocks until a packet whose deobfuscated first byte matches is received.

    Mirrors qsSerial.js's readPacket: non-matching packets are silently
    discarded (the radio spams its 0x18 "ready" beacon continuously), and the
    trailing 2 obfuscated bytes of the payload (the sender's CRC) are dropped
    rather than verified - matching the reference implementation exactly,
    since that's what real bootloader firmware has been verified to expect.
    """
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            buffer.extend(chunk)
        while buffer and buffer[0] != 0xAB:
            del buffer[0]
        while len(buffer) >= 4 and buffer[0] == 0xAB and buffer[1] == 0xCD:
            payload_length = buffer[2] + (buffer[3] << 8)
            total_length = payload_length + 8
            if len(buffer) < total_length:
                break
            packet = bytes(buffer[:total_length])
            if packet[payload_length + 6] == 0xDC and packet[payload_length + 7] == 0xBA:
                del buffer[:total_length]
                data = xor(packet[4:4 + payload_length], FRAME_XOR_KEY)
                if data and data[0] == expected_first_byte:
                    return data
            else:
                del buffer[0]
        if not chunk:
            time.sleep(0.01)
    raise Timeout(f"No 0x{expected_first_byte:02x} packet received within {timeout}s")


def flash_init(ser, version):
    packet_data = bytes([0x30, 0x05, len(version), 0x00]) + version
    ser.write(packetize(packet_data))
    return read_packet(ser, 0x18)


def flash_block(ser, data, address, total_size):
    if len(data) < 0x100:
        data = data + bytes(0x100 - len(data))
    address_final = (total_size + 0xFF) & ~0xFF
    if address_final > 0xF000:
        raise ValueError("Firmware too large to flash")
    command = bytes([
        0x19, 0x05, 0x0C, 0x01, 0x8A, 0x8D, 0x9F, 0x1D,
        (address >> 8) & 0xFF, address & 0xFF,
        (address_final >> 8) & 0xFF, 0x00,
        0x01, 0x00, 0x00, 0x00,
    ]) + data
    ser.write(packetize(command))
    read_packet(ser, 0x1A)


def flash(port, firmware_path, baud_timeout=5.0):
    with open(firmware_path, "rb") as f:
        packed = f.read()

    firmware, version = unpack_firmware(packed)
    print(f"Detected firmware version: {version.split(b'\\x00', 1)[0].decode(errors='replace')}")
    print(f"Firmware uses {len(firmware) / MAX_FIRMWARE_SIZE * 100:.2f}% "
          f"of available memory ({len(firmware)}/{MAX_FIRMWARE_SIZE} bytes).")
    if len(firmware) > MAX_FIRMWARE_SIZE:
        raise ValueError("Firmware is too large and will not fit - disable some ENABLE_* mods")

    with serial.Serial(port, baudrate=38400, timeout=0.1) as ser:
        print("Waiting for radio in bootloader mode...")
        read_packet(ser, 0x18, timeout=baud_timeout)
        print("Radio in flash mode detected.")

        response = flash_init(ser, version)
        bootloader_version = response[0x14:0x14 + 7].decode(errors="replace")
        print(f"Bootloader version: {bootloader_version}")
        if version[0] != 0x2A and response[0x14] != version[0]:
            raise ValueError("Version check failed - wrong firmware for this hardware")
        print("Version check passed.")

        print("Flashing firmware...")
        for i in range(0, len(firmware), 0x100):
            flash_block(ser, firmware[i:i + 0x100], i, len(firmware))
            print(f"\rFlashing... {i / len(firmware) * 100:5.1f}%", end="", flush=True)
        print("\rFlashing... 100.0%")
        print("Successfully flashed firmware.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("firmware", nargs="?", help="path to a *.packed.bin firmware file")
    parser.add_argument("-p", "--port", help="serial port, e.g. COM10")
    parser.add_argument("--list-ports", action="store_true", help="list available serial ports and exit")
    parser.add_argument("--timeout", type=float, default=5.0, help="seconds to wait for the radio's ready beacon")
    args = parser.parse_args()

    if args.list_ports:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return

    if not args.port or not args.firmware:
        parser.error("both --port and a firmware file are required (or use --list-ports)")

    try:
        flash(args.port, args.firmware, baud_timeout=args.timeout)
    except Timeout:
        print("\nTimed out waiting for the radio. Is it connected, powered on, "
              "and in bootloader mode (PTT held while powering on, white LED lit)?", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
