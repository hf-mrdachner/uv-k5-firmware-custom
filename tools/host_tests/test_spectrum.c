// Host-side integration tests for the spectrum analyzer (app/spectrum.c).
//
// This pulls in the REAL production source file (not a reimplementation) so
// that static functions and the actual font/draw code are under test. Only
// the hardware-touching leaf functions (BK4819_*, GPIO_*, KEYBOARD_*, etc.)
// are stubbed in stubs.c -- everything else, including the pixel buffers and
// the fonts, is the genuine firmware code.
//
// Build/run: see build.sh in this directory.

#include <assert.h>
#include <stdio.h>
#include <string.h>

// GPIOA/B/C (bsp/dp32g030/gpio.h) are hardcoded MCU peripheral addresses
// (e.g. GPIOC = 0x40061000). egzumer's ToggleRX() unconditionally touches
// these via audio.h's inline AUDIO_AudioPathOn/Off (GPIO_SetBit/ClearBit on
// &GPIOC->DATA) -- a real hardware write that segfaults when this code runs
// as a host process, since that address isn't mapped memory here. Redirect
// the macros to host-process memory before spectrum.c (and the headers it
// pulls in, transitively) use them: the same "leaf hardware dependency"
// stubbing this file already does for BK4819_*/KEYBOARD_* etc., just done
// at the macro level since GPIOA/B/C are compile-time pointer constants
// rather than function calls we could override in stubs.c.
#include "../../bsp/dp32g030/gpio.h"
static volatile GPIO_Bank_t fake_gpio_banks[3];
#undef GPIOA
#undef GPIOB
#undef GPIOC
#define GPIOA (&fake_gpio_banks[0])
#define GPIOB (&fake_gpio_banks[1])
#define GPIOC (&fake_gpio_banks[2])

#include "../../app/spectrum.c"
#include "fake_signal.h"

// gVfoInfoStub is defined in stubs.c (the real VFO_Info_t storage that
// gTxVfo/gRxVfo/gCurrentVfo all point at by default). Not declared in any
// production header since real firmware doesn't have a "the" VFO stub --
// only this test needs direct access to it, to control gTxVfo->pRX->Frequency.
extern VFO_Info_t gVfoInfoStub;
extern KEY_Code_t fake_next_key;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } else { \
        printf("ok   %s\n", #cond); \
    } \
} while (0)

// ---------------------------------------------------------------------
// Regression test: KEY_3 / KEY_9 must select the next/previous preset
// via SelectNearestPreset(), restoring the pre-swap fagci-based
// behavior (git show 64f7cc8^:app/spectrum.c). Superseded by user
// request: KEY_3/KEY_9 previously adjusted the dB display range via
// UpdateDBMax() (see Task 5's plan notes), but the user decided to give
// that up in favor of restoring manual band switching, and to stop
// displaying dbMin/dbMax on the status line entirely -- UpdateDBMax()
// and its display are both removed now.
// ---------------------------------------------------------------------
static void test_key3_key9_selects_nearest_preset(void) {
    printf("\n-- test_key3_key9_selects_nearest_preset --\n");

    // Between 13mBC (ends 2185000) and 12mHam (starts 2489000).
    currentFreq = 2300000;

    OnKeyDown(KEY_3); // SelectNearestPreset(true) -- next preset above
    CHECK(currentFreq == 2489000); // 12mHam's fStart
    CHECK(settings.scanStepIndex == S_STEP_1_0kHz);
    CHECK(settings.modulationType == MODULATION_USB);
    CHECK(settings.listenBw == BK4819_FILTER_BW_NARROWER);

    OnKeyDown(KEY_9); // SelectNearestPreset(false) -- previous preset below
    CHECK(currentFreq == 2145000); // 13mBC's fStart
    CHECK(settings.scanStepIndex == S_STEP_5_0kHz);
    CHECK(settings.modulationType == MODULATION_AM);
    CHECK(settings.listenBw == BK4819_FILTER_BW_NARROW);
}

// ---------------------------------------------------------------------
// Regression test: the STILL screen's S-meter/dBm text must not collide
// with the big frequency readout in the same framebuffer row.
// ---------------------------------------------------------------------
static void test_still_screen_no_collision(void) {
    printf("\n-- test_still_screen_no_collision --\n");

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    memset(gStatusLine, 0, sizeof(gStatusLine));

    fMeasure = 14500000; // 145.00000 MHz
    scanInfo.rssi = 400;
    isTransmitting = false;
    kbd.current = KEY_INVALID;
    txAllowState = VFO_STATE_NORMAL;
    monitorMode = false;
    settings.rssiTriggerLevel = 350;
    settings.dbMin = -130;
    settings.dbMax = -50;

    RenderStill();

    // Row 0 (screen y 8-15) holds the big frequency readout (DrawF, via
    // UI_PrintStringSmallNormal). The S-meter/dBm text is drawn at
    // GUI_DisplaySmallest(..., y=25, ...) -> gFrameBuffer[25/8] = row 3.
    // They are 3 rows apart, so a simple "both rows have content" check
    // confirms no collision.
    int row0_has_content = 0, row3_has_content = 0;
    for (int c = 0; c < LCD_WIDTH; c++) {
        if (gFrameBuffer[0][c] != 0) row0_has_content = 1;
        if (gFrameBuffer[3][c] != 0) row3_has_content = 1;
    }
    CHECK(row0_has_content); // big frequency line
    CHECK(row3_has_content); // S-meter/dBm text (y=25 -> row 3)
}

