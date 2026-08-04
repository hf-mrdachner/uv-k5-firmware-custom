// Scriptable fake RF signal for host-side spectrum tests.
//
// BK4819_GetRSSI() (stubbed in stubs.c) reads sequentially from this buffer
// instead of touching real hardware. Tests fill fake_rssi_profile with a
// synthetic RSSI-vs-frequency-bin profile, set fake_rssi_profile_len, reset
// fake_rssi_profile_pos to 0, then drive a scan sweep through the real
// spectrum.c scan functions (Scan/NextScanStep/MoveHistory/...).
//
// Calls beyond fake_rssi_profile_len wrap around (index % len), so a sweep
// slightly longer than the profile still returns sane values.

#ifndef FAKE_SIGNAL_H
#define FAKE_SIGNAL_H

#include <stdint.h>

// 512 leaves headroom above the widest ENABLE_SCAN_RANGES regression probe
// (a 5 MHz / 12.5kHz scan = 400 steps, needing 401 profile entries -- see
// test_wide_scan_range_measures_past_128_steps in test_spectrum.c).
#define FAKE_RSSI_PROFILE_MAX 512

extern uint16_t fake_rssi_profile[FAKE_RSSI_PROFILE_MAX];
extern int fake_rssi_profile_len;
extern int fake_rssi_profile_pos;

// Set by the BK4819_SetAGC() stub to whatever it was last called with.
// Currently unread by any test in this directory -- kept as a hook for a
// future test that wants to confirm spectrum code actually froze/restored
// gain around a scan, not as evidence such a test already exists.
extern bool fake_agc_enabled;

// Set by the RADIO_SetupAGC() stub to the `disable` arg it was last called
// with -- i.e. what app/spectrum.c itself requested, one layer above
// fake_agc_enabled (radio.c's own translation of that request into actual
// BK4819_SetAGC() calls isn't compiled for host tests). Read by
// test_agc_frozen_during_scan in test_spectrum.c.
extern bool fake_setupagc_disable_last;

#endif
