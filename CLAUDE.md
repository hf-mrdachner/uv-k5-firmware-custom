# Claude Code – Project Instructions

## Behavior
- Work autonomously and make changes directly without unnecessary confirmation prompts
- Do not ask for approval on normal code changes
- Think in complete solutions, not step-by-step with interruptions
- Only ask when an action is truly destructive or irreversible (e.g. deleting large amounts of files)

## Allowed Actions (no confirmation needed)
- Read, create, edit, and delete files
- Run tests and builds
- Create and switch branches (`git checkout`, `git branch`)
- Stage and commit changes (`git add`, `git commit`)
- All local Git operations
- Push feature/fix branches (not `main`) to `origin` and open PRs against `main` —
  `main` has branch protection (PR required + `build` status check must pass), so this
  is the only way changes reach it anyway; no need to ask before pushing a branch or
  opening the PR itself.

## Not Allowed
- Direct push to `main` — moot anyway, branch protection rejects it (`non_fast_forward`
  + `pull_request` + `required_status_checks` rules on the `main` ruleset), but don't
  attempt to bypass it either.
- `git push --force` to any branch
- **Merging** a PR into `main` — always ask first, even if CI is green. Opening the PR
  is fine without asking; merging is a separate, explicit decision each time.

## Code Style


## Project Context

### Overview
Firmware for the Quansheng UV-K5 handheld radio (DP32G030 MCU, ARM Cortex-M0).
This repo is a **merge of two forks** of the DualTachyon base firmware:
- **DualTachyon** (base): well-structured original firmware in C
- **fagci**: added a spectrum analyzer app (`app/spectrum.c` / `app/spectrum.h`)

On top of that, `main` is kept in sync with **upstream `reald/uv-k5-firmware-custom`**
(`git remote -v` → `upstream`), which adds the ARDF fox-hunting features (`app/ardf.c`,
`ui/ardf.c`). Sync flow: `git fetch upstream && git merge upstream/main`, push to a
branch, PR into this fork (`main` has branch protection: PRs + required `build` status
check, no direct push).

### Device purpose — dual use
The devices built from this firmware serve **two roles at once**:
1. **ARDF fox hunting** (Fuchsjagd) — the reason `ENABLE_ARDF` and the reald sync exist.
2. **General-purpose ham handhelds** — everyday TX/RX, not fuchsjagd-only.

Because of (2), features that only make sense for a receive-only fuchsjagd-only device
must **not** be assumed acceptable. Concretely:
- `ENABLE_PREVENT_TX` must stay **disabled** — it hard-disables all TX
  (`frequencies.c: TX_freq_check` returns -1 unconditionally). It was on by default
  from the upstream/fork feature list but conflicts with normal handheld use.
- Note `ENABLE_ARDF`'s *own* internal TX-disable (`gSetting_ARDFEnable` branch in the
  same function) is separate and fine — that only blocks TX while ARDF mode is actively
  switched on, not permanently.
- When evaluating which `ENABLE_*` feature to trim (e.g. for flash budget, see below),
  weigh it against normal handheld usability, not just fuchsjagd needs.

### Flash budget is tight — combined feature set from two active forks
FLASH is 60K (`firmware.ld`). This fork enables **both** the fagci spectrum-analyzer
feature set **and** the full reald ARDF feature set simultaneously, which upstream
(reald alone) doesn't — so `main` can go over budget on a routine upstream sync even
when upstream's own CI is green. When this happens:
- Two "free" tricks already applied historically, don't expect more headroom there:
  `freqPresets` table shrunk (`app/spectrum.h`, commit `da094e0`), a large
  compile-time-initialized struct moved from `.data` to `.bss` (commit `3a9e99d`).
