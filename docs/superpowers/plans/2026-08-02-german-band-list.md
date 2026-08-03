# German band list Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the spectrum analyzer's band-preset table (`app/spectrum.h`'s `freqPresets[]`) correct for Germany, opt-in via two independent build flags, with the existing (international) table staying the unchanged default.

**Architecture:** Two new Makefile flags, `ENABLE_DE_HAM_BANDS` (amateur bands) and `ENABLE_DE_EXTRA_BANDS` (CB/PMR446/LPD433/Flugfunk/Seefunk/LoRaWAN), both off by default. `freqPresets[]` stays a single array definition; each entry that differs between flag states is wrapped in its own `#if defined(...)` / `#else` / `#endif` picking that one entry's name/values, so with both flags off the compiled table is byte-for-byte what it is today.

**Tech Stack:** C (ARM Cortex-M0 firmware + host-side test harness compiled with native gcc), Make.

## Global Constraints

- Frequencies are in the project's 10 Hz units (`value = Hz / 10`; e.g. 145.000 MHz = `14500000`) — every literal in this plan is already given in these units.
- Both new flags default to `0` (off). With both off, `freqPresets[]` must be byte-for-byte identical to its content before this feature (verified by the existing host test suite passing unchanged).
- `FreqPreset.name` grows from `char[8]` to `char[12]` — this is shared by both flag states, done once, independent of either flag.
- Exactly one ordering exception exists in the table: the LPD/LPD433 entry must appear *before* the 70cmHam entry in source order (both match on "first entry in array order whose range contains the frequency" — see Task 2's code and its comment for why).
- Host-test CFLAGS (`tools/host_tests/build.sh` and the new `build_band_list_de.sh`) must mirror whatever `-D` flags the real Makefile gains here, per this repo's own established rule (see `build.sh`'s header comment) — drift between the two has caused real bugs before in this project.
- Do not modify the content of any existing test function in `tools/host_tests/test_spectrum.c` — they were checked against the new table's values during planning and all remain valid unchanged (none reference a frequency or band name that this feature alters or removes).

---

### Task 1: Widen `FreqPreset.name` to `char[12]`

