# Egzumer Spectrum Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace this repo's `app/spectrum.c`/`app/spectrum.h` internals with egzumer/uv-k5-firmware-custom's current (HEAD) implementation — the user's preferred reference — while preserving this repo's existing transmit-from-STILL-screen capability that egzumer's version doesn't have, then remove now-dead code this swap orphans, and rewrite the host-side test fixture to match the new API.

**Architecture:** Bulk-copy egzumer's `app/spectrum.h` (used verbatim, zero patches needed) and `app/spectrum.c` (used as the new baseline) from a shallow clone of their repo, then apply a fixed sequence of precise patches on top of the copied `spectrum.c` to re-add TX support (`ToggleTX`, `txAllowState`, register vault backup/restore, PTT handling). Remove `app/finput.c/h`, `helper/measurements.c/h`, and `driver/bk4819.c`'s `BK4819_TuneTo` — all confirmed used exclusively by the old spectrum code and made fully dead by this swap. Rewrite `tools/host_tests/` against egzumer's actual API (no more `blacklist[]`/`mov`/`freqPresets` — replaced by `RSSI_MAX_VALUE`-as-sentinel and `settings.dbMin`/`dbMax` auto-ranging).

**Tech Stack:** C (ARM Cortex-M0 firmware), Docker-based `arm-none-eabi-gcc` build, Docker-based native-`gcc` host test harness (`tools/host_tests/`), `git` for cloning the reference.

## Global Constraints

