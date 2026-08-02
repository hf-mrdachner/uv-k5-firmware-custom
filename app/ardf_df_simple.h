#ifndef APP_ARDF_DF_SIMPLE_H
#define APP_ARDF_DF_SIMPLE_H

#ifdef ENABLE_ARDF

#include <stdint.h>
#include <stdbool.h>
#include "radio.h"
#include "frequencies.h"

// Snapshot of the settings ARDF "DF Simple" mode overrides, captured right
// before it applies its own fixed values, and written back when DF Simple is
// switched off again -- see app/ardf_df_simple.c.
typedef struct
{
    bool             valid;         // false: no backup captured (or already restored)
    uint8_t          vfo;           // which VFO (0/1) was overridden
    uint8_t          squelch;       // gEeprom.SQUELCH_LEVEL
    ModulationMode_t modulation;    // gEeprom.VfoInfo[vfo].Modulation
    uint8_t          bandwidth;     // gEeprom.VfoInfo[vfo].CHANNEL_BANDWIDTH
    STEP_Setting_t   step_setting;  // gEeprom.VfoInfo[vfo].STEP_SETTING
    uint8_t          num_foxes;     // gARDFNumFoxes
    uint8_t          gain_remember; // gARDFGainRemember
    uint8_t          dual_watch;    // gEeprom.DUAL_WATCH
    uint8_t          cross_band;    // gEeprom.CROSS_BAND_RX_TX
} t_ardf_df_simple_backup;

extern t_ardf_df_simple_backup gARDFDFSimpleBackup;

// Snapshot the 8 fields above off the VFO the menu operates on
// (gEeprom.TX_VFO, i.e. gTxVfo -- the one DF Simple overrides) into
// gARDFDFSimpleBackup and mark it valid. Call right before applying
// DF-Simple's own forced values.
void ARDF_DFSimpleBackup(void);

// If gARDFDFSimpleBackup is valid, write its 8 fields back onto the VFO
// index it recorded (not necessarily the currently active VFO) and mark the
// backup invalid. No-op if there is no valid backup.
void ARDF_DFSimpleRestore(void);

// Pack/unpack gARDFDFSimpleBackup-shaped data to/from a single uint32_t for
// EEPROM storage (settings.c reuses 4 previously-unused padding bytes in the
// ARDF EEPROM block for this). Pure bit arithmetic, no globals touched.
uint32_t ARDF_DFSimpleBackupPack(const t_ardf_df_simple_backup *backup);
void     ARDF_DFSimpleBackupUnpack(uint32_t raw, t_ardf_df_simple_backup *backup);

#endif

#endif
