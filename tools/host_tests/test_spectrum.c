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
// Regression test: KEY_3 / KEY_9 must adjust the dB display range
// (settings.dbMax), via UpdateDBMax(). Band presets no longer exist in
// egzumer's implementation -- this replaces the old band-switch test.
// ---------------------------------------------------------------------
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
static void run_fake_sweep(const uint16_t *profile, uint16_t n) {
    for (uint16_t i = 0; i <= n && i < FAKE_RSSI_PROFILE_MAX; i++) {
        fake_rssi_profile[i] = profile[i];
    }
    fake_rssi_profile_len = n + 1;
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

int main(void) {
    test_key3_key9_adjusts_db_range();
    test_still_screen_no_collision();
    test_spectrum_scan_finds_peak();
    test_spectrum_arrow_text_collision();
    test_automatic_preset_choose_matches_near_band_edge();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
