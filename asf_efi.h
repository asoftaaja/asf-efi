#pragma once

// Shared globals — defined in asf_efi.ino, used across modules via this header

#include "sensors.h"
#include "injection.h"

extern uint16_t tps;       // 0–1000 per-mille (0 = 0%, 1000 = 100%)
extern float    fps_bar;
extern int16_t  iat_degc;  // whole °C
extern int16_t  et_degc;   // whole °C
extern float    bat_v;     // battery voltage in V

extern volatile uint16_t rpm;
extern volatile bool     pump_active;
extern bool              pump_manual;        // true = pump held on by PC test command
extern bool              pump_mode_always_on; // false = PID (default), true = full PWM when running

extern uint16_t rpm_axis[RPM_BINS];
extern uint16_t tps_axis[TPS_BINS];   // 0–1000 per-mille breakpoints

extern uint16_t inj_map[RPM_BINS][TPS_BINS];
extern uint16_t iat_correction[IAT_BINS];  // Q8.8: 256 = 1.0 (no correction)
extern uint16_t et_correction[ET_BINS];    // Q8.8: 256 = 1.0 (no correction)

extern uint16_t pressure_threshold_rpm;
extern float    pressure_low_bar;
extern float    pressure_high_bar;

extern float    pid_kp, pid_ki, pid_kd;
