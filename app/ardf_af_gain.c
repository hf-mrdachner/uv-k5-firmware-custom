// Pure math for the ARDF "AFGain" menu item -- see app/ardf_af_gain.h.
//
// No hardware dependencies -- deliberate, same reasoning as
// app/ardf_df_simple.c: lets tools/host_tests/test_ardf_af_gain.c compile
// and run this directly on a host compiler, no BK4819/EEPROM stubbing at all.

#ifdef ENABLE_ARDF

#include "app/ardf_af_gain.h"

uint8_t ARDF_ClampDacGain(uint8_t dac_gain_calibrated, int8_t offset)
{
   int16_t dac_gain = (int16_t)dac_gain_calibrated + offset;

   if ( dac_gain < 0 )
      dac_gain = 0;
   else if ( dac_gain > 15 )
      dac_gain = 15;

   return (uint8_t)dac_gain;
}

int8_t ARDF_AFGainOffsetMin(uint8_t dac_gain_calibrated)
{
   return (int8_t)(0 - (int16_t)dac_gain_calibrated);
}

int8_t ARDF_AFGainOffsetMax(uint8_t dac_gain_calibrated)
{
   return (int8_t)(15 - (int16_t)dac_gain_calibrated);
}

#endif
