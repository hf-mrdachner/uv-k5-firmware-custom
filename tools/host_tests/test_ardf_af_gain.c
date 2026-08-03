// Host-side unit tests for the ARDF "AFGain" pure math (app/ardf_af_gain.c).
//
// This module has zero hardware/globals dependencies by design, so it's
// pulled in directly (unity-build style, same approach as
// test_ardf_df_simple.c) with no stubs required at all.
//
// Build/run: see build_ardf_af_gain.sh in this directory.

#include <stdio.h>

#define ENABLE_ARDF

#include "../../app/ardf_af_gain.c"

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } else { \
        printf("ok   %s\n", #cond); \
    } \
} while (0)

// In-range base+offset must pass through unchanged.
static void test_clamp_dac_gain_in_range(void)
{
    CHECK(ARDF_ClampDacGain(8, 0)  == 8);
    CHECK(ARDF_ClampDacGain(8, 7)  == 15);
    CHECK(ARDF_ClampDacGain(8, -8) == 0);
    CHECK(ARDF_ClampDacGain(0, 15) == 15);
    CHECK(ARDF_ClampDacGain(15, -15) == 0);
}

// base+offset below 0 must clamp to 0, not wrap or go negative.
static void test_clamp_dac_gain_below_zero(void)
{
    CHECK(ARDF_ClampDacGain(0, -1)   == 0);
    CHECK(ARDF_ClampDacGain(3, -100) == 0);
}

// base+offset above 15 must clamp to 15, not overflow the 4-bit register field.
static void test_clamp_dac_gain_above_max(void)
{
    CHECK(ARDF_ClampDacGain(15, 1)   == 15);
    CHECK(ARDF_ClampDacGain(12, 100) == 15);
}

// The offset range must always be exactly 16 values wide (dac_gain_calibrated
// + offset has to stay within 0..15), whatever the calibration is, just
// shifted -- this is the invariant the whole "reproducible loudness" feature
// relies on: no menu step should ever land on a value that clamps silently.
static void test_offset_range_always_16_wide_and_correctly_shifted(void)
{
    CHECK(ARDF_AFGainOffsetMin(8)  == -8);
    CHECK(ARDF_AFGainOffsetMax(8)  == 7);

    CHECK(ARDF_AFGainOffsetMin(0)  == 0);
    CHECK(ARDF_AFGainOffsetMax(0)  == 15);

    CHECK(ARDF_AFGainOffsetMin(15) == -15);
    CHECK(ARDF_AFGainOffsetMax(15) == 0);

    for (int dac_gain = 0; dac_gain <= 15; dac_gain++)
    {
        int width = ARDF_AFGainOffsetMax((uint8_t)dac_gain) - ARDF_AFGainOffsetMin((uint8_t)dac_gain);
        CHECK(width == 15);

        // the range's own boundaries must never actually clamp when fed back in
        CHECK(ARDF_ClampDacGain((uint8_t)dac_gain, ARDF_AFGainOffsetMin((uint8_t)dac_gain)) == 0);
        CHECK(ARDF_ClampDacGain((uint8_t)dac_gain, ARDF_AFGainOffsetMax((uint8_t)dac_gain)) == 15);
    }
}

int main(void)
{
    test_clamp_dac_gain_in_range();
    test_clamp_dac_gain_below_zero();
    test_clamp_dac_gain_above_max();
    test_offset_range_always_16_wide_and_correctly_shifted();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