**Files:**
- Modify: `app/spectrum.h:126` (the `FreqPreset` struct's `name` field)
- Test: `tools/host_tests/test_spectrum.c`

**Interfaces:**
- Produces: `FreqPreset.name` is now `char[12]` (11 usable chars + null), used by Task 2's new table entries (e.g. `"Flugfunk"`, `"70cmHam"` — both under 12 chars).

- [ ] **Step 1: Change the struct field**

In `app/spectrum.h`, find:
```c
typedef struct FreqPreset {
  char name[8]; // max 7 chars + null; fits all BK4819-receivable band names
```
Replace with:
```c
typedef struct FreqPreset {
  char name[12]; // max 11 chars + null
```

- [ ] **Step 2: Add a regression test locking in the new size**

In `tools/host_tests/test_spectrum.c`, add this test function directly above `int main(void)`:
```c
static void test_freq_preset_name_buffer_size(void) {
    printf("\n-- test_freq_preset_name_buffer_size --\n");
    CHECK(sizeof(freqPresets[0].name) == 12);
}
```
Add the call in `main()`, right before the final `printf("\n%s ...")` line:
```c
    test_freq_preset_name_buffer_size();
```

- [ ] **Step 3: Build and run the default host tests**

Run (from repo root, inside the `uvk5-hosttest` Docker image — see `tools/host_tests/build.sh`'s header comment for the exact `docker build`/`docker run` invocation):
```
./tools/host_tests/build.sh
```
Expected: `PASSED (0 failures)` — this must include the new `test_freq_preset_name_buffer_size` check alongside every pre-existing test.

- [ ] **Step 4: Commit**

```bash
git add app/spectrum.h tools/host_tests/test_spectrum.c
git commit -m "Widen FreqPreset.name to char[12] for longer band labels"
```

---

### Task 2: Add the two build flags and the per-entry band table

**Files:**
- Modify: `Makefile` (flag declarations near line 43, CFLAGS block near line 383)
- Modify: `app/spectrum.h:136-160` (the `freqPresets[]` array)

**Interfaces:**
- Consumes: `FreqPreset.name` is `char[12]` (Task 1).
- Produces: `ENABLE_DE_HAM_BANDS` and `ENABLE_DE_EXTRA_BANDS` as Makefile-controlled `-D` flags; `freqPresets[]` with entries gated by them, consumed by Task 3's tests and by the existing `AutomaticPresetChoose()`/`SelectNearestPreset()`/`DrawStatus()` (all unchanged, `app/spectrum.c:777-1457`).

- [ ] **Step 1: Declare the two flags in the Makefile**

In `Makefile`, find:
```make
ENABLE_SCAN_RANGES            ?= 1
ENABLE_PREVENT_TX             ?= 0
ENABLE_ARDF                   ?= 1
```
Replace with:
```make
ENABLE_SCAN_RANGES            ?= 1
ENABLE_PREVENT_TX             ?= 0
ENABLE_ARDF                   ?= 1
ENABLE_DE_HAM_BANDS           ?= 0
ENABLE_DE_EXTRA_BANDS         ?= 0
```

- [ ] **Step 2: Wire the flags into CFLAGS**

In `Makefile`, find:
```make
ifeq ($(ENABLE_SCAN_RANGES),1)
	CFLAGS  += -DENABLE_SCAN_RANGES
endif
```
Replace with:
```make
ifeq ($(ENABLE_SCAN_RANGES),1)
	CFLAGS  += -DENABLE_SCAN_RANGES
endif
ifeq ($(ENABLE_DE_HAM_BANDS),1)
	CFLAGS  += -DENABLE_DE_HAM_BANDS
endif
ifeq ($(ENABLE_DE_EXTRA_BANDS),1)
	CFLAGS  += -DENABLE_DE_EXTRA_BANDS
endif
```

- [ ] **Step 3: Replace the `freqPresets[]` array**

In `app/spectrum.h`, replace the entire existing block (from the `// Presets below 18 MHz omitted...` comment through the closing `};` of `freqPresets`) with:

```c
// Presets below 18 MHz omitted: BK4819 hardware minimum is 18 MHz.
//
// ENABLE_DE_HAM_BANDS corrects the amateur-radio entries to the German
// allocations (adds 17m/70cm, narrows 6m/2m to their German width) -- these
// are bands a licensed operator may transmit on with this radio.
//
// ENABLE_DE_EXTRA_BANDS corrects/replaces the remaining entries (CB, PMR,
// LPD, AirBand, Sea/River, LoRaWAN) and drops Railway/Satcom/River1/River2/
// FRS/GSM (found factually wrong for Germany, or not practically useful --
// see docs/superpowers/specs/2026-08-02-german-band-list-design.md). These
// stay receive-only reference bands regardless of the flag: the UV-K5 isn't
// a certified device for PMR446/CB even though those bands are license-free.
//
// Both flags default off; with both off this table is unchanged from
// before either flag existed.
//
// ORDERING NOTE: LPD/LPD433 is placed before 70cmHam even though its
// fStart (43307500) is higher than 70cmHam's (43000000) -- LPD sits
// entirely inside the German 70cm ham band, and AutomaticPresetChoose()/
// DrawStatus() match on "first array entry whose range contains the
// frequency" (app/spectrum.c:777-783, 1449-1457), so LPD must come first
// for a frequency in its slice to be labeled "LPD"/"LPD433" rather than
// "70cmHam". This is the only place in the table where source order isn't
// purely ascending by frequency -- SelectNearestPreset()'s KEY_3/KEY_9
// next/prev navigation will, right at this one boundary, jump to LPD's
// start (433.075 MHz) before reaching 70cmHam's actual start (430.000 MHz)
// coming from below. Accepted trade-off; covered by Task 3's tests.
static const FreqPreset freqPresets[] = {
#ifdef ENABLE_DE_HAM_BANDS
    {"17mHam",  1806800,  1816800, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#endif
    {"15mBC",   1890000,  1902000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"15mHam",  2100000,  2144990, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"13mBC",   2145000,  2185000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"12mHam",  2489000,  2499000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"11mBC",   2567000,  2610000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
#ifdef ENABLE_DE_EXTRA_BANDS
    {"CB",      2656500,  2740500, STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_NARROW},
#else
    {"CB",      2697500,  2799990, STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
    {"10mHam",  2800000,  2970000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#ifdef ENABLE_DE_HAM_BANDS
    {"6mHam",   5000000,  5200000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#else
    {"6mHam",   5000000,  5400000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"Flugfunk",11797500, 13700000,STEPS_128, S_STEP_100_0kHz, MODULATION_AM, BK4819_FILTER_BW_NARROW},
#else
    {"AirBand", 11800000, 13500000,STEPS_128, S_STEP_100_0kHz, MODULATION_AM, BK4819_FILTER_BW_NARROW},
#endif
#ifdef ENABLE_DE_HAM_BANDS
    {"2mHam",   14400000, 14600000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"2mHam",   14400000, 14800000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"Railway", 15175000, 15599990,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"Seefunk", 15600000, 16202500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"Sea",     15600000, 16327500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"Satcom",  24300000, 27000000,STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"River1",  30001250, 30051250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"River2",  33601250, 33651250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"LPD433",  43307500, 43477500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"LPD",     43307500, 43477500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_HAM_BANDS
    {"70cmHam", 43000000, 44000000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"PMR446",  44600000, 44620000,STEPS_32,  S_STEP_6_25kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#else
    {"PMR",     44600625, 44620000,STEPS_32,  S_STEP_6_25kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"FRS 462", 46256250, 46272500,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"FRS 467", 46756250, 46771250,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
    {"LoRaWAN", 86400000, 86900000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
#ifndef ENABLE_DE_EXTRA_BANDS
    {"GSM-UP",  89000000, 91500000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"GSM-DN",  93500000, 96000000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
    {"23cmHam",124000000,130000000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
};
```

- [ ] **Step 4: Confirm the default build is untouched**

Run:
```
./tools/host_tests/build.sh
```
Expected: `PASSED (0 failures)`, identical to Task 1's run (both new flags are off by default, so this compiles the exact same table as before).

- [ ] **Step 5: Sanity-compile each flag combination (no new assertions yet — that's Task 3)**

Run these three ad hoc compiles from repo root to confirm the `#if`/`#else`/`#endif` structure is syntactically valid in every combination (Task 3 adds the real test coverage; this step just catches a stray `#endif` or duplicate name early):
```
gcc -I. -I external/CMSIS_5/CMSIS/Core/Include -I external/CMSIS_5/Device/ARM/ARMCM0/Include -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -DSPECTRUM_AUTOMATIC_SQUELCH -DENABLE_AM_FIX -DENABLE_DE_HAM_BANDS -fsyntax-only -Wall -Wextra tools/host_tests/test_spectrum.c
gcc -I. -I external/CMSIS_5/CMSIS/Core/Include -I external/CMSIS_5/Device/ARM/ARMCM0/Include -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -DSPECTRUM_AUTOMATIC_SQUELCH -DENABLE_AM_FIX -DENABLE_DE_EXTRA_BANDS -fsyntax-only -Wall -Wextra tools/host_tests/test_spectrum.c
gcc -I. -I external/CMSIS_5/CMSIS/Core/Include -I external/CMSIS_5/Device/ARM/ARMCM0/Include -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -DSPECTRUM_AUTOMATIC_SQUELCH -DENABLE_AM_FIX -DENABLE_DE_HAM_BANDS -DENABLE_DE_EXTRA_BANDS -fsyntax-only -Wall -Wextra tools/host_tests/test_spectrum.c
```
Expected: no errors from any of the three (warnings about the test file's own content are fine; this step only guards against a preprocessor/array-syntax mistake in Task 2's edit).

- [ ] **Step 6: Commit**

```bash
git add Makefile app/spectrum.h
git commit -m "Add ENABLE_DE_HAM_BANDS/ENABLE_DE_EXTRA_BANDS band-list flags"
```

---

### Task 3: Host test coverage for both flags

**Files:**
- Modify: `tools/host_tests/test_spectrum.c`
- Create: `tools/host_tests/build_band_list_de.sh`

**Interfaces:**
- Consumes: `freqPresets[]` and its `#ifdef`-gated entries (Task 2); `ARRAY_SIZE` macro (already available, used elsewhere in this file, e.g. `test_wide_scan_range_measures_past_128_steps`).
- Produces: `FindMatchingPresetName(uint32_t f)` — a small test-only helper, not part of production code, usable by any future test in this file that needs "what preset would the status line show at this frequency" without going through `DrawStatus()`'s battery-stub dependency.

- [ ] **Step 1: Add the matching helper and the two new test functions**

In `tools/host_tests/test_spectrum.c`, add this directly above `int main(void)`:

```c
// ---------------------------------------------------------------------
// German band list (ENABLE_DE_HAM_BANDS / ENABLE_DE_EXTRA_BANDS) tests.
// Only compiled in when the relevant flag is defined -- these assert on
// names/ranges that only exist under that flag, so they must not run
// against the default (both-off) table.
// ---------------------------------------------------------------------
#if defined(ENABLE_DE_HAM_BANDS) || defined(ENABLE_DE_EXTRA_BANDS)
// Mirrors DrawStatus()'s matching loop (app/spectrum.c:777-783) without
// its battery-stub dependency: first array entry whose range contains f.
static const char *FindMatchingPresetName(uint32_t f) {
    for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
        if (f >= freqPresets[i].fStart && f <= freqPresets[i].fEnd) {
            return freqPresets[i].name;
        }
    }
    return NULL;
}
#endif

#ifdef ENABLE_DE_HAM_BANDS
static void test_de_ham_bands(void) {
    printf("\n-- test_de_ham_bands --\n");

    // 17mHam: new entry, absent from the international table.
    CHECK(strcmp(FindMatchingPresetName(1810000), "17mHam") == 0); // 18.10 MHz

    // 70cmHam: new entry, the original point of this feature.
    CHECK(strcmp(FindMatchingPresetName(43550000), "70cmHam") == 0); // 435.50 MHz, outside LPD433's slice

    // 2mHam narrowed to the German 144-146 MHz allocation (was 144-148).
    CHECK(FindMatchingPresetName(14700000) == NULL); // 147.0 MHz, inside the old int'l range, outside the new German one

    // 6mHam narrowed to the German 50-52 MHz allocation (was 50-54).
    CHECK(FindMatchingPresetName(5300000) == NULL); // 53.0 MHz, same story

    // LPD/LPD433 must win over 70cmHam inside LPD's slice (documented
    // ordering exception -- see Task 2's array comment).
#ifdef ENABLE_DE_EXTRA_BANDS
    CHECK(strcmp(FindMatchingPresetName(43350000), "LPD433") == 0); // 433.50 MHz
#else
    CHECK(strcmp(FindMatchingPresetName(43350000), "LPD") == 0);
#endif
}
#endif

#ifdef ENABLE_DE_EXTRA_BANDS
static void test_de_extra_bands(void) {
    printf("\n-- test_de_extra_bands --\n");

    // CB widened to the full German 80-channel allocation (26.565-27.405 MHz).
    CHECK(strcmp(FindMatchingPresetName(2660000), "CB") == 0); // 26.60 MHz -- inside new CB, outside old (26.975-28.00)

    // AirBand renamed "Flugfunk" and widened to the ICAO edges (117.975-137.000 MHz).
    CHECK(strcmp(FindMatchingPresetName(11798000), "Flugfunk") == 0); // 117.98 MHz -- inside new, outside old (118.00-135.00)

    // Sea/River1/River2 consolidated into one "Seefunk" entry.
    CHECK(strcmp(FindMatchingPresetName(15700000), "Seefunk") == 0); // 157.0 MHz

    // PMR renamed "PMR446".
    CHECK(strcmp(FindMatchingPresetName(44610000), "PMR446") == 0); // 446.10 MHz

    // Railway, Satcom, River1, River2, FRS 462/467, GSM-UP/DN dropped entirely.
    CHECK(FindMatchingPresetName(15300000) == NULL); // 153.0 MHz, old Railway range
    CHECK(FindMatchingPresetName(25000000) == NULL); // 250.0 MHz, old Satcom range
    CHECK(FindMatchingPresetName(30020000) == NULL); // 300.20 MHz, old River1 range
    CHECK(FindMatchingPresetName(33620000) == NULL); // 336.20 MHz, old River2 range
    CHECK(FindMatchingPresetName(46260000) == NULL); // 462.60 MHz, old FRS 462 range
    CHECK(FindMatchingPresetName(90000000) == NULL); // 900.0 MHz, old GSM-UP range
}
#endif
```

- [ ] **Step 2: Wire the new tests into `main()`**

In `tools/host_tests/test_spectrum.c`, find:
```c
    test_freq_preset_name_buffer_size();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
```
Replace with:
```c
    test_freq_preset_name_buffer_size();
#ifdef ENABLE_DE_HAM_BANDS
    test_de_ham_bands();
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    test_de_extra_bands();
#endif

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
```

- [ ] **Step 3: Add the German-band-list build script**

Create `tools/host_tests/build_band_list_de.sh`:
```sh
#!/bin/sh
# Build and run tools/host_tests/test_spectrum.c against the two German
# band-list flags (ENABLE_DE_HAM_BANDS, ENABLE_DE_EXTRA_BANDS), independently
# and combined -- build.sh only covers the default (both flags off) config.
#
# Keep the base CFLAGS in sync with build.sh's (and, per build.sh's own
# comment, with the real Makefile's spectrum-related -D flags) -- this is
# the same class of drift that caused three rounds of "clean in host tests,
# broken on hardware" earlier in this project.
#
#   docker run --rm -v "$PWD":/app uvk5-hosttest /app/tools/host_tests/build_band_list_de.sh

set -e
cd "$(dirname "$0")/../.."   # repo root

BASE_CFLAGS="-I. -DPRINTF_INCLUDE_CONFIG_H -DENABLE_SPECTRUM -DENABLE_SCAN_RANGES -DSPECTRUM_AUTOMATIC_SQUELCH -DENABLE_AM_FIX -Wall -Wextra -fsanitize=address -g"

build_and_run() {
    NAME="$1"
    EXTRA_CFLAGS="$2"
    OUT="/tmp/uvk5_host_tests_$NAME"
    mkdir -p "$OUT"
    CFLAGS="$BASE_CFLAGS $EXTRA_CFLAGS"

    gcc $CFLAGS -c font.c -o "$OUT/font.o"
    gcc $CFLAGS -c external/printf/printf.c -o "$OUT/printf.o"
    gcc $CFLAGS -c ui/helper.c -o "$OUT/helper.o"
    gcc $CFLAGS -c frequencies.c -o "$OUT/frequencies.o"
    gcc $CFLAGS -c misc.c -o "$OUT/misc.o"
    gcc $CFLAGS -c tools/host_tests/stubs.c -o "$OUT/stubs.o"
    gcc $CFLAGS \
        -I external/CMSIS_5/CMSIS/Core/Include \
        -I external/CMSIS_5/Device/ARM/ARMCM0/Include \
        -c tools/host_tests/test_spectrum.c -o "$OUT/test_spectrum.o"

    gcc -fsanitize=address -g "$OUT/test_spectrum.o" "$OUT/font.o" \
        "$OUT/printf.o" "$OUT/helper.o" "$OUT/frequencies.o" "$OUT/misc.o" "$OUT/stubs.o" \
        -o "$OUT/test_spectrum"

    echo "=== $NAME ==="
    "$OUT/test_spectrum"
}

build_and_run de_ham "-DENABLE_DE_HAM_BANDS"
build_and_run de_extra "-DENABLE_DE_EXTRA_BANDS"
build_and_run de_ham_and_extra "-DENABLE_DE_HAM_BANDS -DENABLE_DE_EXTRA_BANDS"
```

Make it executable:
```bash
chmod +x tools/host_tests/build_band_list_de.sh
```

- [ ] **Step 4: Run it and confirm all three configurations pass**

Run:
```
./tools/host_tests/build_band_list_de.sh
```
Expected: three `=== ... ===` sections (`de_ham`, `de_extra`, `de_ham_and_extra`), each ending in `PASSED (0 failures)`. Every pre-existing test runs in all three (they were confirmed compatible with every flag combination during planning), plus `test_de_ham_bands()` in the two configs that define `ENABLE_DE_HAM_BANDS`, plus `test_de_extra_bands()` in the two that define `ENABLE_DE_EXTRA_BANDS`.

- [ ] **Step 5: Re-run the default build once more**

Run:
```
./tools/host_tests/build.sh
```
Expected: `PASSED (0 failures)` — confirms Task 3's changes to `test_spectrum.c` didn't disturb the default (both-flags-off) path.

- [ ] **Step 6: Commit**

```bash
git add tools/host_tests/test_spectrum.c tools/host_tests/build_band_list_de.sh
git commit -m "Add host test coverage for ENABLE_DE_HAM_BANDS/ENABLE_DE_EXTRA_BANDS"
```