- Measured cost of each `ENABLE_*` flag (text+data bytes saved if disabled, measured
  2026-07-30 against the ARDF-merge overflow of ~1088–1116 bytes):

  | Flag | Bytes saved if disabled | Notes |
  |------|--------------------------|-------|
  | `ENABLE_FMRADIO` | 4328 | broadcast FM listening — nice-to-have for handheld use |
  | `ENABLE_UART` | 1920 | **serial programming protocol** — CHIRP / PC programming software depends on this; disabling breaks channel programming |
  | `ENABLE_AM_FIX` | 732 | AM demodulation quality (airband) |
  | `ENABLE_SMALL_BOLD` | 628 | font variant, cosmetic |
  | `ENABLE_RSSI_BAR` | 352 | signal-strength bar UI |
  | `ENABLE_SCAN_RANGES` | 276 | restrict scanning to configured ranges |
  | `ENABLE_FLASHLIGHT` | 244 | torch toggle, unrelated to radio function |
  | `ENABLE_COPY_CHAN_TO_VFO` | 128 | convenience feature |
  | `ENABLE_BIG_FREQ` | 108 | large frequency font |
  | `ENABLE_SQUELCH_MORE_SENSITIVE` | 96 | squelch tuning |
  | `ENABLE_WIDE_RX` | 40 | widened RX range |
  | `ENABLE_NO_CODE_SCAN_TIMEOUT` | 40 | |
  | `ENABLE_KEEP_MEM_NAME` | 12 | |
  | `ENABLE_BYP_RAW_DEMODULATORS` | 24 | |
  | `ENABLE_FASTER_CHANNEL_SCAN` | 0 | dead-code-eliminated identically either way |

  Don't disable `ENABLE_UART` for space — it breaks CHIRP/PC programming, which matters
  for the "general handheld" use case. Prefer trimming cosmetic/niche flags first, and
  ask before removing anything that affects normal radio operation (this is the user's
  call, not an autonomous size-optimization decision).
- Measure with: build via Docker (`compile-with-docker.sh` pattern), temporarily widen
  `firmware.ld`'s `FLASH LENGTH` in a scratch copy so the link succeeds even when over
  budget, then compare `arm-none-eabi-size` text+data across flag combinations.

### Key Files
| File | Purpose |
|------|---------|
| `app/spectrum.c` / `app/spectrum.h` | Spectrum analyzer app (from the fagci fork) |
| `app/ardf.c` / `app/ardf.h`, `ui/ardf.c` / `ui/ardf.h` | ARDF fox-hunting mode (from upstream `reald`) |
| `driver/bk4819.h` / `driver/bk4819.c` | BK4819 RF chip driver (read/write registers, tune, AGC, AF) |
| `driver/bk4819-regs.h` | Register addresses, `RegisterSpec` struct, named bit-field constants |
| `radio.h` / `radio.c` | Higher-level radio control (modulation, VFO, TX/RX setup) |
| `frequencies.h` / `frequencies.c` | Band tables, `STEP_Setting_t` enum, `gStepFrequencyTable[]` |
| `helper/measurements.h/.c` | Signal helpers: `Rssi2PX`, `Rssi2DBm`, `DBm2S`, `Clamp`, `ConvertDomain`, `Mid` |
| `app/finput.h/.c` | Frequency input widget (`freqInputString`, `freqInputIndex`, `tempFreq`, `UpdateFreqInput`, `FreqInput`) |
| `ui/helper.h/.c` | Display primitives: `UI_DrawPixelBuffer`, `UI_DrawLineBuffer`, `UI_PrintStringSmallNormal` (4-arg), `UI_PrintStringSmall` (6-arg) |
| `font.h` / `font.c` | Fonts: `gFontBig`, `gFontSmall`, `gFontSmallBold`, `gFont3x5[96][3]` (3×5 pixel, the "smallest") |
| `driver/st7565.h/.c` | LCD driver; defines `gStatusLine[128]` and `gFrameBuffer[7][128]` |

