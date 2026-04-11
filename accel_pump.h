#pragma once

#include <Arduino.h>

// Tunable parameters (stored in EEPROM, modifiable via serial)
extern uint16_t accel_threshold_pct_per_s;  // TPS rate threshold to trigger (%/sec)
extern uint16_t accel_extra_us;              // Peak extra pulse width at trigger (µs)
extern uint16_t accel_duration_ms;           // Enrichment decay duration (ms)

// Call after readTPS() each main loop iteration
void updateAccelPump(uint8_t current_tps, uint32_t now_ms);

// Returns additional pulse width (µs) to add to injection; 0 if inactive
uint16_t getAccelPumpExtra(uint32_t now_ms);

// Returns true while enrichment is active (for telemetry)
bool isAccelPumpActive();