// ---------------------------------------------------------------------
// Fake-signal harness: drives the real scan loop (Scan/NextScanStep/
// MoveHistory/UpdatePeakInfo, via the real UpdateScan()) against a
// scripted RSSI-vs-frequency-bin profile instead of real hardware.
//
// `profile` must have n+1 entries: the scan loop measures one extra bin
// beyond the intended window before it notices the sweep is done (a
// pre-existing off-by-one in UpdateScan()'s completion check, not
// something these tests are trying to fix), so profile[n] is measured too.
// ---------------------------------------------------------------------
// NOTE (found while doing Task 9's ENABLE_SCAN_RANGES build-flag alignment,
// confirmed via temporary diagnostics, not app/spectrum.c changes): with
// ENABLE_SCAN_RANGES compiled in -- as it now is, to match the real
// firmware's default, see build.sh -- IsBlacklisted() has a real,
// pre-existing bug in app/spectrum.c: a freshly-reset blacklistFreqs[] is
// all zero, and IsBlacklisted(0) treats a zero *entry* (an empty slot) as
// equal to frequency-bin *index* 0, so it spuriously reports bin 0 as
// blacklisted. Scan() therefore silently never measures bin 0 of any fresh
// sweep (confirmed: IsBlacklisted(0)==true, fake_rssi_profile_pos stays at
// 0 through i=0, so bin 1 consumes profile position 0, not bin 0). This is
// a genuine firmware bug, unrelated to the preset-selection guard this
// project has been chasing, and out of scope to fix here -- see
// task-9-report.md. Bin 0's own entry in `profile` is therefore unused;
// bins 1..n each consume profile[i], same as before.
static void run_fake_sweep(const uint16_t *profile, uint16_t n) {
    for (uint16_t i = 0; i < n && i < FAKE_RSSI_PROFILE_MAX; i++) {
        fake_rssi_profile[i] = profile[i + 1];
    }
    fake_rssi_profile_len = n;
    fake_rssi_profile_pos = 0;

    RelaunchScan();
    ResetBlacklist();
    for (uint16_t i = 0; i <= n; i++) {
        UpdateScan();
    }
}

// ---------------------------------------------------------------------
// Feed a synthetic signal (flat noise floor + one sharp peak) through the
// real scan loop and confirm peak detection and rendering respond to it.
// ---------------------------------------------------------------------
static void test_spectrum_scan_finds_peak(void) {
    printf("\n-- test_spectrum_scan_finds_peak --\n");

    settings.stepsCount = STEPS_64;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE; // disable auto-RX side effects
    currentFreq = 14500000; // 145.00000 MHz
    settings.scanStepIndex = S_STEP_25_0kHz;
    uint16_t n = GetStepsCount(); // 64

    uint16_t profile[FAKE_RSSI_PROFILE_MAX];
    for (int i = 0; i <= n; i++) profile[i] = 300; // flat noise floor
    profile[20] = 500; // synthetic strong signal at bin 20

    run_fake_sweep(profile, n);

    CHECK(rssiHistory[20] == 500);
    CHECK(scanInfo.iPeak == 20);
    CHECK(peak.i == 20);

    // Real DrawSpectrum()/Rssi2Y(): the peak bin must render higher
    // (smaller Y = closer to the top of the graph) than the noise floor.
    uint8_t y_peak = Rssi2Y(rssiHistory[20]);
    uint8_t y_floor = Rssi2Y(rssiHistory[5]);
    CHECK(y_peak < y_floor);
}

// ---------------------------------------------------------------------
// Reusable fixture: dump a framebuffer row as ASCII, one line per bit
// (screen y = 8 + row*8 + bit), for visually inspecting collisions between
// draw calls that share a row. Not a test by itself -- use it from a
// diagnostic test, print the output, then decide what to assert.
// ---------------------------------------------------------------------
static void dump_row_ascii(int row, int x0, int x1) {
    for (int bit = 0; bit < 8; bit++) {
        printf("  y=%2d ", 8 + row * 8 + bit);
        for (int x = x0; x < x1; x++) {
            putchar((gFrameBuffer[row][x] & (1 << bit)) ? '#' : '.');
        }
        putchar('\n');
    }
}

