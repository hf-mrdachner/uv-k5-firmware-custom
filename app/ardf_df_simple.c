// ARDF "DF Simple" settings backup/restore.
//
// Entering DF-Simple mode forces several settings to fixed, beginner-friendly
// values (see app/menu.c's MENU_ARDF case). This module remembers whatever
// those settings were right before they got overridden, so leaving DF-Simple
// puts them back instead of permanently losing them.
//
// No hardware dependencies -- only reads/writes gEeprom and the two ARDF
// globals it needs. This is deliberate: it's what lets
// tools/host_tests/test_ardf_df_simple.c compile and run this file directly
// on a host compiler with no BK4819/audio/UI stubbing at all.

#ifdef ENABLE_ARDF

#include "app/ardf_df_simple.h"
#include "app/ardf.h"
#include "settings.h"

t_ardf_df_simple_backup gARDFDFSimpleBackup;

void ARDF_DFSimpleBackup(void)
{
    uint8_t vfo = gEeprom.TX_VFO; // the VFO the menu operates on (gTxVfo), which is what DF Simple overrides

    gARDFDFSimpleBackup.valid         = true;
    gARDFDFSimpleBackup.vfo           = vfo;
    gARDFDFSimpleBackup.squelch       = gEeprom.SQUELCH_LEVEL;
    gARDFDFSimpleBackup.modulation    = gEeprom.VfoInfo[vfo].Modulation;
    gARDFDFSimpleBackup.bandwidth     = gEeprom.VfoInfo[vfo].CHANNEL_BANDWIDTH;
    gARDFDFSimpleBackup.step_setting  = gEeprom.VfoInfo[vfo].STEP_SETTING;
    gARDFDFSimpleBackup.num_foxes     = gARDFNumFoxes;
    gARDFDFSimpleBackup.gain_remember = gARDFGainRemember;
    gARDFDFSimpleBackup.dual_watch    = gEeprom.DUAL_WATCH;
    gARDFDFSimpleBackup.cross_band    = gEeprom.CROSS_BAND_RX_TX;
    gARDFDFSimpleBackup.af_gain_offset = gARDFAFGainOffset;
}

void ARDF_DFSimpleRestore(void)
{
    if ( gARDFDFSimpleBackup.valid == false )
    {
        return;
    }

    uint8_t vfo = gARDFDFSimpleBackup.vfo;

    // NOTE: putting gARDFGainRemember back moves the live gain/mistune slot from
    // ardf_*[vfo][0] (used while gain remember is off) to ardf_*[vfo][gARDFActiveFox],
    // which needs the same hardware reconciliation MENU_ARDF_GAIN_REMEMBER performs
    // for its own off->on transition. That is done by the caller in app/menu.c's
    // MENU_ARDF case, not here: it needs ARDF_DoMistuneFreq()/ARDF_ActivateGainIndex(),
    // which touch the BK4819, and this module is deliberately hardware-free so
    // tools/host_tests/test_ardf_df_simple.c can compile it with no stubs at all.

    gEeprom.SQUELCH_LEVEL                  = gARDFDFSimpleBackup.squelch;
    gEeprom.VfoInfo[vfo].Modulation        = gARDFDFSimpleBackup.modulation;
    gEeprom.VfoInfo[vfo].CHANNEL_BANDWIDTH = gARDFDFSimpleBackup.bandwidth;
    gEeprom.VfoInfo[vfo].STEP_SETTING      = gARDFDFSimpleBackup.step_setting;
    gARDFNumFoxes                          = gARDFDFSimpleBackup.num_foxes;
    gARDFGainRemember                      = gARDFDFSimpleBackup.gain_remember;
    gEeprom.DUAL_WATCH                     = gARDFDFSimpleBackup.dual_watch;
    gEeprom.CROSS_BAND_RX_TX               = gARDFDFSimpleBackup.cross_band;
    gARDFAFGainOffset                      = gARDFDFSimpleBackup.af_gain_offset;

    gARDFDFSimpleBackup.valid = false;
}

