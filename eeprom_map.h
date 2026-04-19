#pragma once

#include <Arduino.h>
#include "injection.h"
#include "sensors.h"

// EEPROM layout (ATmega328P has 1024 bytes)
//
//  Address  Size   Content
//  -------  ----   -------
//    0       120   Injection map: RPM_BINS × TPS_BINS × uint16_t (big-endian)
//  120        12   PID coefficients: kp, ki, kd as float32 (big-endian)
//  132        10   Pressure table: low_bar, high_bar as float32 + threshold as uint16
//  142        10   IAT correction: IAT_CORR_BINS × uint16_t Q8.8 (256 = 1.0)
//  152        10   ET correction:  ET_CORR_BINS  × uint16_t Q8.8
//  162         1   Magic byte — 0xAB when EEPROM has been initialised
//  163        24   RPM axis breakpoints: RPM_BINS × uint16_t (big-endian)
//  187         5   TPS axis breakpoints: TPS_BINS × uint8_t (0–100 percent)
//  192         1   Axis magic byte — 0xAC when axis section has been initialised
//  193         1   Pump mode: 0=PID (default), 1=always-on
//  194         1   Pump mode magic byte — 0xA9 when pump mode section has been initialised
//  -------  ----
//  195 bytes total

#define EEPROM_ADDR_INJ_MAP        0
#define EEPROM_ADDR_PID          120
#define EEPROM_ADDR_PRESSURE     132
#define EEPROM_ADDR_IAT_CORR     142  // 10 bytes (IAT_CORR_BINS × uint16)
#define EEPROM_ADDR_ET_CORR      152  // 10 bytes (ET_CORR_BINS × uint16)
#define EEPROM_ADDR_MAGIC        162
#define EEPROM_MAGIC_VALUE      0xAB  // changed from 0xA7 to force re-init (correction bins reduced to 5)
#define EEPROM_ADDR_RPM_AXIS     163
#define EEPROM_ADDR_TPS_AXIS     187  // 5 bytes
#define EEPROM_ADDR_AXIS_MAGIC        192
#define EEPROM_AXIS_MAGIC_VALUE      0xAC  // changed from 0xA8 to force re-init (TPS axis now uint8 percent)
#define EEPROM_ADDR_PUMP_MODE         193  // 1 byte: 0=PID, 1=always_on
#define EEPROM_ADDR_PUMP_MODE_MAGIC   194  // 1 byte: magic when pump mode section init'd
#define EEPROM_PUMP_MODE_MAGIC_VALUE 0xA9
#define EEPROM_ADDR_TPS_CAL           195  // 4 bytes: closed uint16 + open uint16 (big-endian)
#define EEPROM_ADDR_TPS_CAL_MAGIC     199  // 1 byte: magic when TPS cal section init'd
#define EEPROM_TPS_CAL_MAGIC_VALUE   0xAD
//  200         2   accel_threshold_pct_per_s (uint16 BE)
//  202         2   accel_extra_us (uint16 BE)
//  204         2   accel_duration_ms (uint16 BE)
//  206         1   Accel pump magic — 0xAE
//  207 bytes total
#define EEPROM_ADDR_ACCEL_PUMP        200
#define EEPROM_ADDR_ACCEL_PUMP_MAGIC  206
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
