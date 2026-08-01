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
    uint8_t vfo = gEeprom.RX_VFO;

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
}

void ARDF_DFSimpleRestore(void)
{
    if ( gARDFDFSimpleBackup.valid == false )
    {
        return;
    }

    uint8_t vfo = gARDFDFSimpleBackup.vfo;

    gEeprom.SQUELCH_LEVEL                  = gARDFDFSimpleBackup.squelch;
    gEeprom.VfoInfo[vfo].Modulation        = gARDFDFSimpleBackup.modulation;
    gEeprom.VfoInfo[vfo].CHANNEL_BANDWIDTH = gARDFDFSimpleBackup.bandwidth;
    gEeprom.VfoInfo[vfo].STEP_SETTING      = gARDFDFSimpleBackup.step_setting;
    gARDFNumFoxes                          = gARDFDFSimpleBackup.num_foxes;
    gARDFGainRemember                      = gARDFDFSimpleBackup.gain_remember;
    gEeprom.DUAL_WATCH                     = gARDFDFSimpleBackup.dual_watch;
    gEeprom.CROSS_BAND_RX_TX               = gARDFDFSimpleBackup.cross_band;

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

    return raw;
}

void ARDF_DFSimpleBackupUnpack(uint32_t raw, t_ardf_df_simple_backup *backup)
{
    backup->valid         = (raw >> 0) & 0x01u;
    backup->vfo           = (raw >> 1) & 0x01u;
    backup->squelch       = (raw >> 2) & 0x0Fu;
    backup->modulation    = (ModulationMode_t)((raw >> 6) & 0x07u);
    backup->bandwidth     = (raw >> 9) & 0x03u;
    backup->step_setting  = (STEP_Setting_t)((raw >> 11) & 0x1Fu);
    backup->num_foxes     = (raw >> 16) & 0x0Fu;
    backup->gain_remember = (raw >> 20) & 0x03u;
    backup->dual_watch    = (raw >> 22) & 0x03u;
    backup->cross_band    = (raw >> 24) & 0x03u;
}

#endif
