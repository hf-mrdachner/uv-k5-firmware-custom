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

The merge is **currently broken** – the fagci spectrum app calls functions that don't exist in the DualTachyon API.

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
| `app/spectrum.c` / `app/spectrum.h` | Spectrum analyzer app – **main broken file** |
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

### Repair Status – COMPLETED

All missing symbols have been implemented. The following summarises what was done:

#### Added to `driver/bk4819.h` / `driver/bk4819.c`
| Symbol | Implementation |
|--------|---------------|
| `BK4819_GetRegValue(RegisterSpec s)` | `(BK4819_ReadRegister(s.num) >> s.offset) & s.mask` |
| `BK4819_TuneTo(uint32_t f, bool precise)` | `BK4819_SetFrequency(f); BK4819_PickRXFilterPathBasedOnFrequency(f);` (`precise` unused) |
| `BK4819_ToggleAFDAC(bool on)` | read-modify-write bit 9 (`BK4819_REG_30_MASK_ENABLE_AF_DAC`) in `BK4819_REG_30` |
| `BK4819_ToggleAFBit(bool on)` | read-modify-write bit 6 in `BK4819_REG_47` |

#### Fixed in `radio.h` / `radio.c`
`RADIO_EnableCxCSS()` was not added — instead the one call site in `spectrum.c` was changed to call `RADIO_SendCssTail()` directly.

#### Added as `static inline` in `app/spectrum.h`
| Symbol | Implementation |
|--------|---------------|
| `BK4819_SetModulation(ModulationMode_t m)` | `RADIO_SetModulation(m)` (avoids bk4819↔radio circular dep) |
| `GetOffsetedF(VFO_Info_t *vfo, uint32_t f)` | switch on `TX_OFFSET_FREQUENCY_DIRECTION` ADD/SUB/OFF |
| `GetTuneF(uint32_t f)` | identity — return `f` |
| `GetScreenF(uint32_t f)` | identity — return `f` |
| `IsTXAllowed(uint32_t f)` | `TX_freq_check(f) == 0` |
| `DrawVLine(uint8_t x, uint8_t y1, uint8_t y2, bool black)` | `UI_DrawLineBuffer(gFrameBuffer, x, y1, x, y2, black)` — renamed from `DrawHLine` (was misnamed) |
| `PutPixel(uint8_t x, uint8_t y, uint8_t color)` | `UI_DrawPixelBuffer(gFrameBuffer, x, y, color != 0)` |
| `UI_PrintStringSmallest(str, x, y, align, color)` | renders `gFont3x5` into `gStatusLine` (y<8) or `gFrameBuffer` (y≥8, framebuffer_y = y-8); `align` unused (left-aligned) |

`spectrum.h` also now includes `"../ui/main.h"` to get `VfoStateStr[]`.

#### Fixed in `app/spectrum.c`
| Location | Problem | Fix applied |
|----------|---------|-------------|
| Line 100–101 | `BK4819_GetRegValue(s)` commented out; raw read had no mask/shift | Uncommented, removed raw fallback |
| Line 678 | `VfoState[txAllowState]` — type is `VfoState_t` (enum), not `const char *` | Changed to `sprintf(String, "%s", VfoStateStr[txAllowState])` |
| Line 684 | `UI_PrintStringSmall(String, 8, 127, 0)` — renamed in DualTachyon | Replaced with `UI_PrintStringSmallNormal(String, 8, 127, 0)` |
| Line 1079 | `UI_PrintString(freqInputString, 2, 127, 0, 8, true)` — 6 args, API only has 5 | Removed 6th `true` argument |

#### Renamed / renamed-only symbols (no missing implementation)
- `UI_PrintStringSmall(str, start, end, line)` → **renamed** to `UI_PrintStringSmallNormal` in DualTachyon
- `VfoState[]` (fagci: `const char *[]` of state strings) → **renamed** to `VfoStateStr[]` in DualTachyon; `VfoState[]` in DualTachyon is `VfoState_t[]` (enum values for the two VFOs)

#### Additional findings not in original analysis
- `VfoStateStr` was only defined in `ui/main.c` with no header export — fixed by adding `extern const char *VfoStateStr[];` to `ui/main.h`
- `UI_PrintString` lost its 6th `bool center` arg in DualTachyon; centering is now automatic when `End > Start`

### Key API Facts (confirmed)
- `UI_PrintString(str, start, end, line, width)` — 5 args; centers text when `end > start`
- `UI_PrintStringSmallNormal(str, start, end, line)` — 4 args; was `UI_PrintStringSmall` in fagci
- `UI_PrintStringSmall(str, start, end, line, char_width, font)` — internal 6-arg version; do not call directly from spectrum code
- `UI_DrawPixelBuffer(buffer, x, y, black)` — y is relative to top of the `buffer` (0 = topmost row of that buffer)
- `gFont3x5[96][3]` — 3 bytes/char; each byte = one column, bits 0–4 = pixels top-to-bottom
- `BK4819_REG_73/74/75` not in enum but used as raw literals in `RegisterSpec` — compiles OK (implicit int cast)

### Pending Fixes (from code review of commit 6e0f3d1)

| # | Severity | File | Issue |
|---|----------|------|-------|
| 1 | ~~**Bug**~~ FIXED | `app/spectrum.h` | `uint8_t px` in `UI_PrintStringSmallest` — changed to `uint16_t` to prevent wrap-around corruption. |
| 2 | ~~**Bug**~~ FIXED | `app/spectrum.h` + `spectrum.c` | `DrawHLine` renamed to `DrawVLine`, params reordered to `(x, y1, y2, black)`. Call site in `spectrum.c` updated. |
| 3 | Style | `driver/bk4819.c:843` | `BK4819_ToggleAFBit` uses magic `(1u << 6)` for an undocumented REG_47 bit. Add a comment explaining what the bit controls. |
| 4 | Style | `app/spectrum.h` | `UI_PrintStringSmallest` is a ~30-line `static inline` in a header. Move to `spectrum.c` as a `static` function to avoid flash duplication if the header is ever included from a second TU. |
| 5 | Style | `app/spectrum.h` | Missing newline at EOF — can trigger `-Wnewline-eof` on some toolchains. |

### Open Questions
- `BK4819_TuneTo(..., precise=false)` currently ignores the flag — both paths have identical settling. A fast-scan path may need a shorter delay for useful spectrum scan speed.
- `GetTuneF` / `GetScreenF` are identity stubs. The original fagci code may have applied a TCXO calibration offset here. Verify against fagci source before assuming identity is correct.
- `UI_PrintStringSmallest` with `align=true` is currently always left-aligned. If status bar labels need centering or right-alignment, this must be implemented.
- Build system: `make` is not on PATH in the current shell; use Docker or WSL to compile.
