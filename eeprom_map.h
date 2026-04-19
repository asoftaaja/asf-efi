#pragma once

#include <Arduino.h>
#include "injection.h"
#include "sensors.h"

// EEPROM layout (ATmega328P has 1024 bytes)
//
//  Address  Size   Content
//  -------  ----   -------
//    0        40   Injection map: RPM_BINS × TPS_BINS × uint8_t (1 unit = 100 µs)
//   40        12   PID coefficients: kp, ki, kd as float32 (big-endian)
//   52        10   Pressure table: low_bar, high_bar as float32 + threshold as uint16
//   62        10   IAT correction: IAT_CORR_BINS × uint16_t Q8.8 (256 = 1.0)
//   72        10   ET correction:  ET_CORR_BINS  × uint16_t Q8.8
//   82         1   Magic byte — 0xB1 when EEPROM has been initialised
//   83        20   RPM axis breakpoints: RPM_BINS × uint16_t (big-endian)
//  103         4   TPS axis breakpoints: TPS_BINS × uint8_t (0–100 percent)
//  107         1   Axis magic byte — 0xB2 when axis section has been initialised
//  108         1   Pump mode: 0=PID (default), 1=always-on
//  109         1   Pump mode magic byte — 0xA9 when pump mode section has been initialised
//  110         4   TPS calibration: closed uint16 + open uint16 (big-endian)
//  114         1   TPS cal magic byte — 0xAD when TPS cal section has been initialised
//  115         6   Accel pump: threshold, extra_us, duration_ms as uint16 BE
//  121         1   Accel pump magic — 0xAE
//  -------  ----
//  122 bytes total

#define EEPROM_ADDR_INJ_MAP        0
#define EEPROM_ADDR_PID           40
#define EEPROM_ADDR_PRESSURE      52
#define EEPROM_ADDR_IAT_CORR      62  // 10 bytes (IAT_CORR_BINS × uint16)
#define EEPROM_ADDR_ET_CORR       72  // 10 bytes (ET_CORR_BINS × uint16)
#define EEPROM_ADDR_MAGIC         82
#define EEPROM_MAGIC_VALUE       0xB1  // changed from 0xB0 — map now 10×4, addresses shifted
#define EEPROM_ADDR_RPM_AXIS      83
#define EEPROM_ADDR_TPS_AXIS     103  // 4 bytes
#define EEPROM_ADDR_AXIS_MAGIC        107
#define EEPROM_AXIS_MAGIC_VALUE      0xB2  // changed from 0xAC — axis bins reduced (10 RPM, 4 TPS)
#define EEPROM_ADDR_PUMP_MODE         108  // 1 byte: 0=PID, 1=always_on
#define EEPROM_ADDR_PUMP_MODE_MAGIC   109  // 1 byte: magic when pump mode section init'd
#define EEPROM_PUMP_MODE_MAGIC_VALUE 0xA9
#define EEPROM_ADDR_TPS_CAL           110  // 4 bytes: closed uint16 + open uint16 (big-endian)
#define EEPROM_ADDR_TPS_CAL_MAGIC     114  // 1 byte: magic when TPS cal section init'd
#define EEPROM_TPS_CAL_MAGIC_VALUE   0xAD
//  115         2   accel_threshold_pct_per_s (uint16 BE)
//  117         2   accel_extra_us (uint16 BE)
//  119         2   accel_duration_ms (uint16 BE)
//  121         1   Accel pump magic — 0xAE
//  122 bytes total
#define EEPROM_ADDR_ACCEL_PUMP        115
#define EEPROM_ADDR_ACCEL_PUMP_MAGIC  121
#define EEPROM_ACCEL_PUMP_MAGIC_VALUE 0xAE

void loadFromEEPROM();       // load all sections; writes defaults if uninitialised
void saveInjectionMap();
void savePIDParams();
void savePressureTable();
void saveIATCorrection();
void saveETCorrection();
void saveAxisBreakpoints();
void savePumpMode();
void saveTpsCalibration();
void saveAccelPump();
