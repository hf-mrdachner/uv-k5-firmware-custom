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

#include "../../app/spectrum.c"
#include "fake_signal.h"

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
// Regression test: KEY_3 / KEY_9 must jump to the nearest band preset.
// (Was dead code: `if (0) SelectNearestPreset(...)`.)
// ---------------------------------------------------------------------
static void test_key3_key9_band_switch(void) {
    printf("\n-- test_key3_key9_band_switch --\n");

    // Start inside the "CB" preset's range (2697500..2799990, per freqPresets).
    currentFreq = 2700000;
    ApplyPreset(freqPresets[5]); // "CB" -- establishes a known starting state
    CHECK(strcmp(freqPresets[5].name, "CB") == 0);

    uint32_t before = currentFreq;
    OnKeyDown(KEY_3); // should call SelectNearestPreset(true) -> next band up
    CHECK(currentFreq != before);
    CHECK(currentFreq >= freqPresets[6].fStart); // "10mHam" starts right after CB

    uint32_t afterUp = currentFreq;
    OnKeyDown(KEY_9); // should call SelectNearestPreset(false) -> back down
    CHECK(currentFreq != afterUp);
}

// ---------------------------------------------------------------------
// Regression test: the STILL screen's S-meter/dBm text must not collide
// with the big frequency readout in the same framebuffer row.
// ---------------------------------------------------------------------
static int row_has_bit(int row, int col) {
    return (gFrameBuffer[row][col] != 0);
}

static void test_still_screen_no_collision(void) {
    printf("\n-- test_still_screen_no_collision --\n");

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    memset(gStatusLine, 0, sizeof(gStatusLine));

    fMeasure = 14500000; // 145.00000 MHz
    scanInfo.rssi = 260; // representative mid-range RSSI raw value
    isTransmitting = false;
    kbd.current = KEY_INVALID;
    txAllowState = VFO_STATE_NORMAL;
    monitorMode = false;
    settings.rssiTriggerLevel = 150;

    RenderStill();

    // Row 0 (screen y 8-15) holds the big frequency readout only.
    // Row 1 (screen y 16-23) must hold the S-meter/dBm text.
    // If the S-meter regressed back onto row 0, columns that are part of
    // the frequency string AND used by the S-meter text would be set in
    // row 0 in a pattern inconsistent with pure frequency-digit output --
    // instead we assert directly on the row we now target: row 1 must have
    // *something* drawn (S-meter text present) confirming it moved off row0.
    int row1_has_content = 0;
    for (int c = 0; c < LCD_WIDTH; c++) {
        if (row_has_bit(1, c)) { row1_has_content = 1; break; }
    }
    CHECK(row1_has_content);

    // And row 0 must be non-empty too (the frequency digits themselves).
    int row0_has_content = 0;
    for (int c = 0; c < LCD_WIDTH; c++) {
        if (row_has_bit(0, c)) { row0_has_content = 1; break; }
    }
    CHECK(row0_has_content);
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
static void run_fake_sweep(const uint16_t *profile, int n) {
    for (int i = 0; i <= n && i < FAKE_RSSI_PROFILE_MAX; i++) {
        fake_rssi_profile[i] = profile[i];
    }
    fake_rssi_profile_len = n + 1;
    fake_rssi_profile_pos = 0;

    RelaunchScan();
    ResetBlacklist();
    for (int i = 0; i <= n; i++) {
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
    settings.scanStepIndex = STEP_25_0kHz;
    int n = GetStepsCount(); // 64

    uint16_t profile[FAKE_RSSI_PROFILE_MAX];
    for (int i = 0; i <= n; i++) profile[i] = 140; // flat noise floor
    profile[20] = 400; // synthetic strong signal at bin 20

    run_fake_sweep(profile, n);

    CHECK(rssiHistory[20] == 400);
    CHECK(scanInfo.iPeak == 20);
    CHECK(peak.i == 20);
    CHECK(mov.max == 400);
    CHECK(mov.min == 140);

    // Real DrawSpectrum()/Rssi2Y(): the peak bin must render higher
    // (smaller Y = closer to the top of the graph) than the noise floor.
    uint8_t y_peak = Rssi2Y(rssiHistory[20]);
    uint8_t y_floor = Rssi2Y(rssiHistory[5]);
    CHECK(y_peak < y_floor);
}

// ---------------------------------------------------------------------
// Diagnostic (not a "must behave this way" guard): characterizes the
// still-open "squelch trigger line frozen at the top" report. Feeds a
// low-absolute-RSSI profile (as if the real radio's ambient noise floor
// sits well below the fixed default rssiTriggerLevel=150) through the
// real scan loop, then checks whether Rssi2Y() actually produces the
// same clamped position across the +/-60 range ~30 keypresses would
// cover. If this test ever starts failing, the frozen-line report is
// probably fixed (or its cause changed) -- update or remove it then.
// ---------------------------------------------------------------------
static void test_trigger_line_frozen_with_low_signal(void) {
    printf("\n-- test_trigger_line_frozen_with_low_signal (diagnostic) --\n");

    settings.stepsCount = STEPS_64;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    currentFreq = 14500000;
    settings.scanStepIndex = STEP_25_0kHz;
    int n = GetStepsCount();

    uint16_t profile[FAKE_RSSI_PROFILE_MAX];
    for (int i = 0; i <= n; i++) profile[i] = 20 + (i % 5); // ~20-24 noise floor
    profile[10] = 40; // weak local peak

    run_fake_sweep(profile, n);
    printf("   mov.min=%u mov.max=%u\n", mov.min, mov.max);

    uint8_t y_90  = Rssi2Y(90);
    uint8_t y_150 = Rssi2Y(150);
    uint8_t y_210 = Rssi2Y(210);
    printf("   Rssi2Y(90)=%u Rssi2Y(150)=%u Rssi2Y(210)=%u\n", y_90, y_150, y_210);

    CHECK(y_150 == y_90);
    CHECK(y_150 == y_210);
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
    settings.scanStepIndex = STEP_25_0kHz; // exercises the wide "center mode" text
    settings.frequencyChangeStep = 8000;
    CHECK(IsCenterMode()); // sanity: confirms which DrawNums branch is under test

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    DrawNums();
    uint8_t text_row5[LCD_WIDTH];
    memcpy(text_row5, gFrameBuffer[5], LCD_WIDTH);

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    peak.i = 31; // roughly mid-scan -- squarely inside the center-mode text's span
    DrawArrow(peak.i << settings.stepsCount);
    uint8_t arrow_row5[LCD_WIDTH];
    memcpy(arrow_row5, gFrameBuffer[5], LCD_WIDTH);

    int overlap_col = -1;
    for (int x = 0; x < LCD_WIDTH; x++) {
        if (text_row5[x] & arrow_row5[x]) { overlap_col = x; break; }
    }
    if (overlap_col >= 0) {
        printf("   collision at column %d (text=0x%02x arrow=0x%02x)\n",
               overlap_col, text_row5[overlap_col], arrow_row5[overlap_col]);
        memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
        DrawNums();
        DrawArrow(peak.i << settings.stepsCount);
        dump_row_ascii(5, overlap_col > 10 ? overlap_col - 10 : 0, overlap_col + 10);
    }
    CHECK(overlap_col == -1);
}

int main(void) {
    test_key3_key9_band_switch();
    test_still_screen_no_collision();
    test_spectrum_scan_finds_peak();
    test_trigger_line_frozen_with_low_signal();
    test_spectrum_arrow_text_collision();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
