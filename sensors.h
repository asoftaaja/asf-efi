#pragma once

#include <Arduino.h>

// ADC pin assignments
#define PIN_TPS  A0
#define PIN_FPS  A1
#define PIN_IAT  A2
#define PIN_ET   A3
#define PIN_BAT  A7

// NTC thermistor lookup table sizes (do not change without updating iat_table/et_table in sensors.cpp)
#define IAT_BINS 10
#define ET_BINS  10

// Injection correction table sizes (independent of thermistor bins)
#define IAT_CORR_BINS 5
#define ET_CORR_BINS  5

// Read raw ADC and convert to engineering units
uint16_t readTPS();    // returns 0–1000 per-mille (0 = 0%, 1000 = 100%)
float    readFPS();    // returns bar
int16_t  readIAT();    // returns whole °C
int16_t  readET();     // returns whole °C
float    readBatV();   // returns battery voltage in V (voltage divider ratio 3.2:1)