uint32_t ARDF_DFSimpleBackupPack(const t_ardf_df_simple_backup *backup)
{
    uint32_t raw = 0;

    raw |= ((uint32_t)(backup->valid ? 1u : 0u) & 0x01u) << 0;
    raw |= ((uint32_t)backup->vfo               & 0x01u) << 1;
    raw |= ((uint32_t)backup->squelch           & 0x0Fu) << 2;
    raw |= ((uint32_t)backup->modulation        & 0x07u) << 6;
    raw |= ((uint32_t)backup->bandwidth         & 0x03u) << 9;
    raw |= ((uint32_t)backup->step_setting      & 0x1Fu) << 11;
    raw |= ((uint32_t)backup->num_foxes         & 0x0Fu) << 16;
    raw |= ((uint32_t)backup->gain_remember     & 0x03u) << 20;
    raw |= ((uint32_t)backup->dual_watch        & 0x03u) << 22;
    raw |= ((uint32_t)backup->cross_band        & 0x03u) << 24;
    raw |= ((uint32_t)backup->af_gain_offset    & 0x1Fu) << 26; // 5-bit two's complement, -16..15

    return raw;
}

void ARDF_DFSimpleBackupUnpack(uint32_t raw, t_ardf_df_simple_backup *backup)
{
    // Some bit fields are wider than the value range they carry (they are
    // sized for the packed layout, not for the enum), so a corrupt or foreign
    // EEPROM can hand us values that would later be written straight into live
    // VFO_Info_t fields and used as array indices. Clamp out-of-range values to
    // a safe default instead of rejecting them -- same convention RADIO_ConfigureChannel()
    // already uses for the very same fields read out of a channel's EEPROM row.

    backup->valid         = (raw >> 0) & 0x01u;
    backup->vfo           = (raw >> 1) & 0x01u;

    backup->squelch       = (raw >> 2) & 0x0Fu;
    if ( backup->squelch > 9 ) // MENU_GetLimits(MENU_SQL, ...)
        backup->squelch = 0;

    backup->modulation    = (ModulationMode_t)((raw >> 6) & 0x07u);
    if ( backup->modulation >= MODULATION_UKNOWN )
        backup->modulation = MODULATION_FM;

    backup->bandwidth     = (raw >> 9) & 0x03u; // 2 bits, all 4 BANDWIDTH_* values are valid

    backup->step_setting  = (STEP_Setting_t)((raw >> 11) & 0x1Fu);
    if ( backup->step_setting >= STEP_N_ELEM )
        backup->step_setting = STEP_12_5kHz;

    backup->num_foxes     = (raw >> 16) & 0x0Fu;
    if ( backup->num_foxes > ARDF_NUM_FOX_MAX ) // gARDFActiveFox indexes ardf_gain_index[][ARDF_NUM_FOX_MAX]
        backup->num_foxes = ARDF_DEFAULT_NUM_FOXES;

    backup->gain_remember = (raw >> 20) & 0x03u; // 2 bits, OFF/VFO A/VFO B/BOTH -- all valid

    backup->dual_watch    = (raw >> 22) & 0x03u;
    if ( backup->dual_watch > DUAL_WATCH_CHAN_B )
        backup->dual_watch = DUAL_WATCH_OFF;

    backup->cross_band    = (raw >> 24) & 0x03u;
    if ( backup->cross_band > CROSS_BAND_CHAN_B )
        backup->cross_band = CROSS_BAND_OFF;

    // 5-bit two's complement -> int8_t, covering -16..15. Wide enough for any
    // offset ARDF_AFGainOffsetMin()/Max() can produce for any calibrated
    // DAC_GAIN (0..15) -- that range is always exactly 16 values wide, just
    // shifted per calibration -- so every bit pattern decodes to a plain
    // valid int8_t, no clamp needed (same reasoning as bandwidth/gain_remember
    // above). ARDF_ApplyAFGain()'s own clamp is what keeps the *hardware*
    // register safe regardless of what value ends up in gARDFAFGainOffset.
    // Firmware predating this field always wrote 0 into these bits (pack()
    // zero-initializes `raw` and only old fields set their own bits), so
    // upgrading from older firmware decodes this as offset 0, matching
    // ARDF_DEFAULT_AF_GAIN_OFFSET.
    uint8_t af_gain_raw    = (raw >> 26) & 0x1Fu;
    backup->af_gain_offset = (af_gain_raw >= 16) ? (int8_t)(af_gain_raw - 32) : (int8_t)af_gain_raw;
}

#endif