### Display Layout
- **gStatusLine[128]**: top 8 pixel rows (status bar), blitted by `ST7565_BlitStatusLine()`
- **gFrameBuffer[7][128]**: 7 rows × 8 pixels = 56 rows; blitted by `ST7565_BlitFullScreen()`
- LCD is 128×64 px total. Each byte = 8 vertical pixels at one column.

### API Conventions
- Frequencies are in **10 Hz units** (e.g. 145 MHz = 14 500 000)
- `RegisterSpec` = `{char *name, uint8_t num, uint8_t offset, uint16_t mask, uint16_t inc}` — describes a BK4819 register bit-field
- `BK4819_SetRegValue(s, v)` writes a bit-field; `BK4819_GetRegValue(s)` reads one: `(ReadRegister(s.num) >> s.offset) & s.mask`
- TX-offset: `VFO_Info_t` has `TX_OFFSET_FREQUENCY_DIRECTION` (OFF/ADD/SUB, from `settings.h`) and `TX_OFFSET_FREQUENCY`
- `TX_freq_check(f) == 0` means TX is allowed on frequency `f`

### Key API Facts (confirmed)
- `UI_PrintString(str, start, end, line, width)` — 5 args; centers text when `end > start`
- `UI_PrintStringSmallNormal(str, start, end, line)` — 4 args; was `UI_PrintStringSmall` in fagci
- `UI_PrintStringSmall(str, start, end, line, char_width, font)` — internal 6-arg version; do not call directly from spectrum code
- `UI_DrawPixelBuffer(buffer, x, y, black)` — y is relative to top of the `buffer` (0 = topmost row of that buffer)
- `gFont3x5[96][3]` — 3 bytes/char; each byte = one column, bits 0–4 = pixels top-to-bottom
- `BK4819_REG_73/74/75` not in enum but used as raw literals in `RegisterSpec` — compiles OK (implicit int cast)

### Build system
`make` requires the `arm-none-eabi-gcc` toolchain, not generally on PATH outside CI.
Build via Docker instead (see `compile-with-docker.sh` / `Dockerfile`, or mount the repo
into an `archlinux:latest` container and install the packages listed in
`.github/workflows/main.yml`).

### Flashing a real device
Hardware version 1 only (see Compatible Devices in README) — V2/V3 need a different
repo/firmware entirely, wrong firmware can brick the device.

- **CLI flasher: `tools/k5flash.py`.** Prefer this over `k5prog_win.exe` linked in the
  README — despite the name/docs treating it like the linux `k5prog` CLI, it's actually
  a GUI app and can't be scripted or driven headlessly (confirmed 2026-07-30). `k5flash.py`
  reimplements the same wire protocol as the egzumer uvtools web flasher
  (`qsSerial.js`/`tool_patcher.js`/`fwpack.js`, reverse-engineered from source), takes a
  `*.packed.bin`, needs only `pyserial`. Tests in `tools/test_k5flash.py` (CRC
  cross-checked against `crcmod`, full protocol flow against a simulated bootloader) pass
  without hardware, and it has since also been confirmed working against a real UV-K5 v1
  over COM10/CH340 (2026-07-30: "Successfully flashed firmware.").
- **Bootloader entry sequence matters and is easy to get wrong:** power off → hold PTT →
  power on (white LED must light) → *then* plug in the programming cable. Plugging the
  cable in before/without this gives `BufferOverrunError: Buffer overrun` on read (radio
  sends unexpected data because it booted normally instead of into the bootloader) —
  reload the flasher and redo the sequence from a full power-off, don't just retry.
- The web flasher (https://egzumer.github.io/uvtools/) needs a Chromium browser (Web
  Serial API) and — if driving it via claude-in-chrome — the port-picker and file-picker
  are native browser/OS dialogs outside the page DOM: use the `file_upload` tool for the
  firmware file (don't click the picker, you can't see or interact with it), and ask the
  user to complete the native serial-port-selection dialog themselves.
