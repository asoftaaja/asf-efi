#pragma once

#include <Arduino.h>

// ADC pin assignments
#define PIN_TPS  A0
#define PIN_FPS  A1
#define PIN_IAT  A2
#define PIN_ET   A3
#define PIN_BAT  A7

// Lookup table sizes
#define IAT_BINS 10
#define ET_BINS  10

// Read raw ADC and convert to engineering units
uint16_t readTPS();    // returns 0–1000 per-mille (0 = 0%, 1000 = 100%)
float    readFPS();    // returns bar
int16_t  readIAT();    // returns whole °C
int16_t  readET();     // returns whole °C
float    readBatV();   // returns battery voltage in V (voltage divider ratio 3.2:1)