- Every dependency egzumer's `spectrum.c` needs (`RADIO_SetupAGC`, `gRxVfo`/`gTxVfo`, `dBmCorrTable`, `AUDIO_AudioPathOn/Off`, `BATTERY_VoltsToPercent`, `gBatteryCheckCounter`, `chFrScanner.h`'s `gScanRangeStart`/`gScanRangeStop`, `BK4819_SetAGC`/`BK4819_InitAGC`, `am_fix.h` symbols, named `BK4819_REG_*` constants, `BAND_N_ELEM`) already exists in this repo under matching names — confirmed by grep before writing this plan. Do not re-verify these from scratch; if a build error surfaces one that's missing, that's new information, not an expected gap.
- `app/finput.c`/`.h` and `helper/measurements.c`/`.h` are used **exclusively** by the old `app/spectrum.c`/`.h` (confirmed via repo-wide grep) — safe to delete entirely, not just stop including.
- `driver/bk4819.c`'s `BK4819_TuneTo` is used **exclusively** by the old `app/spectrum.c` (confirmed via grep) — safe to delete; egzumer's own `SetF()` does its own equivalent PLL-recalibration register toggle inline, so no replacement is needed.
- TX-from-STILL-screen must keep working (user's explicit decision) — every patch in Task 1 that adds this back must be applied exactly as specified, not approximated.
- Every task must end with the real ARM firmware building successfully via the existing Docker toolchain (`uvk5-buildcheck` image, or rebuild it per `tools/host_tests/Dockerfile`-style Dockerfile if not present — see Task 1 Step 1) and fitting under the 60K flash budget (`text+data` from `arm-none-eabi-size`).
- Windows/git-bash long-path issue: cloning egzumer's full repo fails on Windows due to a deeply-nested CMSIS example path. Always clone with `--filter=blob:none --sparse` and `git sparse-checkout set app ui driver` (see Task 1 Step 2) — do not attempt a full clone.

---

### Task 1: Replace spectrum.h/.c with egzumer's version, preserving TX capability

**Files:**
- Modify: `app/spectrum.h` (replaced wholesale)
- Modify: `app/spectrum.c` (replaced wholesale, then patched)
- Delete: `app/finput.c`, `app/finput.h`
- Delete: `helper/measurements.c`, `helper/measurements.h`
- Modify: `Makefile:135` (remove `OBJS += app/finput.o`), `Makefile:154` (remove `OBJS += helper/measurements.o`)

**Interfaces:**
- Produces: `app/spectrum.c` now defines `ToggleTX(bool)`, `txAllowState` (type `VfoState_t`, from `radio.h`), `isTransmitting` (bool), `fTx` (uint32_t), `RegBackupSet(uint8_t, uint16_t)`/`RegRestore(uint8_t)`, `GetOffsetedF(VFO_Info_t*, uint32_t)`, `IsTXAllowed(uint32_t)`, `OnKeysReleased(void)` — all `static` except where noted. `APP_RunSpectrum(void)` keeps its existing external signature (unchanged, still forward-declared the same way in `app/action.c`/`app/main.c` — no changes needed there).
- Consumes: nothing from other tasks in this plan (this is the first task).

- [ ] **Step 1: Confirm the ARM build-check Docker image still exists**

```bash
docker image inspect uvk5-buildcheck >/dev/null 2>&1 && echo "exists" || echo "missing"
```

If it prints `missing`, rebuild it:

```bash
cd "$(git rev-parse --show-toplevel)"
cat > /tmp/Dockerfile.buildcheck << 'EOF'
FROM --platform=amd64 archlinux:latest
RUN pacman -Syyu base-devel --noconfirm
RUN pacman -Syyu arm-none-eabi-gcc --noconfirm
RUN pacman -Syyu arm-none-eabi-newlib --noconfirm
RUN pacman -Syyu git --noconfirm
RUN pacman -Syyu python-pip --noconfirm
RUN pacman -Syyu python-crcmod --noconfirm
WORKDIR /app
COPY . .
RUN git submodule update --init --recursive
EOF
MSYS_NO_PATHCONV=1 docker build -t uvk5-buildcheck -f /tmp/Dockerfile.buildcheck .
```

- [ ] **Step 2: Clone egzumer's repo (sparse, shallow) to a scratch location**

```bash
mkdir -p /tmp/egzumer-ref-src
cd /tmp/egzumer-ref-src
rm -rf egzumer-ref
git -c core.longpaths=true clone --depth 1 --filter=blob:none --sparse \
  https://github.com/egzumer/uv-k5-firmware-custom.git egzumer-ref
cd egzumer-ref
git sparse-checkout set app ui driver
```

Verify it worked:

```bash
test -f /tmp/egzumer-ref-src/egzumer-ref/app/spectrum.c && echo OK
test -f /tmp/egzumer-ref-src/egzumer-ref/app/spectrum.h && echo OK
```

Expected: two lines of `OK`.

- [ ] **Step 3: Copy egzumer's spectrum.h and spectrum.c as the new baseline**

```bash
cd "$(git rev-parse --show-toplevel)"
cp /tmp/egzumer-ref-src/egzumer-ref/app/spectrum.h app/spectrum.h
cp /tmp/egzumer-ref-src/egzumer-ref/app/spectrum.c app/spectrum.c
```

`app/spectrum.h` needs no further changes — every type/constant the TX patches below need (`VFO_Info_t`, `VfoState_t`, `TX_OFFSET_FREQUENCY_DIRECTION_ADD/SUB`) is already reachable through its existing `#include "../radio.h"`, and `VfoStateStr` is reachable through `app/spectrum.c`'s own `#include "ui/main.h"` (already present at the top of the copied file).

- [ ] **Step 4: Delete now-dead files**

```bash
rm app/finput.c app/finput.h
rm helper/measurements.c helper/measurements.h
```

- [ ] **Step 5: Update the Makefile**

Remove the `app/finput.o` line (currently `Makefile:135`, inside the `ifeq ($(ENABLE_SPECTRUM), 1)` block):

Before:
```makefile
ifeq ($(ENABLE_SPECTRUM), 1)
OBJS += app/finput.o
OBJS += app/spectrum.o
endif
```

After:
```makefile
ifeq ($(ENABLE_SPECTRUM), 1)
OBJS += app/spectrum.o
endif
```

Remove the standalone `helper/measurements.o` line (currently `Makefile:154`):

Before:
```makefile
OBJS += helper/battery.o
OBJS += helper/boot.o
OBJS += helper/measurements.o
OBJS += misc.o
```

After:
```makefile
OBJS += helper/battery.o
OBJS += helper/boot.o
OBJS += misc.o
```

- [ ] **Step 6: Apply patch 1 — TX-related global state**

In `app/spectrum.c`, find:

```c
bool isInitialized = false;
bool isListening = true;
bool monitorMode = false;
bool redrawStatus = true;
bool redrawScreen = false;
bool newScanStart = true;
bool preventKeypress = true;
bool audioState = true;
bool lockAGC = false;
```

Replace with:

```c
bool isInitialized = false;
bool isListening = true;
bool monitorMode = false;
bool redrawStatus = true;
bool redrawScreen = false;
bool newScanStart = true;
bool preventKeypress = true;
bool audioState = true;
bool lockAGC = false;

bool isTransmitting = false;
uint32_t fTx = 0;
VfoState_t txAllowState;
```

- [ ] **Step 7: Apply patch 2 — register vault backup/restore for TX register reconfiguration**

Find:

```c
static uint16_t registers_stack [sizeof(registers_to_save)];

static void BackupRegisters() {
```

Replace with:

```c
static uint16_t registers_stack [sizeof(registers_to_save)];

static uint16_t registersVault[128] = {0};

static void RegBackupSet(uint8_t num, uint16_t value) {
  registersVault[num] = BK4819_ReadRegister(num);
  BK4819_WriteRegister(num, value);
}

static void RegRestore(uint8_t num) {
  BK4819_WriteRegister(num, registersVault[num]);
}

static void BackupRegisters() {
```

- [ ] **Step 8: Apply patch 3 — SetTxF**

Find:

```c
static void SetF(uint32_t f) {
  fMeasure = f;

  BK4819_SetFrequency(fMeasure);
  BK4819_PickRXFilterPathBasedOnFrequency(fMeasure);
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_30, reg);
}
```

Replace with:

```c
static void SetF(uint32_t f) {
  fMeasure = f;

  BK4819_SetFrequency(fMeasure);
  BK4819_PickRXFilterPathBasedOnFrequency(fMeasure);
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_30, reg);
}

static void SetTxF(uint32_t f) {
  fTx = f;
  BK4819_SetFrequency(f);
}
```

- [ ] **Step 9: Apply patch 4 — GetOffsetedF / IsTXAllowed helpers**

Find:

```c
static uint8_t my_abs(signed v) { return v > 0 ? v : -v; }

void SetState(State state) {
```

Replace with:

```c
static uint8_t my_abs(signed v) { return v > 0 ? v : -v; }

static uint32_t GetOffsetedF(VFO_Info_t *vfo, uint32_t f) {
  switch (vfo->TX_OFFSET_FREQUENCY_DIRECTION) {
  case TX_OFFSET_FREQUENCY_DIRECTION_ADD:
    return f + vfo->TX_OFFSET_FREQUENCY;
  case TX_OFFSET_FREQUENCY_DIRECTION_SUB:
    return f - vfo->TX_OFFSET_FREQUENCY;
  default:
    return f;
  }
}

static bool IsTXAllowed(uint32_t f) { return TX_freq_check(f) == 0; }

void SetState(State state) {
```

- [ ] **Step 10: Apply patch 5 — ToggleRX/ToggleTX mutual exclusion + full ToggleTX**

Find (egzumer's original `ToggleRX`, no forward declaration, no early-return guard, no TX call):

```c
static void ToggleRX(bool on) {
  isListening = on;

  RADIO_SetupAGC(on, lockAGC);
  BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);

  ToggleAudio(on);
  ToggleAFDAC(on);
  ToggleAFBit(on);

  if (on) {
    listenT = 1000;
    BK4819_WriteRegister(0x43, listenBWRegValues[settings.listenBw]);
  } else {
    BK4819_WriteRegister(0x43, GetBWRegValueForScan());
  }
}
```

Replace with:

```c
static void ToggleTX(bool);

static void ToggleRX(bool on) {
  isListening = on;
  if (on) {
    ToggleTX(false);
  }

  RADIO_SetupAGC(on, lockAGC);
  BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);

  ToggleAudio(on);
  ToggleAFDAC(on);
  ToggleAFBit(on);

  if (on) {
    listenT = 1000;
    BK4819_WriteRegister(0x43, listenBWRegValues[settings.listenBw]);
  } else {
    BK4819_WriteRegister(0x43, GetBWRegValueForScan());
  }
}

static void ToggleTX(bool on) {
  if (isTransmitting == on) {
    return;
  }
  isTransmitting = on;
  if (on) {
    ToggleRX(false);
  }

  BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, on);

  if (on) {
    ToggleAudio(false);

    SetTxF(GetOffsetedF(gCurrentVfo, fMeasure));

    RegBackupSet(BK4819_REG_47, 0x6040);
    RegBackupSet(BK4819_REG_7E, 0x302E);
    RegBackupSet(BK4819_REG_50, 0x3B20);
    RegBackupSet(BK4819_REG_37, 0x1D0F);
    RegBackupSet(BK4819_REG_52, 0x028F);
    RegBackupSet(BK4819_REG_30, 0x0000);
    BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
    RegBackupSet(BK4819_REG_51, 0x0000);

    BK4819_SetupPowerAmplifier(gCurrentVfo->TXP_CalculatedSetting,
                               gCurrentVfo->pTX->Frequency);
  } else {
    RADIO_SendEndOfTransmission();
    RADIO_SendCssTail();

    BK4819_SetupPowerAmplifier(0, 0);

    RegRestore(BK4819_REG_51);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    RegRestore(BK4819_REG_30);
    RegRestore(BK4819_REG_52);
    RegRestore(BK4819_REG_37);
    RegRestore(BK4819_REG_50);
    RegRestore(BK4819_REG_7E);
    RegRestore(BK4819_REG_47);

    SetF(fMeasure);
  }
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, !on);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, on);
}
```

Note: `gCurrentVfo` (not `gRxVfo`/`gTxVfo`) is used deliberately here — it's this repo's own "currently active for TX" pointer (follows `TX_VFO` unless crossband/dual-watch changes it), already the correct choice proven by the pre-swap code.

- [ ] **Step 11: Apply patch 6 — DrawF shows TX state**

Find:

```c
static void DrawF(uint32_t f) {
  sprintf(String, "%u.%05u", f / 100000, f % 100000);
  UI_PrintStringSmallNormal(String, 8, 127, 0);

  sprintf(String, "%3s", gModulationStr[settings.modulationType]);
  GUI_DisplaySmallest(String, 116, 1, false, true);
  sprintf(String, "%s", bwOptions[settings.listenBw]);
  GUI_DisplaySmallest(String, 108, 7, false, true);
}
```

Replace with:

```c
static void DrawF(uint32_t f) {
  sprintf(String, "%u.%05u", f / 100000, f % 100000);

  if (currentState == STILL && kbd.current == KEY_PTT) {
    if (txAllowState) {
      sprintf(String, "%s", VfoStateStr[txAllowState]);
    } else if (isTransmitting) {
      uint32_t txF = GetOffsetedF(gCurrentVfo, f);
      sprintf(String, "TX %u.%05u", txF / 100000, txF % 100000);
    }
  }
  UI_PrintStringSmallNormal(String, 8, 127, 0);

  sprintf(String, "%3s", gModulationStr[settings.modulationType]);
  GUI_DisplaySmallest(String, 116, 1, false, true);
  sprintf(String, "%s", bwOptions[settings.listenBw]);
  GUI_DisplaySmallest(String, 108, 7, false, true);
}
```

- [ ] **Step 12: Apply patch 7 — AF output level meter during TX in RenderStill**

First, add a small local helper to replace the deleted `helper/measurements.c`'s `ConvertDomain` for this one remaining use. Find:

```c
static int clamp(int v, int min, int max) {
  return v <= min ? min : (v >= max ? max : v);
}
```

Replace with:

```c
static int clamp(int v, int min, int max) {
  return v <= min ? min : (v >= max ? max : v);
}

// Was helper/measurements.c's ConvertDomain(afDB, 26, 194, 0, 121) -- inlined
// here since that file is deleted (nothing else needed it).
static uint8_t ScaleAfLevel(uint8_t afDB) {
  int v = clamp((int)afDB, 26, 194);
  return ((v - 26) * 121 + (194 - 26) / 2) / (194 - 26);
}
```

Then find (in `RenderStill`):

```c
  if (!monitorMode) {
    uint8_t x = Rssi2PX(settings.rssiTriggerLevel, 0, 121);
    gFrameBuffer[2][METER_PAD_LEFT + x] = 0b11111111;
  }

  const uint8_t PAD_LEFT = 4;
```

Replace with:

```c
  if (!monitorMode) {
    uint8_t x = Rssi2PX(settings.rssiTriggerLevel, 0, 121);
    gFrameBuffer[2][METER_PAD_LEFT + x] = 0b11111111;
  }

  if (isTransmitting) {
    uint8_t afDB = BK4819_ReadRegister(0x6F) & 0b1111111;
    uint8_t afPX = ScaleAfLevel(afDB);
    for (uint8_t i = 0; i < afPX; ++i) {
      gFrameBuffer[3][i + METER_PAD_LEFT] |= 0b00000011;
    }
  }

  const uint8_t PAD_LEFT = 4;
```

- [ ] **Step 13: Apply patch 8 — real PTT/TX in OnKeyDownStill**

Find:

```c
  case KEY_PTT:
    // TODO: start transmit
    /* BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true); */
    break;
```

Replace with:

```c
  case KEY_PTT:
    if (gBatteryDisplayLevel == 6) {
      txAllowState = VFO_STATE_VOLTAGE_HIGH;
    } else if (IsTXAllowed(GetOffsetedF(gCurrentVfo, fMeasure))) {
      txAllowState = VFO_STATE_NORMAL;
      ToggleTX(true);
    } else {
      txAllowState = VFO_STATE_TX_DISABLE;
    }
    redrawScreen = true;
    break;
```

(`gBatteryDisplayLevel` is `helper/battery.h`'s existing global, already maintained by the main app loop — no local battery-reading code needed here.)

- [ ] **Step 14: Apply patch 9 — stop TX when keys release**

Find:

```c
static void RenderFreqInput() { UI_PrintString(freqInputString, 2, 127, 0, 8); }
```

Replace with:

```c
static void OnKeysReleased() {
  if (isTransmitting) {
    ToggleTX(false);
  }
}

static void RenderFreqInput() { UI_PrintString(freqInputString, 2, 127, 0, 8); }
```

Then find (in `HandleUserInput`):

```c
bool HandleUserInput() {
  kbd.prev = kbd.current;
  kbd.current = GetKey();

  if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
```

Replace with:

```c
bool HandleUserInput() {
  kbd.prev = kbd.current;
  kbd.current = GetKey();

  if (kbd.current == KEY_INVALID) {
    OnKeysReleased();
  }

  if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
```

- [ ] **Step 15: Apply patch 10 — Tick() skips scan/listen updates while transmitting**

Find (in `Tick`):

```c
  if (isListening && currentState != FREQ_INPUT) {
    UpdateListening();
  } else {
    if (currentState == SPECTRUM) {
      UpdateScan();
    } else if (currentState == STILL) {
      UpdateStill();
    }
  }
```

Replace with:

```c
  if (isTransmitting) {
    // Nothing to do here; ToggleTX() and OnKeysReleased() manage TX state.
  } else if (isListening && currentState != FREQ_INPUT) {
    UpdateListening();
  } else {
    if (currentState == SPECTRUM) {
      UpdateScan();
    } else if (currentState == STILL) {
      UpdateStill();
    }
  }
```

- [ ] **Step 16: Apply patch 11 — safely stop TX on exit**

Find:

```c
static void DeInitSpectrum() {
  SetF(initialFreq);
  RestoreRegisters();
  isInitialized = false;
}
```

Replace with:

```c
static void DeInitSpectrum() {
  ToggleTX(false);
  SetF(initialFreq);
  RestoreRegisters();
  isInitialized = false;
}
```

- [ ] **Step 17: Verify no dangling references to the deleted helper/measurements.h functions remain**

```bash
cd "$(git rev-parse --show-toplevel)"
grep -n "ConvertDomain\|\bMid(\|\bClamp(" app/spectrum.c
```

Expected: no output (all three were either replaced in the patches above or never used by egzumer's code).

- [ ] **Step 18: Build the real firmware and fix any remaining errors**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

This will very likely surface a handful of undefined-reference or type-mismatch errors beyond what's covered above (e.g. a symbol name that differs slightly between what this repo has and what egzumer's code expects) — that's expected for a swap this size. Fix each one directly in `app/spectrum.c`/`.h` by checking what the equivalent symbol is actually called in this repo (`grep -rn <missing_symbol> --include=*.h .`), rather than reintroducing anything from the deleted `helper/measurements.h`/`app/finput.h`.

Expected once clean: `arm-none-eabi-size` output showing `text+data` under 61440 bytes (the flash budget from `firmware.ld`).

- [ ] **Step 19: Commit**

```bash
git add app/spectrum.h app/spectrum.c Makefile
git rm app/finput.c app/finput.h helper/measurements.c helper/measurements.h
git commit -m "$(cat <<'EOF'
Replace spectrum analyzer with egzumer's current implementation

Swaps this repo's app/spectrum.c/h (based on an older fagci snapshot)
for egzumer/uv-k5-firmware-custom's current HEAD version, per user
preference, while preserving this repo's TX-from-STILL-screen
capability that egzumer's version doesn't have (ToggleTX, txAllowState,
register vault backup/restore, PTT handling in OnKeyDownStill).

Removes app/finput.c/h and helper/measurements.c/h: both were used
exclusively by the old spectrum code (confirmed via repo-wide grep)
and egzumer's version has its own equivalents built in.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Remove now-dead BK4819_TuneTo

**Files:**
- Modify: `driver/bk4819.c`
- Modify: `driver/bk4819.h`

**Interfaces:**
- Consumes: Task 1 must be complete and committed (this task assumes `app/spectrum.c` no longer calls `BK4819_TuneTo`).
- Produces: nothing new; this is pure removal.

- [ ] **Step 1: Confirm nothing calls BK4819_TuneTo anymore**

```bash
cd "$(git rev-parse --show-toplevel)"
grep -rn "BK4819_TuneTo" --include=*.c --include=*.h . | grep -v tools/host_tests
```

Expected: only the two lines in `driver/bk4819.c` (definition) and `driver/bk4819.h` (declaration) themselves.

- [ ] **Step 2: Remove the declaration**

In `driver/bk4819.h`, find:

```c
void     BK4819_TuneTo(uint32_t f, bool precise);
```

Delete this line.

- [ ] **Step 3: Remove the definition**

In `driver/bk4819.c`, find and delete the entire function:

```c
void BK4819_TuneTo(uint32_t f, bool precise) {
  BK4819_PickRXFilterPathBasedOnFrequency(f);
  BK4819_SetFrequency(f);

  // Writing the new frequency to REG_38/REG_39 alone does not make the PLL
  // actually relock -- toggling the VCO calibration bit in REG_30 (write a
  // modified value, then write the original back) strobes the chip into
  // recalibrating for the new frequency. Without this, RX stays tuned to
  // whatever frequency it last locked onto, so every RSSI reading during a
  // fast scan sweep comes back identical regardless of scanInfo.f.
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  if (precise) {
    BK4819_WriteRegister(BK4819_REG_30, 0x0200);
  } else {
    BK4819_WriteRegister(BK4819_REG_30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }
  BK4819_WriteRegister(BK4819_REG_30, reg);
}
```

- [ ] **Step 4: Build the real firmware**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

Expected: clean build, `arm-none-eabi-size` shows `text+data` still under 61440 bytes.

- [ ] **Step 5: Commit**

```bash
git add driver/bk4819.c driver/bk4819.h
git commit -m "$(cat <<'EOF'
Remove BK4819_TuneTo, now unused after the spectrum swap

egzumer's app/spectrum.c does its own equivalent PLL-recalibration
register toggle inline in its own SetF(), so the standalone driver
function this repo added is dead code now.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Rewrite the host test fixture for the new API

**Files:**
- Modify: `tools/host_tests/stubs.c`
- Modify: `tools/host_tests/fake_signal.h`
- Modify: `tools/host_tests/test_spectrum.c`
- Modify: `tools/host_tests/build.sh`

**Interfaces:**
- Consumes: Task 1 and Task 2 complete (compiles the new `app/spectrum.c` via `#include`, same technique as before).
- Produces: a passing `tools/host_tests/build.sh` run, exercising: `UpdateDBMax` (the new KEY_3/KEY_9 behavior, replacing the old band-switch test), the STILL-screen S-meter/frequency layout (still no-collision, positions differ from before), fake-signal peak detection (now using `RSSI_MAX_VALUE`-as-skip-sentinel instead of a separate `blacklist[]` array), and the arrow/text row-5 layout (now via egzumer's own `GUI_DisplaySmallest`/`PutPixel`, not the old `UI_PrintStringSmallest`).

**Key API differences from the old tests (reference, not code to write yet):**
- `ScanInfo.i`/`.iPeak` are now `uint16_t` (were `uint8_t`).
- No `blacklist[128]` array — `rssiHistory[idx] == RSSI_MAX_VALUE` marks a skipped bin instead (see egzumer's `Scan()`/`DrawSpectrum()`).
- No `MovingAverage`/`mov` — `Rssi2Y()`/`Rssi2PX()` use `settings.dbMin`/`settings.dbMax` (auto-updated via `UpdateScanInfo()`'s "new rssiMin" branch calling `Rssi2DBm()`).
- `KEY_3`/`KEY_9` now call `UpdateDBMax(true/false)` (adjusts `settings.dbMax`), not any band-switch logic — `freqPresets`/`ApplyPreset`/`SelectNearestPreset` do not exist in the new code at all.
- `registerSpecs[]` has 5 entries now (`{}, LNAs, LNA, PGA, IF`), not 9.
- Text drawing goes through egzumer's own local `GUI_DisplaySmallest(str, x, y, statusbar, fill)` (explicit bool for buffer selection, `y` used directly with no offset) and `PutPixel`/`PutPixelStatus`, not the old `UI_PrintStringSmallest`.

- [ ] **Step 1: Update stubs.c — remove obsolete stubs, add new ones**

Read the current `tools/host_tests/stubs.c` in full first (`Read` tool), then rewrite it. Remove: the `BK4819_TuneTo` stub (function no longer exists), the `freqInputString`/`freqInputIndex`/`tempFreq`/`FreqInput`/`UpdateFreqInput` stubs (egzumer's `spectrum.c` defines these itself now, non-static, at file scope — a stub definition would collide at link time), the `#include "../../app/finput.h"` line.

Add:

```c
#include "../../driver/backlight.h"

VFO_Info_t *gRxVfo = &gVfoInfoStub;
VFO_Info_t *gTxVfo = &gVfoInfoStub;

const int8_t dBmCorrTable[7] = {0, 0, 0, 0, 0, 0, 0};

uint16_t gBatteryCheckCounter;

void RADIO_SetupAGC(bool listeningAM, bool disable) {
    (void)listeningAM;
    (void)disable;
}

void BACKLIGHT_TurnOn(void) {}
void BACKLIGHT_TurnOff(void) {}
```

Keep: `gCurrentVfo`, `gEeprom`, `gBatteryVoltages`, `gBatteryCurrent`, `gBatteryDisplayLevel`, `gBatteryCalibration`, `gModulationStr`, `VfoStateStr`, `gStatusLine`/`gFrameBuffer`, the `BK4819_*` register stubs, `KEYBOARD_Poll`, `SYSTEM_DelayMs`, `SYSTICK_DelayUs`, `BOARD_ADC_GetBatteryInfo`, `UI_DisplayBattery`, `ST7565_Blit*`, `_putchar`, and the fake-RSSI-profile plumbing (`fake_rssi_profile`/`fake_rssi_profile_len`/`fake_rssi_profile_pos`/`BK4819_GetRSSI`).

- [ ] **Step 2: Update build.sh — link `driver/backlight.o` is NOT needed (BACKLIGHT_TurnOn/Off are stubbed), no new object files required**

No changes needed to `build.sh` itself — it already compiles `font.c`, `helper/measurements.c` is now gone so remove that line:

Find:

```bash
gcc $CFLAGS -c helper/measurements.c -o "$OUT/measurements.o"
```

Delete this line, and remove `"$OUT/measurements.o"` from the final link command's argument list.

- [ ] **Step 3: Attempt a build to discover the exact remaining undefined symbols**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh 2>&1 | grep "undefined reference" | sed -E "s/.*undefined reference to \`([^']*)'.*/\1/" | sort -u
```

(If the `uvk5-hosttest` image doesn't exist, rebuild it first: `docker build -t uvk5-hosttest -f tools/host_tests/Dockerfile .`)

Add a minimal stub in `stubs.c` for anything this reveals beyond what Step 1 anticipated, following the same pattern as the existing stubs (no-op bodies for hardware writes, sensible fixed values for reads).

- [ ] **Step 4: Rewrite test_spectrum.c's tests for the new API**

Read the current `tools/host_tests/test_spectrum.c` in full, then replace the following tests:

Replace `test_key3_key9_band_switch` (band presets no longer exist) with:

```c
static void test_key3_key9_adjusts_db_range(void) {
    printf("\n-- test_key3_key9_adjusts_db_range --\n");

    settings.dbMin = -130;
    settings.dbMax = -50;

    int before = settings.dbMax;
    OnKeyDown(KEY_3); // UpdateDBMax(true)
    CHECK(settings.dbMax == before + 1);

    int afterUp = settings.dbMax;
    OnKeyDown(KEY_9); // UpdateDBMax(false)
    CHECK(settings.dbMax == afterUp - 1);
}
```

Replace `test_spectrum_scan_finds_peak` and `run_fake_sweep`'s blacklist-related assumptions: `ResetBlacklist()` no longer exists (egzumer's version zeroes `RSSI_MAX_VALUE`-marked entries back to 0 instead — see egzumer's own `ResetBlacklist()`, which is still the right name/signature, just a different body; the `#include`d real `app/spectrum.c` already provides the correct one, so `run_fake_sweep`'s call to `ResetBlacklist()` needs no change). Update the peak-detection assertions:

```c
static void test_spectrum_scan_finds_peak(void) {
    printf("\n-- test_spectrum_scan_finds_peak --\n");

    settings.stepsCount = STEPS_64;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    currentFreq = 14500000;
    settings.scanStepIndex = S_STEP_25_0kHz;
    uint16_t n = GetStepsCount();

    uint16_t profile[FAKE_RSSI_PROFILE_MAX];
    for (int i = 0; i <= n; i++) profile[i] = 300; // flat noise floor
    profile[20] = 500; // synthetic strong signal at bin 20

    run_fake_sweep(profile, n);

    CHECK(rssiHistory[20] == 500);
    CHECK(scanInfo.iPeak == 20);
    CHECK(peak.i == 20);

    uint8_t y_peak = Rssi2Y(rssiHistory[20]);
    uint8_t y_floor = Rssi2Y(rssiHistory[5]);
    CHECK(y_peak < y_floor);
}
```

(Note: `run_fake_sweep`'s `for (int i = 0; i <= n; i++)` loop and its `uint16_t n` type must be updated to match `GetStepsCount()`'s new `uint16_t` return type — check the current helper's signature and widen `int n` to `uint16_t n` throughout if needed.)

Remove `test_trigger_line_frozen_with_low_signal` entirely — it was diagnosing a `mov.min`/`mov.max` mechanism that no longer exists (egzumer's dB-range auto-scaling replaces it, and the underlying frozen-line bug is already fixed at the driver level from the earlier `BK4819_TuneTo` work, now superseded by egzumer's own `SetF()`).

Update `test_still_screen_no_collision`: egzumer's `RenderStill()` draws the S-meter text at `GUI_DisplaySmallest(String, 4, 25, false, true)` / `(String, 28, 25, false, true)` — both `y=25`, going straight to `gFrameBuffer` (not the old two-row split). Replace the row0/row1 check with a single check that this text lands in `gFrameBuffer` row `25/8 = 3` and the frequency line (still via `UI_PrintStringSmallNormal`, unchanged, row 0) doesn't collide with it — they're 3 rows apart, so simplify to:

```c
static void test_still_screen_no_collision(void) {
    printf("\n-- test_still_screen_no_collision --\n");

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    memset(gStatusLine, 0, sizeof(gStatusLine));

    fMeasure = 14500000;
    scanInfo.rssi = 400;
    isTransmitting = false;
    kbd.current = KEY_INVALID;
    txAllowState = VFO_STATE_NORMAL;
    monitorMode = false;
    settings.rssiTriggerLevel = 350;
    settings.dbMin = -130;
    settings.dbMax = -50;

    RenderStill();

    int row0_has_content = 0, row3_has_content = 0;
    for (int c = 0; c < LCD_WIDTH; c++) {
        if (gFrameBuffer[0][c] != 0) row0_has_content = 1;
        if (gFrameBuffer[3][c] != 0) row3_has_content = 1;
    }
    CHECK(row0_has_content); // big frequency line
    CHECK(row3_has_content); // S-meter/dBm text (y=25 -> row 3)
}
```

Update `test_spectrum_arrow_text_collision`: egzumer's `DrawArrow` is unchanged in shape/row (still hardcoded `gFrameBuffer[5]`), and `DrawNums`'s center-mode text still lands in row 5 via `GUI_DisplaySmallest(..., 36, 49, false, true)` — same `y=49` value, but now routed through egzumer's own `PutPixel` (no `-8` offset bug), so recompute what row this actually lands in: `y=49` directly indexes `gFrameBuffer[49/8] = gFrameBuffer[6]`, **not** row 5. This means the collision this test was written to catch **may already be gone** by construction once egzumer's own addressing is in use. Keep the test, but update the assertion to reflect the new expectation:

```c
static void test_spectrum_arrow_text_collision(void) {
    printf("\n-- test_spectrum_arrow_text_collision --\n");

    settings.stepsCount = STEPS_64;
    currentFreq = 14500000;
    settings.scanStepIndex = S_STEP_25_0kHz;
    settings.frequencyChangeStep = 8000;
    CHECK(IsCenterMode());

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    DrawNums();
    uint8_t text_row5[LCD_WIDTH], text_row6[LCD_WIDTH];
    memcpy(text_row5, gFrameBuffer[5], LCD_WIDTH);
    memcpy(text_row6, gFrameBuffer[6], LCD_WIDTH);

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    peak.i = 31;
    DrawArrow(128u * peak.i / GetStepsCount());
    uint8_t arrow_row5[LCD_WIDTH];
    memcpy(arrow_row5, gFrameBuffer[5], LCD_WIDTH);

    printf("   text landed in row5=%s row6=%s\n",
           /* true if any byte nonzero */ ({int any=0; for(int x=0;x<LCD_WIDTH;x++) if(text_row5[x]) any=1; any;}) ? "yes" : "no",
           ({int any=0; for(int x=0;x<LCD_WIDTH;x++) if(text_row6[x]) any=1; any;}) ? "yes" : "no");

    int overlap_col = -1;
    for (int x = 0; x < LCD_WIDTH; x++) {
        if (text_row5[x] & arrow_row5[x]) { overlap_col = x; break; }
    }
    CHECK(overlap_col == -1);
}
```

Run it once with just the `printf` (before trusting the `CHECK`) to see which row the text actually lands in, and adjust the `CHECK` to match reality if the printed answer is "row6=yes, row5=no" (expected) rather than assuming.

Update `main()` to call `test_key3_key9_adjusts_db_range` instead of the removed `test_key3_key9_band_switch`, and remove the call to the deleted `test_trigger_line_frozen_with_low_signal`.

- [ ] **Step 5: Build and run the host test suite**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

Expected: `PASSED (0 failures)`. Iterate on any remaining compile/link errors or failing assertions — given the API surface changed this much, expect at least one or two rounds of adjustment beyond what's specified above; use the printed `FAIL` messages and the real `app/spectrum.c` source (now egzumer's) as ground truth, not the old removed code.

- [ ] **Step 6: Verify the tests actually catch regressions, not just pass vacuously**

Pick one test (e.g. `test_key3_key9_adjusts_db_range`), temporarily comment out its core assertion in a scratch copy, confirm it fails, then confirm the real file still has the assertion in place. (This mirrors the verification done for the original S-meter/band-switch fixes earlier in this project — don't skip it just because this is "just a test rewrite".)

- [ ] **Step 7: Commit**

```bash
git add tools/host_tests/
git commit -m "$(cat <<'EOF'
Rewrite host test fixture for egzumer's spectrum API

Old tests assumed blacklist[]/mov/freqPresets, none of which exist in
egzumer's implementation. Replaces the band-switch test with a
dB-range-adjustment test (matching egzumer's actual KEY_3/KEY_9
behavior), updates the STILL-screen and row-5 layout tests for
egzumer's own GUI_DisplaySmallest/PutPixel addressing (no more -8
offset bug), and drops the frozen-trigger-line diagnostic (superseded
by the driver-level SetF fix now built into egzumer's code).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Flash to real hardware and verify

**Files:** none (verification only)

**Interfaces:**
- Consumes: Tasks 1-3 complete, firmware builds clean.

- [ ] **Step 1: Build the packed firmware**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

- [ ] **Step 2: Ask the user to put the radio in bootloader mode**

Power off → hold PTT → power on (white LED must light) → then plug in the programming cable. Confirm with the user before flashing (do not assume the sequence was already done).

- [ ] **Step 3: Flash**

```bash
cd "$(git rev-parse --show-toplevel)"
python tools/k5flash.py -p COM10 firmware_uvk5_v1.packed.bin
```

(Adjust `COM10` if `python tools/k5flash.py --list-ports` shows a different port.)

- [ ] **Step 4: Ask the user to verify on the real device**

Specifically check: the squelch/trigger line responds to `*`/F (the original bug report), KEY_3/KEY_9 now adjust the dB display range (not band presets — this is an intentional behavior change from before, the user should be told what changed), the frequency-range text at the bottom of the SPECTRUM screen no longer collides with anything, and — critically, since this is the one custom addition on top of egzumer's code — that PTT-to-transmit from the STILL screen still works correctly (radio actually transmits, releasing PTT stops it, `txAllowState` messages like "TX_DISABLE"/"VOLTAGE_HIGH" display correctly when TX is blocked).

- [ ] **Step 5: Clean up build artifacts**

```bash
cd "$(git rev-parse --show-toplevel)"
rm -f firmware_uvk5_v1 firmware_uvk5_v1.bin firmware_uvk5_v1.packed.bin
```
