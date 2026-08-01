// Host-side unit tests for ARDF "DF Simple" backup/restore (app/ardf_df_simple.c).
//
// This pulls in the REAL production source file directly (unity-build style,
// same approach as test_spectrum.c) since app/ardf_df_simple.c has zero
// hardware dependencies of its own -- it only reads/writes gEeprom and the
// two ARDF globals it needs, both defined directly below. No stubs.c /
// BK4819 / audio / UI stubbing required.
//
// Build/run: see build_ardf_df_simple.sh in this directory.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ENABLE_ARDF

#include "../../app/ardf_df_simple.c"

// gEeprom and the two ARDF globals app/ardf_df_simple.c reads/writes --
// normally defined in settings.c / app/ardf.c, which we deliberately don't
// pull in here (they carry BK4819/UI dependencies this test doesn't need).
EEPROM_Config_t gEeprom;
uint8_t gARDFNumFoxes;
uint8_t gARDFGainRemember;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } else { \
        printf("ok   %s\n", #cond); \
    } \
} while (0)

// Pack/unpack must round-trip every field at its maximum in-range value
// without bit overlap or truncation.
static void test_pack_unpack_round_trip_max_values(void)
{
    t_ardf_df_simple_backup in = {
        .valid         = true,
        .vfo           = 1,
        .squelch       = 9,
        .modulation    = MODULATION_USB,
        .bandwidth     = BANDWIDTH_U1K7,
        .step_setting  = STEP_500_0kHz, // last entry in STEP_Setting_t
        .num_foxes     = 10,
        .gain_remember = 3,
        .dual_watch    = DUAL_WATCH_CHAN_B,
        .cross_band    = CROSS_BAND_CHAN_B,
    };

    uint32_t raw = ARDF_DFSimpleBackupPack(&in);

    t_ardf_df_simple_backup out;
    ARDF_DFSimpleBackupUnpack(raw, &out);

    CHECK(out.valid         == in.valid);
    CHECK(out.vfo           == in.vfo);
    CHECK(out.squelch       == in.squelch);
    CHECK(out.modulation    == in.modulation);
    CHECK(out.bandwidth     == in.bandwidth);
    CHECK(out.step_setting  == in.step_setting);
    CHECK(out.num_foxes     == in.num_foxes);
    CHECK(out.gain_remember == in.gain_remember);
    CHECK(out.dual_watch    == in.dual_watch);
    CHECK(out.cross_band    == in.cross_band);
}

static void test_pack_unpack_round_trip_zero_values(void)
{
    t_ardf_df_simple_backup in = {0};
    in.modulation   = MODULATION_FM;
    in.bandwidth    = BANDWIDTH_WIDE;
    in.step_setting = STEP_2_5kHz;

    uint32_t raw = ARDF_DFSimpleBackupPack(&in);
    t_ardf_df_simple_backup out;
    ARDF_DFSimpleBackupUnpack(raw, &out);

    CHECK(out.valid         == false);
    CHECK(out.vfo           == 0);
    CHECK(out.squelch       == 0);
    CHECK(out.modulation    == MODULATION_FM);
    CHECK(out.bandwidth     == BANDWIDTH_WIDE);
    CHECK(out.step_setting  == STEP_2_5kHz);
    CHECK(out.num_foxes     == 0);
    CHECK(out.gain_remember == 0);
    CHECK(out.dual_watch    == 0);
    CHECK(out.cross_band    == 0);
}

// All-0xFF raw EEPROM (erased flash) unpacks with valid=true from this
// function's point of view -- settings.c's load path is responsible for
// special-casing the all-0xFF "EEPROM never written" case BEFORE calling
// unpack (same pattern already used for the other ARDF EEPROM fields).
static void test_unpack_all_ones_sets_valid_true(void)
{
    t_ardf_df_simple_backup out;
    ARDF_DFSimpleBackupUnpack(0xFFFFFFFFu, &out);
    CHECK(out.valid == true);
}

