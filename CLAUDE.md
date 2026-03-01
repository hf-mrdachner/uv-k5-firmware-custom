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

## Not Allowed
- `git push` – do not push to any remote repository
- `git push --force` or any other remote operations
- Automatically creating or merging pull requests

## Code Style


## Project Context

### Overview
Firmware for the Quansheng UV-K5 handheld radio (DP32G030 MCU, ARM Cortex-M0).
This repo is a **merge of two forks** of the DualTachyon base firmware:
- **DualTachyon** (base): well-structured original firmware in C
- **fagci**: added a spectrum analyzer app (`app/spectrum.c` / `app/spectrum.h`)

The merge is **currently broken** – the fagci spectrum app calls functions that don't exist in the DualTachyon API.

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
- `BK4819_SetRegValue(s, v)` writes a bit-field; the inverse `BK4819_GetRegValue(s)` **does not yet exist** (see below)
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
| `DrawHLine(uint8_t y1, uint8_t y2, uint8_t x, bool black)` | `UI_DrawLineBuffer(gFrameBuffer, x, y1, x, y2, black)` |
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

### Open Questions
- Does `BK4819_TuneTo(..., precise=false)` need a shorter settling delay than `precise=true`? (Currently treated as identical.)
- Should `UI_PrintStringSmallest` with `align=true` center the string or right-align it? Currently ignored (always left-aligned from x). Affects status bar layout only.
- Build system: `make` is not on PATH in the current shell; use Docker or WSL to compile.
