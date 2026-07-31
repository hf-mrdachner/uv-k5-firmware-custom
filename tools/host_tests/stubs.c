// Hardware stubs for host-side spectrum tests.
//
// Everything here is a leaf dependency of app/spectrum.c that touches real
// silicon (BK4819 registers, GPIO, keyboard polling, delays, battery ADC,
// backlight) or lives in a .c file we deliberately don't compile for host
// tests (radio.c, settings.c, helper/battery.c, driver/backlight.c). None of
// it is exercised for its hardware effect by the tests in this directory --
// it exists purely so the real app/spectrum.c links and runs on a host
// compiler.
//
// If a test starts needing one of these to behave more realistically (e.g.
// BK4819_GetRSSI returning a scripted sequence of values), give it a real
// body here rather than reaching into spectrum.c's internals from the test.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../../radio.h"
#include "../../settings.h"
#include "../../frequencies.h"
#include "../../driver/bk4819.h"
#include "../../driver/keyboard.h"
#include "../../driver/backlight.h"
#include "../../ui/battery.h"
#include "fake_signal.h"

// ---- Globals the real .c files would define ----
VFO_Info_t gVfoInfoStub;
VFO_Info_t *gCurrentVfo = &gVfoInfoStub;
VFO_Info_t *gRxVfo = &gVfoInfoStub;
VFO_Info_t *gTxVfo = &gVfoInfoStub;
EEPROM_Config_t gEeprom;

uint16_t gBatteryVoltages[4];
uint16_t gBatteryCurrent;
uint8_t gBatteryDisplayLevel;
uint16_t gBatteryCalibration[6] = {1, 2, 3, 4, 5, 6};
uint16_t gBatteryCheckCounter;

#ifdef ENABLE_SCAN_RANGES
uint32_t gScanRangeStart;
uint32_t gScanRangeStop;
#endif

const int8_t dBmCorrTable[7] = {0, 0, 0, 0, 0, 0, 0};

const char gModulationStr[MODULATION_UKNOWN][4] = {"FM", "AM", "USB"};
const char *VfoStateStr[] = {"", "TX_DISABLE", "VOLTAGE_HIGH", "TDR"};

char gInputBox[8];
uint8_t gInputBoxIndex;

// ---- Pixel buffers under test (the real ones app/spectrum.c draws into) ----
uint8_t gStatusLine[128];
uint8_t gFrameBuffer[7][128];

// ---- No-op hardware access ----
void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data) { (void)Register; (void)Data; }
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register) { (void)Register; return 0; }
uint16_t BK4819_GetRegValue(RegisterSpec s) { (void)s; return 0; }
void BK4819_SetRegValue(RegisterSpec s, uint16_t v) { (void)s; (void)v; }
uint16_t fake_rssi_profile[FAKE_RSSI_PROFILE_MAX];
int fake_rssi_profile_len = 0;
int fake_rssi_profile_pos = 0;

uint16_t BK4819_GetRSSI(void) {
    if (fake_rssi_profile_len <= 0) {
        return 260; // fixed mid-range value when no test has scripted a signal
    }
    uint16_t v = fake_rssi_profile[fake_rssi_profile_pos % fake_rssi_profile_len];
    fake_rssi_profile_pos++;
    return v;
}
void BK4819_RX_TurnOn(void) {}
void BK4819_ToggleAFDAC(bool on) { (void)on; }
void BK4819_ToggleAFBit(bool on) { (void)on; }
void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t Pin, bool bSet) { (void)Pin; (void)bSet; }
void BK4819_SetupPowerAmplifier(uint8_t Bias, uint32_t Frequency) { (void)Bias; (void)Frequency; }
bool fake_agc_enabled = true; // tracks what BK4819_SetAGC was last called with
void BK4819_SetAGC(bool enable) { fake_agc_enabled = enable; }
void BK4819_SetFrequency(uint32_t Frequency) { (void)Frequency; }
void BK4819_PickRXFilterPathBasedOnFrequency(uint32_t Frequency) { (void)Frequency; }
void BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different) { (void)Bandwidth; (void)weak_no_different; }

void RADIO_SetModulation(ModulationMode_t m) { (void)m; }
void RADIO_SendEndOfTransmission(void) {}
void RADIO_SendCssTail(void) {}
void RADIO_SetupAGC(bool listeningAM, bool disable) { (void)listeningAM; (void)disable; }
// TX_freq_check: real implementation from frequencies.c is compiled in --
// no hardware dependency, no need to fake it.

void BACKLIGHT_TurnOn(void) {}
void BACKLIGHT_TurnOff(void) {}

KEY_Code_t fake_next_key = KEY_INVALID;
KEY_Code_t KEYBOARD_Poll(void) { return fake_next_key; }

void SYSTEM_DelayMs(unsigned int ms) { (void)ms; }
void SYSTICK_DelayUs(uint32_t us) { (void)us; }

void BOARD_ADC_GetBatteryInfo(uint16_t *pVoltage, uint16_t *pCurrent) {
    if (pVoltage) *pVoltage = 0;
    if (pCurrent) *pCurrent = 0;
}

void UI_DisplayBattery(uint8_t level, uint8_t blink) { (void)level; (void)blink; }
unsigned int BATTERY_VoltsToPercent(unsigned int voltage_10mV) { (void)voltage_10mV; return 50; }

void ST7565_BlitFullScreen(void) {}
void ST7565_BlitStatusLine(void) {}

#include <stdio.h>
int _putchar(char c) { return putchar((unsigned char)c); } // mpaland printf's output sink
