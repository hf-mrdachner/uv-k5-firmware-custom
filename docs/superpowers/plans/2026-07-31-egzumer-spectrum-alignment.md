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

---

### Task 5: Restore automatic band-preset selection on entry

**Context (found during Task 4's hardware verification):** egzumer's `APP_RunSpectrum()` has no equivalent to the old, pre-swap `freqPresets[]`/`ApplyPreset()`/`AutomaticPresetChoose()` — it just centers the scan window on whatever frequency the active VFO already has, using a generic default step size. The old code snapped to a curated, band-appropriate window (e.g. "2mHam" 144.4-148.0MHz, `S_STEP_25_0kHz`, `BK4819_FILTER_BW_WIDE`) automatically whenever the current frequency fell inside one. The user explicitly asked for this to come back, as an addition on top of egzumer's code — same pattern as Task 1's TX-preservation splice, not a redesign of egzumer's structure.

Note: `KEY_3`/`KEY_9` stay exactly as Task 1 left them (`UpdateDBMax`) — this task is **only** about automatic selection on entry, not about restoring manual preset-switching via keypress (egzumer's own key layout has no room for it, and the user did not ask for it back).

**Files:**
- Modify: `app/spectrum.h` (add `FreqPreset` struct + `freqPresets[]` table)
- Modify: `app/spectrum.c` (add `ApplyPreset`/`AutomaticPresetChoose`, patch `APP_RunSpectrum`)

**Interfaces:**
- Consumes: Tasks 1-3 complete (this builds on the already-swapped `app/spectrum.c`/`.h`).
- Produces: `ApplyPreset(FreqPreset)` and `AutomaticPresetChoose(uint32_t)`, both `static`, used only within `app/spectrum.c`.

- [ ] **Step 1: Add FreqPreset struct and freqPresets table to app/spectrum.h**

Find (near the end of the `ScanStep` enum, right before `typedef struct SpectrumSettings`):

```c
typedef enum ScanStep {
  S_STEP_0_01kHz,
  S_STEP_0_1kHz,
  S_STEP_0_5kHz,
  S_STEP_1_0kHz,

  S_STEP_2_5kHz,
  S_STEP_5_0kHz,
  S_STEP_6_25kHz,
  S_STEP_8_33kHz,
  S_STEP_10_0kHz,
  S_STEP_12_5kHz,
  S_STEP_15_0kHz,
  S_STEP_20_0kHz,
  S_STEP_25_0kHz,
  S_STEP_50_0kHz,
  S_STEP_100_0kHz,
} ScanStep;

typedef struct SpectrumSettings {
```

Replace with:

```c
typedef enum ScanStep {
  S_STEP_0_01kHz,
  S_STEP_0_1kHz,
  S_STEP_0_5kHz,
  S_STEP_1_0kHz,

  S_STEP_2_5kHz,
  S_STEP_5_0kHz,
  S_STEP_6_25kHz,
  S_STEP_8_33kHz,
  S_STEP_10_0kHz,
  S_STEP_12_5kHz,
  S_STEP_15_0kHz,
  S_STEP_20_0kHz,
  S_STEP_25_0kHz,
  S_STEP_50_0kHz,
  S_STEP_100_0kHz,
} ScanStep;

typedef struct FreqPreset {
  char name[8]; // max 7 chars + null; fits all BK4819-receivable band names
  uint32_t fStart;
  uint32_t fEnd;
  StepsCount stepsCountIndex;
  ScanStep stepSizeIndex;
  ModulationMode_t modulationType;
  BK4819_FilterBandwidth_t listenBW;
} FreqPreset;

// Presets below 18 MHz omitted: BK4819 hardware minimum is 18 MHz.
static const FreqPreset freqPresets[] = {
    {"15mBC",  1890000,  1902000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"15mHam", 2100000,  2144990, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"13mBC",  2145000,  2185000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"12mHam", 2489000,  2499000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"11mBC",  2567000,  2610000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"CB",     2697500,  2799990, STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"10mHam", 2800000,  2970000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"6mHam",  5000000,  5400000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"AirBand",11800000, 13500000,STEPS_128, S_STEP_100_0kHz, MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"2mHam",  14400000, 14800000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"Railway",15175000, 15599990,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"Sea",    15600000, 16327500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"Satcom", 24300000, 27000000,STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"River1", 30001250, 30051250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"River2", 33601250, 33651250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"LPD",    43307500, 43477500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"PMR",    44600625, 44620000,STEPS_32,  S_STEP_6_25kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"FRS 462",46256250, 46272500,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"FRS 467",46756250, 46771250,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"LoRaWAN",86400000, 86900000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"GSM-UP", 89000000, 91500000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"GSM-DN", 93500000, 96000000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"23cmHam",124000000,130000000,STEPS_128,S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
};

typedef struct SpectrumSettings {
```

(Byte-for-byte the same table this repo had before the egzumer swap, just with `STEP_*` constants renamed to egzumer's equivalent `S_STEP_*` names — same numeric frequencies, same step/bandwidth/modulation per band.)

- [ ] **Step 2: Add ApplyPreset and AutomaticPresetChoose to app/spectrum.c**

Find:

```c
void APP_RunSpectrum() {
```

Replace with:

```c
static void ApplyPreset(FreqPreset p) {
  currentFreq = p.fStart;
  settings.scanStepIndex = p.stepSizeIndex;
  settings.listenBw = p.listenBW;
  settings.modulationType = p.modulationType;
  settings.stepsCount = p.stepsCountIndex;
  RADIO_SetModulation(settings.modulationType);
  RelaunchScan();
  ResetBlacklist();
  redrawScreen = true;
  settings.frequencyChangeStep = GetBW();
}

static void AutomaticPresetChoose(uint32_t f) {
  for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
    const FreqPreset *p = &freqPresets[i];
    if (f >= p->fStart && f <= p->fEnd) {
      ApplyPreset(*p);
    }
  }
}

void APP_RunSpectrum() {
```

- [ ] **Step 3: Call AutomaticPresetChoose from APP_RunSpectrum, only in the non-scan-range path**

Find:

```c
#ifdef ENABLE_SCAN_RANGES
  if(gScanRangeStart) {
    currentFreq = initialFreq = gScanRangeStart;
    for(uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++) {
      if(scanStepValues[i] >= gTxVfo->StepFrequency) {
        settings.scanStepIndex = i;
        break;
      }
    }
    settings.stepsCount = STEPS_128;
  }
  else
#endif
    currentFreq = initialFreq = gTxVfo->pRX->Frequency -
                                ((GetStepsCount() / 2) * GetScanStep());

  BackupRegisters();
```

Replace with:

```c
#ifdef ENABLE_SCAN_RANGES
  if(gScanRangeStart) {
    currentFreq = initialFreq = gScanRangeStart;
    for(uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++) {
      if(scanStepValues[i] >= gTxVfo->StepFrequency) {
        settings.scanStepIndex = i;
        break;
      }
    }
    settings.stepsCount = STEPS_128;
  }
  else
#endif
  {
    currentFreq = initialFreq = gTxVfo->pRX->Frequency -
                                ((GetStepsCount() / 2) * GetScanStep());
    AutomaticPresetChoose(currentFreq);
  }

  BackupRegisters();
```

(An explicit scan-range selection via `chFrScanner`/`ENABLE_SCAN_RANGES` is a deliberate, narrower user choice — automatic band-preset selection must not override it, hence the `else` scoping.)

- [ ] **Step 4: Build and verify**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

Expected: clean build, `text+data` under 61440 bytes. The `freqPresets[]` table is the same size as before the swap (23 entries × ~20 bytes), so expect flash usage to grow by roughly that much from Task 3's numbers — still comfortably under budget based on prior measurements in this plan.

- [ ] **Step 5: Sanity-check the host test suite still passes**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

This task doesn't touch anything the Task 3 tests directly exercise (`ApplyPreset`/`AutomaticPresetChoose` are only called from `APP_RunSpectrum`, which the host tests don't invoke — it has a real hardware `while` loop). Expected: still `PASSED (0 failures)`, unchanged from Task 3. If anything now fails, something in this task's changes had an unexpected effect on shared state (e.g. `settings` globals) — investigate rather than assume it's unrelated.

- [ ] **Step 6: Commit**

```bash
git add app/spectrum.h app/spectrum.c
git commit -m "$(cat <<'EOF'
Restore automatic band-preset selection on spectrum entry

egzumer's APP_RunSpectrum has no equivalent to the pre-swap
freqPresets[]/ApplyPreset()/AutomaticPresetChoose() -- it just centers
the scan window on the active VFO's frequency with a generic default
step size, losing the curated per-band window (step size, bandwidth,
modulation) the old code snapped to automatically. Restores the same
byte-for-byte preset table (STEP_* renamed to egzumer's S_STEP_*
equivalents) and wires AutomaticPresetChoose into the non-scan-range
entry path only -- an explicit ENABLE_SCAN_RANGES selection is a more
specific user choice and must not be overridden by it.

Note: KEY_3/KEY_9 remain egzumer's UpdateDBMax (dB-range adjustment,
from Task 1) -- this only restores automatic selection on entry, not
manual preset-switching via keypress, which egzumer's key layout has
no room for and which was not requested back.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Re-flash and confirm on real hardware

**Files:** none (verification only)

**Interfaces:**
- Consumes: Task 5 complete.

- [ ] **Step 1: Build the packed firmware**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

- [ ] **Step 2: Ask the user to put the radio in bootloader mode, then flash**

Same sequence as Task 4: power off → hold PTT → power on (white LED) → plug in cable. Confirm with the user before flashing. Check `python tools/k5flash.py --list-ports` for the current port (it has changed between sessions before — do not assume it's still the same COM port as last time).

```bash
cd "$(git rev-parse --show-toplevel)"
python tools/k5flash.py -p <PORT> firmware_uvk5_v1.packed.bin
```

- [ ] **Step 3: Ask the user to verify**

Specifically: does entering the spectrum screen on a frequency inside a known band (e.g. 2m ham, 144-148MHz) now automatically snap to that band's curated window (wide view, appropriate step size) instead of a narrow generic window centered on the exact current frequency? Confirm `*`/F, TX-from-STILL, and the other Task 4 checks are still fine (this task's changes are additive and shouldn't affect them, but confirm rather than assume).

- [ ] **Step 4: Clean up build artifacts**

```bash
cd "$(git rev-parse --show-toplevel)"
rm -f firmware_uvk5_v1 firmware_uvk5_v1.bin firmware_uvk5_v1.packed.bin
```

---

### Task 7: Fix preset matching against the wrong (already-centered) frequency

**Context (found during Task 6's hardware verification — band-preset selection still didn't work after Task 5):** `AutomaticPresetChoose(currentFreq)` in `APP_RunSpectrum` tests against `currentFreq`, which by the time it's called has already been shifted by `-(GetStepsCount()/2)*GetScanStep())` (~0.8MHz with default `STEPS_64`/`S_STEP_25_0kHz` settings) to center the initial scan window. The pre-swap code tested against the VFO's raw tuned frequency directly, with no centering offset applied first. Verified with real numbers: for the `2mHam` preset (144.400-148.000MHz), any VFO frequency in roughly 144.400-145.199MHz computes a centered test point below 144.400MHz — landing in the gap between presets, matching nothing, producing exactly the reported symptom ("some region" instead of the named band).

**Files:**
- Modify: `app/spectrum.c` (one-line fix to `APP_RunSpectrum`'s `AutomaticPresetChoose` call)
- Modify: `tools/host_tests/test_spectrum.c` (new regression test)

**Interfaces:**
- Consumes: Task 5 complete.
- Produces: nothing new; this is a targeted bug fix plus a test.

- [ ] **Step 1: Fix the call site**

Find:

```c
#ifdef ENABLE_SCAN_RANGES
  if (!gScanRangeStart)
#endif
    AutomaticPresetChoose(currentFreq);
```

Replace with:

```c
#ifdef ENABLE_SCAN_RANGES
  if (!gScanRangeStart)
#endif
    AutomaticPresetChoose(gTxVfo->pRX->Frequency);
```

(Match against the VFO's actual tuned frequency, not the already-centered scan-window midpoint. `ApplyPreset()` overwrites `currentFreq = p.fStart` on a match regardless, so this only changes what frequency is used for the matching test itself, not what happens once a preset is found.)

- [ ] **Step 2: Add a regression test proving this with real numbers**

In `tools/host_tests/test_spectrum.c`, add a new test that calls `AutomaticPresetChoose` directly (it's a `static` function in the `#include`d real `app/spectrum.c`, already callable from the test file the same way other static functions are) with a frequency in the range this bug affected, and confirms it now matches:

```c
static void test_automatic_preset_choose_matches_near_band_edge(void) {
    printf("\n-- test_automatic_preset_choose_matches_near_band_edge --\n");

    // 144.500MHz: inside 2mHam's range (144.400-148.000MHz), but before the
    // fix, AutomaticPresetChoose was being called with an already-centered
    // frequency (~0.8MHz below the true VFO frequency with default settings),
    // which lands BELOW 144.400MHz here -- no preset matched at all.
    settings.stepsCount = STEPS_64;
    settings.scanStepIndex = S_STEP_25_0kHz;
    settings.modulationType = MODULATION_FM;
    settings.listenBw = BK4819_FILTER_BW_WIDE;

    uint32_t vfo_freq = 14450000; // 144.50000 MHz, in this codebase's 10Hz units

    AutomaticPresetChoose(vfo_freq);

    CHECK(currentFreq == 14400000); // jumped to 2mHam's fStart
    CHECK(settings.scanStepIndex == S_STEP_25_0kHz);
    CHECK(settings.stepsCount == STEPS_128);
    CHECK(settings.modulationType == MODULATION_FM);
    CHECK(settings.listenBw == BK4819_FILTER_BW_WIDE);

    // The actual bug: calling it with the CENTERED value (what the old,
    // unfixed call site passed) must NOT match -- this is what "some region
    // instead of 2mHam" looked like on real hardware.
    uint32_t centered_test_point = vfo_freq - 80000; // matches the real offset computation
    currentFreq = 0; // sentinel so we can tell if ApplyPreset ran
    AutomaticPresetChoose(centered_test_point);
    CHECK(currentFreq == 0); // confirms the centered value indeed falls in the gap
}
```

Add the call to `main()` alongside the other tests.

- [ ] **Step 3: Build and verify**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

Expected: ARM build clean, under 61440 bytes. Host tests `PASSED (0 failures)` including the new test.

- [ ] **Step 4: Verify the test actually catches the regression**

Temporarily revert Step 1's fix in a scratch copy (or just change `gTxVfo->pRX->Frequency` back to `currentFreq` in the call site), re-run the host test suite, confirm the new test fails, then restore the fix. This is the same verification discipline used for every other test added in this plan — don't skip it just because the bug is now well-understood.

- [ ] **Step 5: Commit**

```bash
git add app/spectrum.c tools/host_tests/test_spectrum.c
git commit -m "$(cat <<'EOF'
Fix preset matching to use the VFO's actual frequency, not the centered scan window

AutomaticPresetChoose(currentFreq) was matching against an already-offset
value (currentFreq minus half the scan window width, ~0.8MHz with default
settings) instead of the VFO's actual tuned frequency. Any frequency in
roughly the lower 0.8MHz of a band's range computed a test point below
the band's start, landing in the gap between presets and matching
nothing -- confirmed on real hardware (reported as "some region" instead
of the expected band) and reproduced exactly with real numbers in the new
host test.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Re-flash and confirm the fix on real hardware

**Files:** none (verification only)

**Interfaces:**
- Consumes: Task 7 complete.

- [ ] **Step 1: Build the packed firmware**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
```

- [ ] **Step 2: Ask the user to put the radio in bootloader mode, then flash**

Check `python tools/k5flash.py --list-ports` for the current port first — it has changed between sessions in this project before.

```bash
cd "$(git rev-parse --show-toplevel)"
python tools/k5flash.py -p <PORT> firmware_uvk5_v1.packed.bin
```

- [ ] **Step 3: Ask the user to verify on the real device**

Specifically: tune the active VFO/channel to a frequency in the lower portion of a known band (e.g. 144.4-145.2MHz for 2m) — the case that was broken before this fix — and confirm entering the spectrum screen now correctly snaps to that band's curated window. Also spot-check a frequency well inside a band's middle (which likely already appeared to work before, since the bug was specifically edge-dependent) to make sure nothing regressed there.

- [ ] **Step 4: Clean up build artifacts**

```bash
cd "$(git rev-parse --show-toplevel)"
rm -f firmware_uvk5_v1 firmware_uvk5_v1.bin firmware_uvk5_v1.packed.bin
```

---

### Task 9: Build scripted-key-injection test infra; prove or disprove why Task 7's fix still fails on hardware

**Context (found after Task 8's hardware verification failed a third time):** Automatic band-preset selection still didn't work on real hardware after Task 7's fix, despite that fix being independently verified correct by code review and host tests twice. `APP_RunSpectrum()` has never been runnable end-to-end in the host test harness — it ends in a real `while (isInitialized) { Tick(); }` loop — so nothing has ever tested the actual entry path in full; only isolated pieces (`AutomaticPresetChoose`, `GetPresetMatchFrequency`) were ever tested directly, called manually with hand-picked arguments. This task closes that gap and investigates a specific, code-confirmed discrepancy between the host test build and the real firmware build (below), rather than proposing another unverified patch.

**Investigation finding (confirmed by reading the code, not yet by a test):** `tools/host_tests/build.sh`'s `CFLAGS` does **not** define `ENABLE_SCAN_RANGES`, while the real firmware's `Makefile` defaults it to `1` (`ENABLE_SCAN_RANGES ?= 1`, `Makefile:43`). `app/spectrum.c` has several `#ifdef ENABLE_SCAN_RANGES` blocks, including the exact guard around the call this project has been fixing:

```c
#ifdef ENABLE_SCAN_RANGES
  if (!gScanRangeStart)
#endif
    AutomaticPresetChoose(GetPresetMatchFrequency());
```

Without the flag, this compiles down to an unconditional call in the host test binary — meaning every previous host-test "pass" for Tasks 5 and 7 **never exercised this guard at all**, regardless of how correct the matching logic itself was. `gScanRangeStart`/`gScanRangeStop` (`app/chFrScanner.c`, toggled by `toggle_chan_scanlist()` in `app/main.c:58`, reset to 0 only in `app/app.c:1603` and `app/common.c:32`'s `COMMON_SwitchVFOs`) are exactly the kind of state that can persist across unrelated key presses in a real testing session — this task's tests directly confirm (or refute) whether that's what's happening, without assuming it.

**Files:**
- Modify: `tools/host_tests/build.sh` (add `-DENABLE_SCAN_RANGES` to `CFLAGS`, matching the real firmware default)
- Modify: `tools/host_tests/stubs.c` (define `gScanRangeStart`/`gScanRangeStop`, normally supplied by the not-compiled-for-host-tests `app/chFrScanner.c`; make `KEYBOARD_Poll` scriptable)
- Modify: `tools/host_tests/test_spectrum.c` (two new end-to-end tests that call the real `APP_RunSpectrum()`)

**Interfaces:**
- Consumes: Task 7 complete (this tests Task 7's fix end-to-end for the first time).
- Produces: nothing new for other tasks to consume — this task's output is test coverage plus a definitive finding about root cause, which determines what Task 10 needs to fix (if anything code-level needs fixing at all).

**This task does NOT fix anything.** Its sole job is to build the infra, add the two tests below exactly as specified, run them, and report the exact pass/fail result of each — including, if either test hangs instead of completing, precisely what state (`preventKeypress`, `currentState`, `scanInfo.i`/`.measurementsCount`, `kbd.current`/`.prev`/`.counter`) it was in when you had to interrupt it. Do not add a timeout/iteration cap inside `app/spectrum.c` to make a hang "go away" — a hang is itself a finding, not a bug to patch around, since the real hardware's own loop has no such cap either.

- [ ] **Step 1: Align the host test build flags with the real firmware's default config**

In `tools/host_tests/build.sh`, find:

```bash
CFLAGS="-I. -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -Wall -Wextra"
```

Replace with:

```bash
CFLAGS="-I. -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -Wall -Wextra"
```

- [ ] **Step 2: Add `gScanRangeStart`/`gScanRangeStop` definitions to stubs.c**

These are normally defined in `app/chFrScanner.c`, which is not compiled into the host test binary. Step 1's flag now pulls in `app/chFrScanner.h`'s `extern` declarations (via `app/spectrum.c`'s own `#ifdef ENABLE_SCAN_RANGES #include "chFrScanner.h"`), so the link will fail without a definition somewhere.

In `tools/host_tests/stubs.c`, add near the other globals (e.g. right after `gBatteryCheckCounter`):

```c
#ifdef ENABLE_SCAN_RANGES
uint32_t gScanRangeStart;
uint32_t gScanRangeStop;
#endif
```

- [ ] **Step 3: Make KEYBOARD_Poll scriptable**

Find:

```c
KEY_Code_t KEYBOARD_Poll(void) { return KEY_INVALID; }
```

Replace with:

```c
KEY_Code_t fake_next_key = KEY_INVALID;
KEY_Code_t KEYBOARD_Poll(void) { return fake_next_key; }
```

No existing test calls `HandleUserInput()`/`Tick()`/`APP_RunSpectrum()`, so this default (`KEY_INVALID`, same as before) doesn't change any current test's behavior.

- [ ] **Step 4: Build once to confirm Steps 1-3 link cleanly, before adding new tests**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

Expected: still `PASSED (0 failures)` — Steps 1-3 alone should not change any existing test's outcome (`ENABLE_SCAN_RANGES` only compiles in more self-contained code paths; nothing existing exercises them yet). If anything now fails to compile/link, that's a genuinely new missing symbol from turning the flag on — add a minimal stub for it in `stubs.c` following the existing pattern (no-op bodies for hardware writes, sensible fixed values for reads), then retry.

- [ ] **Step 5: Add the end-to-end test for the normal (no active scan range) path**

In `tools/host_tests/test_spectrum.c`, add `extern KEY_Code_t fake_next_key;` next to the existing `extern VFO_Info_t gVfoInfoStub;` near the top of the file.

Then add this test (place it after `test_get_preset_match_frequency_returns_raw_vfo_frequency`):

```c
// ---------------------------------------------------------------------
// End-to-end regression test: the REAL APP_RunSpectrum() -- not
// AutomaticPresetChoose() called directly -- must apply a matching
// preset on entry. Tasks 5 and 7 were both "verified clean" against
// AutomaticPresetChoose() in isolation, and still failed on real
// hardware three times; this closes the gap between "the matching
// logic is correct" and "the real entry path actually reaches it and
// nothing overwrites it afterward." Drives the real `while
// (isInitialized) { Tick(); }` loop with a scripted KEY_EXIT so it
// runs to completion and returns normally via the real
// DeInitSpectrum(), the same way it would on hardware.
// ---------------------------------------------------------------------
static void test_app_run_spectrum_applies_preset_end_to_end(void) {
    printf("\n-- test_app_run_spectrum_applies_preset_end_to_end --\n");

    kbd = (KeyboardState){KEY_INVALID, KEY_INVALID, 0};
    menuState = 0;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE; // scan always runs to completion
    gEeprom.TX_VFO = 0;

#ifdef ENABLE_SCAN_RANGES
    gScanRangeStart = 0;
    gScanRangeStop = 0;
#endif

    gVfoInfoStub.freq_config_RX.Frequency = 14450000; // 144.50000 MHz, inside 2mHam
    gVfoInfoStub.pRX = &gVfoInfoStub.freq_config_RX;
    gVfoInfoStub.Modulation = MODULATION_USB; // deliberately NOT 2mHam's FM, so a match is visible

    fake_next_key = KEY_EXIT;

    APP_RunSpectrum(); // real entry point, blocks until it exits via KEY_EXIT

    fake_next_key = KEY_INVALID;

    CHECK(currentFreq == 14400000); // 2mHam's fStart
    CHECK(settings.scanStepIndex == S_STEP_25_0kHz);
    CHECK(settings.stepsCount == STEPS_128);
    CHECK(settings.modulationType == MODULATION_FM); // overwritten by ApplyPreset, not left as USB
    CHECK(settings.listenBw == BK4819_FILTER_BW_WIDE);
}
```

- [ ] **Step 6: Add the end-to-end test for the ledger's gScanRangeStart hypothesis**

Add directly after the test from Step 5:

```c
#ifdef ENABLE_SCAN_RANGES
// ---------------------------------------------------------------------
// Regression test for the ledger's unconfirmed hypothesis: a nonzero
// gScanRangeStart left over from an earlier toggle_chan_scanlist()
// press (app/main.c) makes APP_RunSpectrum's `if (!gScanRangeStart)`
// guard skip AutomaticPresetChoose entirely, even when the VFO
// frequency sits inside a well-defined preset's range. This does NOT
// prove gScanRangeStart is actually nonzero on the real device -- only
// the code's behavior IF it were.
// ---------------------------------------------------------------------
static void test_app_run_spectrum_skips_preset_when_scan_range_active(void) {
    printf("\n-- test_app_run_spectrum_skips_preset_when_scan_range_active --\n");

    kbd = (KeyboardState){KEY_INVALID, KEY_INVALID, 0};
    menuState = 0;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    gEeprom.TX_VFO = 0;

    gScanRangeStart = 14500000; // 145.00000 MHz -- an active scan range
    gScanRangeStop  = 14550000; // 145.50000 MHz (0.5MHz wide)

    // VFO frequency is still inside 2mHam's range, same as the test above --
    // if the guard works as coded, this must make NO difference here.
    gVfoInfoStub.freq_config_RX.Frequency = 14450000;
    gVfoInfoStub.pRX = &gVfoInfoStub.freq_config_RX;
    gVfoInfoStub.Modulation = MODULATION_AM; // NOT 2mHam's FM
    gVfoInfoStub.StepFrequency = 1250; // picks S_STEP_12_5kHz below, NOT 2mHam's S_STEP_25_0kHz

    fake_next_key = KEY_EXIT;

    APP_RunSpectrum();

    fake_next_key = KEY_INVALID;
    gScanRangeStart = 0;
    gScanRangeStop = 0;

    CHECK(currentFreq == 14500000); // gScanRangeStart's own value, NOT 2mHam's fStart (14400000)
    CHECK(settings.scanStepIndex == S_STEP_12_5kHz); // the scan-range path's own step, NOT 2mHam's S_STEP_25_0kHz
    CHECK(settings.modulationType == MODULATION_AM); // NOT overwritten to 2mHam's FM -- confirms ApplyPreset never ran
}
#endif
```

Update `main()` to call both new tests, after the existing ones:

```c
    test_app_run_spectrum_applies_preset_end_to_end();
#ifdef ENABLE_SCAN_RANGES
    test_app_run_spectrum_skips_preset_when_scan_range_active();
#endif
```

- [ ] **Step 7: Build and run, with a wall-clock safety net while iterating**

The test binary itself has no way to interrupt a hung `APP_RunSpectrum()` call from the outside — it's a real, unbounded `while` loop. While iterating, wrap the run in an external timeout so a wrong assumption doesn't hang your shell indefinitely:

```bash
cd "$(git rev-parse --show-toplevel)"
timeout 60 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

If it times out, do not add a cap inside `app/spectrum.c` — instead add temporary `printf` diagnostics (`scanInfo.i`, `scanInfo.measurementsCount`, `preventKeypress`, `kbd.counter`) inside `Tick()` or the new test itself (e.g. a bounded diagnostic loop calling `Tick()` manually up to a few thousand times with prints every 100 iterations, in a scratch copy, to see exactly where it's stuck), figure out why it didn't terminate as expected, fix the test's setup to match reality, remove the diagnostics, and retry. Report exactly what you found either way.

Expected once both new tests are added and passing: `PASSED (0 failures)`, all existing tests plus these two.

- [ ] **Step 8: Report findings precisely**

In the report, state plainly:
1. Did `test_app_run_spectrum_applies_preset_end_to_end` pass?
2. Did `test_app_run_spectrum_skips_preset_when_scan_range_active` pass?
3. Given both results, does the code's real entry path (`APP_RunSpectrum`, exactly as committed after Task 7) correctly apply a matching preset when `gScanRangeStart == 0`, and correctly skip it when `gScanRangeStart != 0`? Answer this directly — this is the fact Task 10 will act on, not a guess.

- [ ] **Step 9: Commit**

```bash
git add tools/host_tests/
git commit -m "$(cat <<'EOF'
Add end-to-end test coverage for APP_RunSpectrum's real entry path

Tasks 5 and 7 both verified AutomaticPresetChoose()/GetPresetMatchFrequency()
correct in isolation, called directly with hand-picked arguments, and both
still failed on real hardware -- because nothing had ever run the real
APP_RunSpectrum() entry path end-to-end: it ends in a genuine
while(isInitialized) hardware loop, previously untestable on the host.

Adds scripted key injection (KEYBOARD_Poll is now a controllable global)
so a test can drive that real loop to completion via a scripted KEY_EXIT
and exit cleanly through the real DeInitSpectrum(), then two tests: one
confirming the real entry path applies a preset end-to-end, and one
testing the ledger's standing hypothesis that a nonzero gScanRangeStart
left over from toggle_chan_scanlist() (app/main.c) silently skips preset
selection via APP_RunSpectrum's `if (!gScanRangeStart)` guard.

Also fixes a build-config gap: tools/host_tests/build.sh never defined
ENABLE_SCAN_RANGES, while the real firmware's Makefile defaults it to 1
-- meaning every #ifdef ENABLE_SCAN_RANGES block in app/spectrum.c,
including this exact guard, was silently compiled OUT of every previous
host test run, regardless of how correct the code inside it was.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Diagnose gScanRangeStart on real hardware, then fix accordingly

**Context:** Task 9 proved `AutomaticPresetChoose`/`ApplyPreset`/`GetPresetMatchFrequency` and the `APP_RunSpectrum` call site correct end-to-end in host tests — the real entry path, driven through its genuine hardware loop, correctly applies a matching preset when `gScanRangeStart == 0` and correctly skips it when `gScanRangeStart != 0`. Task 9 made **no changes to `app/spectrum.c`**, so the firmware already flashed after Task 7/8 is unchanged and still valid for this check — no rebuild is needed just to test the hypothesis below.

The leading remaining explanation for three straight hardware failures despite three independently-verified-correct fixes: `gScanRangeStart` (`app/chFrScanner.c`) is genuinely stuck non-zero on the device. It's toggled by `toggle_chan_scanlist()`, bound to **F+7** by default in this build (`app/main.c:238`, since `ENABLE_VOX` defaults to `0`), and reset only by **F+2** (`COMMON_SwitchVFOs`, `app/main.c:176`/`app/common.c:32`) or a specific `app/app.c:1603` trigger. Spectrum entry itself is **F+5** (`app/main.c:217-219`). If F+7 was pressed at any point during this project's many hardware test sessions and F+2 (or the app.c:1603 trigger) never fired since, `gScanRangeStart` would still be stuck on today. `ui/main.c:365-369` displays a frequency-range readout on the **main VFO screen** whenever `gScanRangeStart` is nonzero — directly checkable by eye, no flashing required.

**This task is primarily an interactive, controller-led hardware session with the user — not a subagent-dispatchable implementation task.** The controller drives Steps 1-3 directly with the user in real time; only Step 4 (if code changes turn out to be needed at all) may warrant an implementer dispatch, and only after the branch it depends on is resolved live.

- [ ] **Step 1: Check the main VFO screen for an active scan range, before touching the spectrum screen**

Ask the user to power on the radio (no reflash needed) and look at the main VFO screen. Does it show a frequency-range readout (two frequencies, e.g. "144.xxx" / "148.xxx") instead of the normal single-frequency/channel display?

- [ ] **Step 2a: If a scan range IS showing — this was it**

Ask the user to press **F+2** (or press F+7 again to toggle it back off — either clears `gScanRangeStart`), confirm the main screen returns to normal, then enter the spectrum screen (F+5) on a frequency inside a known band (e.g. 144-148MHz) and confirm automatic preset selection now works. If it does: **no code change is needed** — this was stale device state, not a firmware bug. Skip to Step 5 (close out).

- [ ] **Step 2b: If no scan range is showing — the hypothesis is refuted**

`gScanRangeStart == 0`, matching the "should work" branch Task 9 proved correct — yet the user still needs to confirm current behavior on hardware (rebuild + flash HEAD first, since nothing has been flashed since Task 7/8 if the answer to Step 1 was "haven't flashed yet"). Re-run Task 4/6/8's verification steps (build, flash, check band-preset auto-selection on entry). If it now works: something about the device's prior state (not code) was the cause after all — close out via Step 5, note what changed. If it still fails with `gScanRangeStart` confirmed at 0: this is genuinely new information the host tests didn't anticipate — do not guess at another patch. Add temporary diagnostic output (same technique as Task 8's abandoned attempt, but informed this time by knowing the guard itself is not the problem) or walk the actual VFO/frequency values live with the user to find what really differs between the hardware and the host test's assumptions (e.g. `gTxVfo->pRX->Frequency` not being what's expected, `ENABLE_SCAN_RANGES` build flag mismatch between this repo's real Makefile and what was assumed, a stale/wrong firmware image). Report findings back before proposing any fix.

- [ ] **Step 3: If a code fix does turn out to be needed (only reachable via 2b's dead end)**

Write precise steps here once the real cause is known — do not pre-guess them now.

- [ ] **Step 4: Build, flash, and get the user's hardware confirmation** (standard pattern from Tasks 4/6/8: build via `uvk5-buildcheck`, confirm bootloader entry sequence with the user, check `python tools/k5flash.py --list-ports` for the current port, flash, ask the user to verify, clean up build artifacts).

- [ ] **Step 5: Close out** — update the ledger with the confirmed root cause (device state vs. code bug), and if no code changed, note that explicitly rather than leaving Task 10 looking like a no-op.

**Resolution:** confirmed device state, not a code bug. The radio was running Task 8's abandoned debug-code build (never rebuilt/reflashed after the source-level `git checkout` revert), explaining both the on-screen collision and the user's impression that the preset wasn't applying. Rebuilding the current clean commit and reflashing resolved it immediately — hardware now shows exactly the predicted 2mHam window (144.00000-147.20000MHz, 3200.00k span, FM, WIDE, 128×25.00kHz), matching Task 9's host-test predictions exactly. No `app/spectrum.c` change was needed.

---

### Task 11: Restore the matched preset's name on the SPECTRUM status line

**Context:** While confirming Task 10's fix on hardware, the user noted the status line has no indication of *which* band preset matched (expected e.g. `"2mHam"` in the top-left, where `dbMin`/`dbMax` is currently shown instead). Checked the pre-swap (fagci-based) `app/spectrum.c` (`git show 64f7cc8^:app/spectrum.c`, `DrawStatus()`): it computed this live on every render — scan `freqPresets[]` for whichever entry's `[fStart, fEnd)` contains the current display frequency, and draw `p->name` at the status line's `(0, 0)`. This was dropped entirely during Task 1's wholesale replacement of `spectrum.c` with egzumer's version (which has no preset concept at all) and never re-added when Task 5 restored `ApplyPreset`/`AutomaticPresetChoose` — Task 5 only restored the *selection* logic, not this *display* of what was selected. Not a bug — a feature gap nobody had explicitly scoped until now.

**Files:**
- Modify: `app/spectrum.c` (`DrawStatus()`)
- Modify: `tools/host_tests/test_spectrum.c` (new test)

**Interfaces:**
- Consumes: Task 10 complete (no dependency on Task 10's specific findings, just sequenced after).
- Produces: nothing new for other tasks.

- [ ] **Step 1: Add the live preset-name lookup and draw call to DrawStatus()**

Find (in `app/spectrum.c`):

```c
static void DrawStatus() {
#ifdef SPECTRUM_EXTRA_VALUES
  sprintf(String, "%d/%d P:%d T:%d", settings.dbMin, settings.dbMax,
          Rssi2DBm(peak.rssi), Rssi2DBm(settings.rssiTriggerLevel));
#else
  sprintf(String, "%d/%d", settings.dbMin, settings.dbMax);
#endif
  GUI_DisplaySmallest(String, 0, 1, true, true);
```

Replace with:

```c
static void DrawStatus() {
#ifdef SPECTRUM_EXTRA_VALUES
  sprintf(String, "%d/%d P:%d T:%d", settings.dbMin, settings.dbMax,
          Rssi2DBm(peak.rssi), Rssi2DBm(settings.rssiTriggerLevel));
#else
  sprintf(String, "%d/%d", settings.dbMin, settings.dbMax);
#endif
  GUI_DisplaySmallest(String, 0, 1, true, true);

  // Recomputed live on every render (not cached at preset-match time) so it
  // stays correct if the user manually retunes afterward -- same approach
  // as the pre-swap fagci-based DrawStatus() this restores (git show
  // 64f7cc8^:app/spectrum.c).
  const FreqPreset *matchedPreset = NULL;
  for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
    if (currentFreq >= freqPresets[i].fStart && currentFreq <= freqPresets[i].fEnd) {
      matchedPreset = &freqPresets[i];
      break;
    }
  }
  if (matchedPreset != NULL) {
    GUI_DisplaySmallest(matchedPreset->name, 48, 1, true, true);
  }
```

(Position `x=48` sits in the status line's unused middle ground: the `dbMin/dbMax` text at `x=0` is at most ~8 characters wide at 3px/char, well clear of 48; the battery icon starts at `x=116`. `freqPresets[].name` is capped at 7 chars + null, so even the longest name fits.)

- [ ] **Step 2: Add a regression test**

In `tools/host_tests/test_spectrum.c`, add:

```c
// ---------------------------------------------------------------------
// Regression test: DrawStatus() must show the matched preset's name on
// the status line, restoring behavior dropped during the egzumer swap
// (Task 1) and never re-added when preset selection itself was restored
// (Task 5). Checks gStatusLine directly rather than parsing glyphs --
// confirms *something* is drawn at the expected column when a preset
// matches, and nothing is when it doesn't.
// ---------------------------------------------------------------------
static void test_draw_status_shows_matched_preset_name(void) {
    printf("\n-- test_draw_status_shows_matched_preset_name --\n");

    memset(gStatusLine, 0, sizeof(gStatusLine));
    currentFreq = 14450000; // 144.50000 MHz, inside 2mHam (144.00000-148.00000)
    settings.dbMin = -130;
    settings.dbMax = -50;

    DrawStatus();

    int name_area_has_content = 0;
    for (int c = 48; c < 48 + 21; c++) { // ~7 chars * 3px
        if (gStatusLine[c] != 0) name_area_has_content = 1;
    }
    CHECK(name_area_has_content);

    memset(gStatusLine, 0, sizeof(gStatusLine));
    currentFreq = 20000000; // 200.00000 MHz, outside every preset's range
    DrawStatus();

    int name_area_has_content_no_match = 0;
    for (int c = 48; c < 48 + 21; c++) {
        if (gStatusLine[c] != 0) name_area_has_content_no_match = 1;
    }
    CHECK(!name_area_has_content_no_match);
}
```

Add the call to `main()`, after the existing tests.

- [ ] **Step 3: Build and verify**

```bash
cd "$(git rev-parse --show-toplevel)"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-buildcheck /bin/bash -c "cd /app && make clean && make -j4"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app uvk5-hosttest /bin/bash /app/tools/host_tests/build.sh
```

Expected: ARM build clean, `text+data` still under 61440 bytes. Host tests `PASSED (0 failures)` including the new test.

- [ ] **Step 4: Flash and confirm on hardware**

Build the packed firmware, confirm bootloader entry with the user, check `python tools/k5flash.py --list-ports` for the current port (it has changed between sessions in this project before — most recently to COM12, a CH340 device distinguishable from other enumerated ports by name), flash, and ask the user to verify: entering the spectrum screen on a frequency inside a known band now shows that band's name (e.g. `"2mHam"`) on the status line, without colliding with the `dbMin/dbMax` text or the battery icon. Clean up build artifacts afterward.

- [ ] **Step 5: Commit**

```bash
git add app/spectrum.c tools/host_tests/test_spectrum.c
git commit -m "$(cat <<'EOF'
Restore the matched preset's name on the SPECTRUM status line

Dropped during Task 1's wholesale replacement of spectrum.c with
egzumer's version (which has no preset concept), and never re-added
when Task 5 restored ApplyPreset/AutomaticPresetChoose -- that task
only restored preset *selection*, not this display of what was
selected. Restores the pre-swap fagci-based DrawStatus()'s approach
(git show 64f7cc8^:app/spectrum.c): scan freqPresets[] live against
the current display frequency on every render, so it stays correct
across manual retuning, not just at the moment a preset first matched.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

**Addendum (decided live during hardware verification, same session):** confirming the name label on hardware, the user noticed KEY_3/KEY_9 no longer switch bands (Task 1's egzumer swap had repurposed them to `UpdateDBMax`, adjusting `settings.dbMax`; Task 5 explicitly kept that repurposing). Asked the user directly whether they wanted the old manual band-switching back, giving up the dB-range adjustment — confirmed yes, and additionally: stop displaying `dbMin`/`dbMax` on the status line at all (freeing `x=0` for the preset name, moved there from `x=48` to match the pre-swap original's position more closely — also addresses the user's "not as in the original" observation about the name's position).

Implemented directly in this same task (not spun off separately, since it's tightly coupled to what Task 11 already touches): removed `UpdateDBMax()` entirely (both its `OnKeyDown`/SPECTRUM-state and `OnKeyDownStill` call sites — the STILL-screen binding didn't exist in the pre-swap code either, so it's dropped rather than repointed), added `SelectNearestPreset(bool inc)` (restored from `git show 64f7cc8^:app/spectrum.c`, adapted to use `currentFreq` directly since the current code has no `GetScreenF()` equivalent), wired it to `KEY_3`/`KEY_9` in `OnKeyDown`, and removed the `dbMin`/`dbMax` status-line readout (both the normal and `SPECTRUM_EXTRA_VALUES` debug-build variants). Host test `test_key3_key9_adjusts_db_range` replaced with `test_key3_key9_selects_nearest_preset`; `test_draw_status_shows_matched_preset_name` updated for the `x=0` position. Host tests and hardware both confirmed working.