// ARDF_DFSimpleBackup() must snapshot the 8 live values off the VFO that is
// currently active (gEeprom.RX_VFO), and mark the backup valid.
static void test_backup_captures_active_vfo_and_globals(void)
{
    memset(&gEeprom, 0, sizeof(gEeprom));
    gEeprom.RX_VFO = 1;
    gEeprom.SQUELCH_LEVEL = 4;
    gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_A;
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_CHAN_A;
    gEeprom.VfoInfo[1].Modulation = MODULATION_FM;
    gEeprom.VfoInfo[1].CHANNEL_BANDWIDTH = BANDWIDTH_NARROW;
    gEeprom.VfoInfo[1].STEP_SETTING = STEP_12_5kHz;
    gARDFNumFoxes = 5;
    gARDFGainRemember = 1;

    gARDFDFSimpleBackup = (t_ardf_df_simple_backup){0};
    ARDF_DFSimpleBackup();

    CHECK(gARDFDFSimpleBackup.valid == true);
    CHECK(gARDFDFSimpleBackup.vfo == 1);
    CHECK(gARDFDFSimpleBackup.squelch == 4);
    CHECK(gARDFDFSimpleBackup.modulation == MODULATION_FM);
    CHECK(gARDFDFSimpleBackup.bandwidth == BANDWIDTH_NARROW);
    CHECK(gARDFDFSimpleBackup.step_setting == STEP_12_5kHz);
    CHECK(gARDFDFSimpleBackup.num_foxes == 5);
    CHECK(gARDFDFSimpleBackup.gain_remember == 1);
    CHECK(gARDFDFSimpleBackup.dual_watch == DUAL_WATCH_CHAN_A);
    CHECK(gARDFDFSimpleBackup.cross_band == CROSS_BAND_CHAN_A);
}

// ARDF_DFSimpleRestore() must write the 8 values back onto the VFO index
// recorded in the backup -- NOT necessarily the currently active VFO, since
// the user may have switched VFOs while DF-Simple was active -- and then
// invalidate the backup so a second restore is a no-op.
static void test_restore_targets_backed_up_vfo_not_current_one(void)
{
    memset(&gEeprom, 0, sizeof(gEeprom));
    gARDFDFSimpleBackup = (t_ardf_df_simple_backup){
        .valid         = true,
        .vfo           = 0,
        .squelch       = 6,
        .modulation    = MODULATION_FM,
        .bandwidth     = BANDWIDTH_WIDE,
        .step_setting  = STEP_25_0kHz,
        .num_foxes     = 5,
        .gain_remember = 1,
        .dual_watch    = DUAL_WATCH_CHAN_A,
        .cross_band    = CROSS_BAND_OFF,
    };

    // simulate: user switched to VFO 1 and DF-Simple's fixed values are live there
    gEeprom.RX_VFO = 1;
    gEeprom.VfoInfo[1].Modulation = MODULATION_AM;
    gEeprom.VfoInfo[1].CHANNEL_BANDWIDTH = BANDWIDTH_U1K7;
    gEeprom.VfoInfo[1].STEP_SETTING = STEP_1_0kHz;
    gEeprom.SQUELCH_LEVEL = 0;
    gARDFNumFoxes = 0;
    gARDFGainRemember = 0;
    gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;

    ARDF_DFSimpleRestore();

    CHECK(gEeprom.SQUELCH_LEVEL == 6);
    CHECK(gEeprom.VfoInfo[0].Modulation == MODULATION_FM);
    CHECK(gEeprom.VfoInfo[0].CHANNEL_BANDWIDTH == BANDWIDTH_WIDE);
    CHECK(gEeprom.VfoInfo[0].STEP_SETTING == STEP_25_0kHz);
    CHECK(gARDFNumFoxes == 5);
    CHECK(gARDFGainRemember == 1);
    CHECK(gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_A);
    CHECK(gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF);

    // VFO 1 (still active) must be untouched by the restore
    CHECK(gEeprom.VfoInfo[1].Modulation == MODULATION_AM);
    CHECK(gEeprom.VfoInfo[1].CHANNEL_BANDWIDTH == BANDWIDTH_U1K7);
    CHECK(gEeprom.VfoInfo[1].STEP_SETTING == STEP_1_0kHz);

    CHECK(gARDFDFSimpleBackup.valid == false);
}

static void test_restore_is_noop_when_backup_invalid(void)
{
    memset(&gEeprom, 0, sizeof(gEeprom));
    gEeprom.SQUELCH_LEVEL = 7;
    gARDFDFSimpleBackup = (t_ardf_df_simple_backup){0}; // valid = false

    ARDF_DFSimpleRestore();

    CHECK(gEeprom.SQUELCH_LEVEL == 7); // untouched
}

int main(void)
{
    test_pack_unpack_round_trip_max_values();
    test_pack_unpack_round_trip_zero_values();
    test_unpack_all_ones_sets_valid_true();
    test_backup_captures_active_vfo_and_globals();
    test_restore_targets_backed_up_vfo_not_current_one();
    test_restore_is_noop_when_backup_invalid();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
