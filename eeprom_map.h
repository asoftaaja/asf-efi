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
//  142        20   IAT correction: IAT_BINS × uint16_t Q8.8 (256 = 1.0)
//  162        20   ET correction:  ET_BINS  × uint16_t Q8.8
//  182         1   Magic byte — 0xA7 when EEPROM has been initialised
//  183        24   RPM axis breakpoints: RPM_BINS × uint16_t (big-endian)
//  207        10   TPS axis breakpoints: TPS_BINS × uint16_t (0–1000 per-mille)
//  217         1   Axis magic byte — 0xA8 when axis section has been initialised
//  -------  ----
//  218 bytes total

#define EEPROM_ADDR_INJ_MAP        0
#define EEPROM_ADDR_PID          120
#define EEPROM_ADDR_PRESSURE     132
#define EEPROM_ADDR_IAT_CORR     142  // 20 bytes (was 40)
#define EEPROM_ADDR_ET_CORR      162  // 20 bytes (was 40)
#define EEPROM_ADDR_MAGIC        182  // was 222
#define EEPROM_MAGIC_VALUE      0xA7  // was 0xA5; changed to force re-init on upgrade
#define EEPROM_ADDR_RPM_AXIS     183  // was 223
#define EEPROM_ADDR_TPS_AXIS     207  // 10 bytes (was 20 bytes float)
#define EEPROM_ADDR_AXIS_MAGIC   217  // was 267
#define EEPROM_AXIS_MAGIC_VALUE 0xA8  // was 0xA6; changed to force re-init on upgrade

void loadFromEEPROM();       // load all sections; writes defaults if uninitialised
void saveInjectionMap();
void savePIDParams();
void savePressureTable();
void saveIATCorrection();
void saveETCorrection();
void saveAxisBreakpoints();