// ---------------------------------------------------------------------
// Regression test: the SPECTRUM screen's frequency-range labels (DrawNums,
// row 5, y49-53) and the peak-position arrow (DrawArrow, row 5, bits3-6 /
// y51-54) both live in row 5 and can visually merge whenever the peak's x
// position falls within the text's column range -- reported as the
// frequency text appearing to collide with "the spectrum" (inherited from
// the egzumer/fagci spectrum merge, not introduced by later changes here).
//
// Isolates each element in its own clean buffer (rather than parsing a
// combined render) so the check is exactly "do these two elements' pixels
// ever share a column+bit", independent of exact glyph shapes.
// ---------------------------------------------------------------------
static void test_spectrum_arrow_text_collision(void) {
    printf("\n-- test_spectrum_arrow_text_collision --\n");

    settings.stepsCount = STEPS_64;
    currentFreq = 14500000;
    // egzumer's IsCenterMode() is `scanStepIndex < S_STEP_2_5kHz` -- true only
    // for the finest step sizes (0.01k..1.0k), unlike the old STEP_Setting_t
    // ordering where STEP_25_0kHz landed below the old threshold. Use
    // S_STEP_1_0kHz so the sanity check below actually holds.
    settings.scanStepIndex = S_STEP_1_0kHz; // exercises the "center mode" text
    settings.frequencyChangeStep = 8000;
    CHECK(IsCenterMode()); // sanity: confirms which DrawNums branch is under test

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    DrawNums();
    uint8_t text_row5[LCD_WIDTH], text_row6[LCD_WIDTH];
    memcpy(text_row5, gFrameBuffer[5], LCD_WIDTH);
    memcpy(text_row6, gFrameBuffer[6], LCD_WIDTH);

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    peak.i = 31; // roughly mid-scan -- squarely inside the center-mode text's span
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
    if (overlap_col >= 0) {
        printf("   collision at column %d (text=0x%02x arrow=0x%02x)\n",
               overlap_col, text_row5[overlap_col], arrow_row5[overlap_col]);
        memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
        DrawNums();
        DrawArrow(128u * peak.i / GetStepsCount());
        dump_row_ascii(5, overlap_col > 10 ? overlap_col - 10 : 0, overlap_col + 10);
    }
    CHECK(overlap_col == -1);
}

// ---------------------------------------------------------------------
// Regression test: AutomaticPresetChoose() must be called with the VFO's
// actual tuned frequency, not the already-centered scan-window midpoint
// (currentFreq minus half the scan window width) computed just above its
// call site in APP_RunSpectrum. Confirmed on real hardware: band-preset
// selection silently failed for any frequency in roughly the lower 0.8MHz
// of a band's range, because the centered test point fell below the
// band's fStart -- landing in the gap between presets and matching
// nothing.
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Regression test: GetPresetMatchFrequency() -- the exact expression the
// real APP_RunSpectrum() call site passes into AutomaticPresetChoose() --
// must return the VFO's raw, un-centered RX frequency (gTxVfo->pRX->Frequency)
// and nothing else. The previous test above proves the MATCHING LOGIC is
// correct in isolation, but it hand-computes its "centered" test value
// independently of the real call site; it would not have caught the actual
// bug if the call site's expression had been wrong in some other way. This
// test ties the check directly to the real data source (the gTxVfo VFO
// stub), closing that gap.
// ---------------------------------------------------------------------
static void test_get_preset_match_frequency_returns_raw_vfo_frequency(void) {
    printf("\n-- test_get_preset_match_frequency_returns_raw_vfo_frequency --\n");

    uint32_t vfo_freq = 14450000; // 144.50000 MHz
    gVfoInfoStub.freq_config_RX.Frequency = vfo_freq;
    gVfoInfoStub.pRX = &gVfoInfoStub.freq_config_RX;

    CHECK(GetPresetMatchFrequency() == vfo_freq);

    // And, tying it back to the matching logic: feeding the real call
    // site's actual data source through the real call site's actual
    // expression must select 2mHam's preset, not fall in the gap.
    settings.stepsCount = STEPS_64;
    settings.scanStepIndex = S_STEP_25_0kHz;
    settings.modulationType = MODULATION_FM;
    settings.listenBw = BK4819_FILTER_BW_WIDE;
    currentFreq = 0; // sentinel

    AutomaticPresetChoose(GetPresetMatchFrequency());

    CHECK(currentFreq == 14400000); // jumped to 2mHam's fStart
}

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

