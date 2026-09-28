/* test_globals.cpp
 *
 * Definitions of every global variable that the real build obtains from
 * asf-efi.ino (the main sketch, which Ceedling cannot compile).
 *
 * Per-module globals that each .cpp defines for itself are NOT listed here:
 *   rpm_axis[], tps_axis[]        -> injection.cpp
 *   pump_pwm                      -> pump.cpp
 *   last_injection_ms/pulse_width -> injection.cpp
 *   injection_trigger             -> ckps.cpp
 *   accel_threshold_*             -> accel_pump.cpp
 *   shift_cut_*                   -> shift_cut.cpp
 *
 * This file is placed in test/support/ so Ceedling links it with every test.
 */

#include "Arduino.h"   // pulls in avr/io.h, EEPROM.h, Serial, etc.
#include "EEPROM.h"
#include "injection.h" // RPM_BINS, TPS_BINS, IAT_CORR_BINS, ET_CORR_BINS
#include "sensors.h"

/* ---------- Sensor readings (filled each main-loop iteration) ---------- */
uint8_t  tps               = 0;   // 0-100 %
uint8_t  fps_sixteenth_bar = 0;   // 0-160 (units: 1/16 bar)
int16_t  iat_degc          = 20;  // intake air temperature  degC
int16_t  et_degc           = 80;  // engine temperature  degC
uint8_t  bat_v             = 0;   // battery voltage (1/16 V per count)

/* ---------- Engine state shared across modules ---------- */
volatile uint16_t rpm          = 0;
volatile bool     pump_active  = false;
bool              pump_manual        = false;
bool              pump_mode_always_on = false;

/* ---------- Injection map and corrections (defined in asf-efi.ino) ---------- */
uint8_t  inj_map[RPM_BINS][TPS_BINS]      = {};        // all zeros (no fuel)
uint16_t iat_correction[IAT_CORR_BINS]    = { 256, 256, 256, 256, 256 };  // 1.0 (neutral)
uint16_t et_correction[ET_CORR_BINS]      = { 256, 256, 256, 256, 256 };

/* ---------- Pressure / PID parameters ---------- */
uint16_t pressure_threshold_rpm = 2000;
float    pressure_low_bar       = 3.0f;
float    pressure_high_bar      = 4.0f;
float    pid_kp                 = 10.0f;
float    pid_ki                 = 2.0f;
float    pid_kd                 = 0.0f;

/* ---------- TPS calibration ---------- */
uint16_t tps_adc_closed = 0;
uint16_t tps_adc_open   = 1023;

/* ---------- AVR hardware register mocks ---------- */
volatile uint16_t TCNT1   = 0;
volatile uint16_t ICR1    = 0;
volatile uint16_t OCR1A   = 0;
volatile uint8_t  TCCR1A  = 0;
volatile uint8_t  TCCR1B  = 0;
volatile uint8_t  TIFR1   = 0;
volatile uint8_t  TIMSK1  = 0;
volatile uint8_t  PORTD   = 0;
volatile uint8_t  PIND    = 0xFF;  // pull-ups: all inputs read high when idle
volatile uint8_t  SREG_reg = 0;

/* ---------- Mock state for Arduino API ---------- */
uint32_t mock_millis_val      = 0;
uint16_t mock_analog_read_val = 0;
uint8_t  mock_analog_write_pin = 0;
uint8_t  mock_analog_write_val = 0;

/* ---------- Mock Serial instance ---------- */
HardwareSerial Serial;

/* ---------- Mock EEPROM ---------- */
uint8_t    mock_eeprom[EEPROM_SIZE] = {};
EEPROMClass EEPROM;
