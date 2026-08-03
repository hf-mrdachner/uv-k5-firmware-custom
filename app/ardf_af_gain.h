#ifndef APP_ARDF_AF_GAIN_H
#define APP_ARDF_AF_GAIN_H

#ifdef ENABLE_ARDF

#include <stdint.h>

// Pure math for the ARDF "AFGain" menu item's BK4819 AF DAC Gain offset.
// Split out from app/ardf.c so it has zero hardware/globals dependencies --
// see app/ardf_af_gain.c and tools/host_tests/test_ardf_af_gain.c.

// Clamp a calibrated DAC_GAIN (0..15) plus a signed offset into the valid
// 0..15 BK4819 register field range.
uint8_t ARDF_ClampDacGain(uint8_t dac_gain_calibrated, int8_t offset);

// The AFGain menu's usable offset range for a given calibrated DAC_GAIN.
// Always exactly 16 values wide (dac_gain_calibrated + offset must stay
// within 0..15), just shifted depending on calibration -- e.g. calibration 8
// (typical middle) gives -8..7, calibration 3 gives -3..12.
int8_t ARDF_AFGainOffsetMin(uint8_t dac_gain_calibrated);
int8_t ARDF_AFGainOffsetMax(uint8_t dac_gain_calibrated);

#endif

#endif
