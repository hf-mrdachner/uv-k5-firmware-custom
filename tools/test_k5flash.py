"""Tests for k5flash.py that don't require real hardware.

Two independent checks:
1. crc16_xmodem / packetize cross-checked against crcmod (an independent,
   third-party implementation already used elsewhere in this repo by
   fw-pack.py), not just against k5flash's own code.
2. A simulated bootloader on a real socketpair (not a mock that mirrors
   k5flash's own logic) exercises the full flash() flow: handshake, block
   chunking, address/padding math for a non-block-aligned firmware size,
   and progress across many blocks.
"""

import socket
import sys
import threading
import unittest

import crcmod

sys.path.insert(0, __file__.rsplit("/", 1)[0].rsplit("\\", 1)[0])
import k5flash  # noqa: E402

REFERENCE_CRC = crcmod.predefined.mkCrcFun("xmodem")


class CrcAndFramingTests(unittest.TestCase):
    def test_crc16_xmodem_matches_crcmod(self):
        for data in (b"", b"\x00", b"hello world", bytes(range(256)), b"A" * 1000):
            self.assertEqual(k5flash.crc16_xmodem(data), REFERENCE_CRC(data))

    def test_packetize_framing(self):
        data = bytes([0x30, 0x05, 0x10, 0x00]) + b"*ARDF 89d8c41\x00\x00"
        packet = k5flash.packetize(data)

        self.assertEqual(packet[0:2], b"\xab\xcd")
        self.assertEqual(packet[-2:], b"\xdc\xba")
        length = packet[2] + (packet[3] << 8)
        self.assertEqual(length, len(data))
        # payload on the wire is data+crc (2 bytes longer than the length field)
        self.assertEqual(len(packet), 4 + length + 2 + 2)

    def test_xor_is_its_own_inverse(self):
        data = bytes(range(50))
        self.assertEqual(k5flash.xor(k5flash.xor(data, k5flash.FRAME_XOR_KEY), k5flash.FRAME_XOR_KEY), data)


class FakeSerial:
    """pyserial-compatible facade around one end of a socketpair."""

    def __init__(self, sock):
        self._sock = sock
        self._sock.settimeout(0.05)

    def write(self, data):
        self._sock.sendall(data)

    def read(self, n):
        try:
            return self._sock.recv(max(1, n))
        except socket.timeout:
            return b""

    @property
    def in_waiting(self):
        return 4096

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self._sock.close()


class MockBootloader(threading.Thread):
    """Independent server-side protocol implementation (own framing code,
    not reused from k5flash) simulating a UV-K5 in bootloader mode."""

    def __init__(self, sock, expected_firmware):
        super().__init__(daemon=True)
        self._sock = sock
        self._expected_firmware = expected_firmware
        self.received_blocks = {}
        self.error = None

    def _send(self, data):
        length = bytes([len(data) & 0xFF, (len(data) >> 8) & 0xFF])
        crc = REFERENCE_CRC(data)
        payload = bytes(b ^ k5flash.FRAME_XOR_KEY[i % 16] for i, b in enumerate(data + bytes([crc & 0xFF, crc >> 8])))
        self._sock.sendall(b"\xab\xcd" + length + payload + b"\xdc\xba")

    def _recv_packet(self):
        buf = b""
        self._sock.settimeout(2.0)
        while True:
            buf += self._sock.recv(4096)
            if len(buf) >= 4:
                length = buf[2] + (buf[3] << 8)
                total = length + 8
                if len(buf) >= total:
                    packet = buf[:total]
                    data = bytes(b ^ k5flash.FRAME_XOR_KEY[i % 16] for i, b in enumerate(packet[4:4 + length]))
                    return data

    def run(self):
        try:
            # spam the ready beacon like a real bootloader, client should ignore extras
            self._send(bytes([0x18]) + bytes(20))
            self._send(bytes([0x18]) + bytes(20))

            version_req = self._recv_packet()
            assert version_req[0] == 0x30, version_req[0]
            response = bytearray(30)
            response[0] = 0x18
            response[0x14] = 0x02  # hardware id the client will accept via '*' wildcard
            response[0x14:0x14 + 7] = b"2.00.06"
            self._send(bytes(response))

            total = len(self._expected_firmware)
            while True:
                packet = self._recv_packet()
                if packet[0] != 0x19:
                    continue
                address = (packet[8] << 8) | packet[9]
                block_data = packet[16:16 + 0x100]
                self.received_blocks[address] = block_data
                self._send(bytes([0x1A]))
                if address + 0x100 >= ((total + 0xFF) & ~0xFF):
                    break
        except Exception as e:  # pragma: no cover - surfaced via assertion in test
            self.error = e


class EndToEndFlashTests(unittest.TestCase):
    def test_flash_full_protocol_flow(self):
        # spans past the 0x2000 version-splice point, with a short final
        # block to exercise zero-padding
        firmware_body = bytes((i * 7) % 256 for i in range(0x2000 + 0x80))
        version = b"*TEST unittest\x00\x00"[:16]
        packed = self._build_packed(firmware_body, version)

        client_sock, server_sock = socket.socketpair()
        bootloader = MockBootloader(server_sock, firmware_body)
        bootloader.start()

        import tempfile, os
        fd, path = tempfile.mkstemp(suffix=".packed.bin")
        os.write(fd, packed)
        os.close(fd)
        try:
            with FakeSerial(client_sock) as fake:
                import unittest.mock as mock
                with mock.patch("serial.Serial", return_value=fake):
                    k5flash.flash("ignored", path, baud_timeout=2.0)
        finally:
            os.unlink(path)

        bootloader.join(timeout=2.0)
        server_sock.close()
        self.assertIsNone(bootloader.error)

        reassembled = bytearray(len(firmware_body))
        for addr, block in bootloader.received_blocks.items():
            reassembled[addr:addr + 0x100] = block[:min(0x100, len(firmware_body) - addr)]
        self.assertEqual(bytes(reassembled), firmware_body)

    @staticmethod
    def _build_packed(firmware_body, version):
        decoded = firmware_body[:0x2000] + version + firmware_body[0x2000:]
        encoded = bytes(b ^ k5flash.CONTAINER_XOR_KEY[i % 128] for i, b in enumerate(decoded))
        crc = REFERENCE_CRC(encoded)
        return encoded + bytes([crc & 0xFF, crc >> 8])


if __name__ == "__main__":
    unittest.main()
