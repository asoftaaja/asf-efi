#pragma once

// Shared globals — defined in asf_efi.ino, used across modules via this header

#include "sensors.h"
#include "injection.h"

extern uint8_t  tps;       // 0–100 percent
extern uint8_t  fps_sixteenth_bar;  // 0–160, units = 0.0625 bar (1/16 bar)
extern int16_t  iat_degc;  // whole °C
extern int16_t  et_degc;   // whole °C
extern uint8_t  bat_v;     // battery voltage in 1/16 V units (0.0625 V per count)

extern volatile uint16_t rpm;
extern volatile bool     pump_active;
extern bool              pump_manual;        // true = pump held on by PC test command
extern bool              pump_mode_always_on; // false = PID (default), true = full PWM when running
extern uint8_t           pump_pwm;           // last analogWrite value to pump (0–255)

extern uint16_t rpm_axis[RPM_BINS];
extern uint8_t  tps_axis[TPS_BINS];   // 0–100 percent breakpoints

extern uint8_t  inj_map[RPM_BINS][TPS_BINS];
extern uint16_t iat_correction[IAT_CORR_BINS];  // Q8.8: 256 = 1.0 (no correction)
extern uint16_t et_correction[ET_CORR_BINS];    // Q8.8: 256 = 1.0 (no correction)

extern uint16_t pressure_threshold_rpm;
extern float    pressure_low_bar;
extern float    pressure_high_bar;

extern float    pid_kp, pid_ki, pid_kd;

extern uint16_t tps_adc_closed;  // ADC count at fully closed throttle (calibrated)
extern uint16_t tps_adc_open;    // ADC count at fully open throttle (calibrated)
