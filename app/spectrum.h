/* Copyright 2023 fagci
 * https://github.com/fagci
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "../bitmaps.h"
#include "../board.h"
#include "../bsp/dp32g030/gpio.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/gpio.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/helper.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const uint8_t DrawingEndY = 40;

static const uint8_t U8RssiMap[] = {
    121, 115, 109, 103, 97, 91, 85, 79, 73, 63,
};

static const uint16_t scanStepValues[] = {
    1, 10, 50, 100, 250, 500, 625, 833, 
    1000, 1250, 1500, 2000, 2500, 5000, 10000,
};

static const uint16_t scanStepBWRegValues[] = {
    //     RX  RXw TX  BW
    // 0b0 000 000 001 01 1000
    // 1
    0b0000000001011000, // 6.25
    // 10
    0b0000000001011000, // 6.25
    // 50
    0b0000000001011000, // 6.25
    // 100
    0b0000000001011000, // 6.25
    // 250
    0b0000000001011000, // 6.25
    // 500
    0b0010010001011000, // 6.25
    // 625
    0b0100100001011000, // 6.25
    // 833
    0b0110110001001000, // 6.25
    // 1000
    0b0110110001001000, // 6.25
    // 1250
    0b0111111100001000, // 6.25
    // 1500
    0b0011011000101000, // 25
    // 2000
    0b0011011000101000, // 25
    // 2500
    0b0011011000101000, // 25
    // 5000
    0b0011011000101000, // 25
    // 10000
    0b0011011000101000, // 25
};

static const uint16_t listenBWRegValues[] = {
    0b0011011000101000, // 25
    0b0111111100001000, // 12.5
    0b0100100001011000, // 6.25
};

typedef enum State {
  SPECTRUM,
  FREQ_INPUT,
  STILL,
} State;

typedef enum StepsCount {
  STEPS_128,
  STEPS_64,
  STEPS_32,
  STEPS_16,
} StepsCount;

typedef enum ScanStep {
  S_STEP_0_01kHz,
  S_STEP_0_1kHz,
  S_STEP_0_5kHz,
  S_STEP_1_0kHz,

  S_STEP_2_5kHz,
  S_STEP_5_0kHz,
  S_STEP_6_25kHz,
  S_STEP_8_33kHz,
  S_STEP_10_0kHz,
  S_STEP_12_5kHz,
  S_STEP_15_0kHz,
  S_STEP_20_0kHz,
  S_STEP_25_0kHz,
  S_STEP_50_0kHz,
  S_STEP_100_0kHz,
} ScanStep;

typedef struct FreqPreset {
  char name[12]; // max 11 chars + null
  uint32_t fStart;
  uint32_t fEnd;
  StepsCount stepsCountIndex;
  ScanStep stepSizeIndex;
  ModulationMode_t modulationType;
  BK4819_FilterBandwidth_t listenBW;
} FreqPreset;

// Presets below 18 MHz omitted: BK4819 hardware minimum is 18 MHz.
//
// ENABLE_DE_HAM_BANDS corrects the amateur-radio entries to the German
// allocations (adds 17m/70cm, narrows 6m/2m to their German width) -- these
// are bands a licensed operator may transmit on with this radio.
//
// ENABLE_DE_EXTRA_BANDS corrects/replaces the remaining entries (CB, PMR,
// LPD, AirBand, Sea/River, LoRaWAN) and drops Railway/Satcom/River1/River2/
// FRS/GSM (found factually wrong for Germany, or not practically useful --
// see docs/superpowers/specs/2026-08-02-german-band-list-design.md). These
// stay receive-only reference bands regardless of the flag: the UV-K5 isn't
// a certified device for PMR446/CB even though those bands are license-free.
//
// Both flags default off; with both off this table is unchanged from
// before either flag existed.
//
// ORDERING NOTE: LPD/LPD433 is placed before 70cmHam even though its
// fStart (43307500) is higher than 70cmHam's (43000000) -- LPD sits
// entirely inside the German 70cm ham band, and AutomaticPresetChoose()/
// DrawStatus() match on "first array entry whose range contains the
// frequency" (app/spectrum.c:777-783, 1449-1457), so LPD must come first
// for a frequency in its slice to be labeled "LPD"/"LPD433" rather than
// "70cmHam". This is the only place in the table where source order isn't
// purely ascending by frequency -- SelectNearestPreset()'s KEY_3/KEY_9
// next/prev navigation will, right at this one boundary, jump to LPD's
// start (433.075 MHz) before reaching 70cmHam's actual start (430.000 MHz)
// coming from below. Accepted trade-off; covered by Task 3's tests.
static const FreqPreset freqPresets[] = {
#ifdef ENABLE_DE_HAM_BANDS
    {"17mHam",  1806800,  1816800, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#endif
    {"15mBC",   1890000,  1902000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"15mHam",  2100000,  2144990, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"13mBC",   2145000,  2185000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
    {"12mHam",  2489000,  2499000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
    {"11mBC",   2567000,  2610000, STEPS_128, S_STEP_5_0kHz,   MODULATION_AM, BK4819_FILTER_BW_NARROW},
#ifdef ENABLE_DE_EXTRA_BANDS
    {"CB",      2656500,  2740500, STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_NARROW},
#else
    {"CB",      2697500,  2799990, STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
    {"10mHam",  2800000,  2970000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#ifdef ENABLE_DE_HAM_BANDS
    {"6mHam",   5000000,  5200000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#else
    {"6mHam",   5000000,  5400000, STEPS_128, S_STEP_1_0kHz,   MODULATION_USB, BK4819_FILTER_BW_NARROWER},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"Flugfunk",11797500, 13700000,STEPS_128, S_STEP_100_0kHz, MODULATION_AM, BK4819_FILTER_BW_NARROW},
#else
    {"AirBand", 11800000, 13500000,STEPS_128, S_STEP_100_0kHz, MODULATION_AM, BK4819_FILTER_BW_NARROW},
#endif
#ifdef ENABLE_DE_HAM_BANDS
    {"2mHam",   14400000, 14600000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"2mHam",   14400000, 14800000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"Railway", 15175000, 15599990,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"Seefunk", 15600000, 16202500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"Sea",     15600000, 16327500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"Satcom",  24300000, 27000000,STEPS_128, S_STEP_5_0kHz,   MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"River1",  30001250, 30051250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"River2",  33601250, 33651250,STEPS_64,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"LPD433",  43307500, 43477500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#else
    {"LPD",     43307500, 43477500,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_HAM_BANDS
    {"70cmHam", 43000000, 44000000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
#ifdef ENABLE_DE_EXTRA_BANDS
    {"PMR446",  44600000, 44620000,STEPS_32,  S_STEP_6_25kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#else
    {"PMR",     44600625, 44620000,STEPS_32,  S_STEP_6_25kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
#ifndef ENABLE_DE_EXTRA_BANDS
    {"FRS 462", 46256250, 46272500,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
    {"FRS 467", 46756250, 46771250,STEPS_16,  S_STEP_12_5kHz,  MODULATION_FM, BK4819_FILTER_BW_NARROW},
#endif
    {"LoRaWAN", 86400000, 86900000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
#ifndef ENABLE_DE_EXTRA_BANDS
    {"GSM-UP",  89000000, 91500000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
    {"GSM-DN",  93500000, 96000000,STEPS_128, S_STEP_100_0kHz, MODULATION_FM, BK4819_FILTER_BW_WIDE},
#endif
    {"23cmHam",124000000,130000000,STEPS_128, S_STEP_25_0kHz,  MODULATION_FM, BK4819_FILTER_BW_WIDE},
};

typedef struct SpectrumSettings {
  uint32_t frequencyChangeStep;  
  StepsCount stepsCount;
  ScanStep scanStepIndex;
  uint16_t scanDelay;
  uint16_t rssiTriggerLevel;
  BK4819_FilterBandwidth_t bw;
  BK4819_FilterBandwidth_t listenBw;
  int dbMin;
  int dbMax;  
  ModulationMode_t modulationType;
  bool backlightState;
} SpectrumSettings;

typedef struct KeyboardState {
  KEY_Code_t current;
  KEY_Code_t prev;
  uint8_t counter;
} KeyboardState;

typedef struct ScanInfo {
  uint16_t rssi, rssiMin, rssiMax;
  uint16_t i, iPeak;
  uint32_t f, fPeak;
  uint16_t scanStep;
  uint16_t measurementsCount;
} ScanInfo;

typedef struct PeakInfo {
  uint16_t t;
  uint16_t rssi;
  uint32_t f;
  uint16_t i;
} PeakInfo;

void APP_RunSpectrum(void);

#endif /* ifndef SPECTRUM_H */

// vim: ft=c