// ---------------------------------------------------------------------
// Regression test: a wide ENABLE_SCAN_RANGES scan (measurementsCount >
// 128, e.g. a several-MHz range at a fine step) must measure every step
// across the FULL range, not silently stop advancing after step 127.
// A prior fix for a real rssiHistory[] out-of-bounds read (confirmed via
// ASan) initially over-corrected by skipping measurement entirely once
// scanInfo.i reached 128, which is wrong: SetRssiHistory() already
// compresses indices > 128 down into the 128-entry rssiHistory[] safely,
// so those steps must still be measured via Scan() -> Measure() ->
// BK4819_GetRSSI(), just without Scan()'s own direct array read.
//
// Expects fake_rssi_profile_pos == n - 1, not n+1: UpdateScan()'s own
// completion call (scanInfo.i == measurementsCount) never measures (see
// its own comment above Scan()), accounting for one side of the gap, and
// bin 0 of every fresh sweep is never measured either -- a separate,
// pre-existing IsBlacklisted() bug (see run_fake_sweep's comment above):
// blacklistFreqs[] is all zero right after ResetBlacklist(), so
// IsBlacklisted(0) spuriously matches slot 0's zero *entry* against
// frequency-bin *index* 0. That second bug is orthogonal to the one
// under test here (it swallows exactly one bin, regardless of range
// width) and out of scope to fix in this change; confirmed empirically
// (fake_rssi_profile_pos == 399 for n == 400) rather than assumed.
// ---------------------------------------------------------------------
static void test_wide_scan_range_measures_past_128_steps(void) {
    printf("\n-- test_wide_scan_range_measures_past_128_steps --\n");

    gScanRangeStart = 10000000; // 100.00000 MHz
    gScanRangeStop  = 10500000; // 105.00000 MHz (5 MHz wide)
    gVfoInfoStub.StepFrequency = 1250; // picks S_STEP_12_5kHz below

    // Mirrors what APP_RunSpectrum() does when gScanRangeStart is active:
    // pick the scan step from gTxVfo->StepFrequency, force STEPS_128 (this
    // setting is unused for the actual measurement count when a scan
    // range is active -- see GetStepsCount()'s ENABLE_SCAN_RANGES branch).
    for (uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++) {
        if (scanStepValues[i] >= gVfoInfoStub.StepFrequency) {
            settings.scanStepIndex = i;
            break;
        }
    }
    settings.stepsCount = STEPS_128;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE; // never divert into UpdateListening()

    CHECK(GetScanStep() == 1250); // S_STEP_12_5kHz
    uint16_t expectedSteps = (gScanRangeStop - gScanRangeStart) / GetScanStep();
    CHECK(expectedSteps == 400); // 5,000,000 / 12,500 (10Hz units)

    uint16_t n = expectedSteps;
    for (int i = 0; i <= n && i < FAKE_RSSI_PROFILE_MAX; i++) fake_rssi_profile[i] = 300;
    fake_rssi_profile_len = n + 1;
    fake_rssi_profile_pos = 0;

    RelaunchScan();
    ResetBlacklist();
    CHECK(scanInfo.measurementsCount == expectedSteps);

    for (uint16_t i = 0; i <= n; i++) {
        UpdateScan();
    }

    // The regression: a scan that silently stopped measuring at step 128
    // would leave fake_rssi_profile_pos stuck around 128, never reaching
    // the full sweep's real BK4819_GetRSSI() call count. (n, not n+1: see
    // the bin-0/IsBlacklisted note above.)
    CHECK(fake_rssi_profile_pos == n - 1);

    gScanRangeStart = 0;
    gScanRangeStop = 0;
}
#endif

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
    for (int c = 0; c < 21; c++) { // ~7 chars * 3px
        if (gStatusLine[c] != 0) name_area_has_content = 1;
    }
    CHECK(name_area_has_content);

    memset(gStatusLine, 0, sizeof(gStatusLine));
    currentFreq = 20000000; // 200.00000 MHz, outside every preset's range
    DrawStatus();

    int name_area_has_content_no_match = 0;
    for (int c = 0; c < 21; c++) {
        if (gStatusLine[c] != 0) name_area_has_content_no_match = 1;
    }
    CHECK(!name_area_has_content_no_match);
}

static void test_freq_preset_name_buffer_size(void) {
    printf("\n-- test_freq_preset_name_buffer_size --\n");
    CHECK(sizeof(freqPresets[0].name) == 12);
}

int main(void) {
    test_key3_key9_selects_nearest_preset();
    test_still_screen_no_collision();
    test_spectrum_scan_finds_peak();
    test_spectrum_arrow_text_collision();
    test_automatic_preset_choose_matches_near_band_edge();
    test_get_preset_match_frequency_returns_raw_vfo_frequency();
    test_app_run_spectrum_applies_preset_end_to_end();
#ifdef ENABLE_SCAN_RANGES
    test_app_run_spectrum_skips_preset_when_scan_range_active();
    test_wide_scan_range_measures_past_128_steps();
#endif
    test_draw_status_shows_matched_preset_name();
    test_freq_preset_name_buffer_size();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
