#pragma once

#include <Arduino.h>

// Pin / timer
#define PIN_CKPS        8    // ICP1 — Timer1 input capture

// Engine considered stopped after this many ms with no pulse
#define CKPS_TIMEOUT_MS 500

// Set by CKPS ISR when a sync injection should be fired (rpm < RPM_SYNC_THRESHOLD).
// Cleared by main loop after processing.
extern volatile bool injection_trigger;

void     initCKPS();
uint16_t getRPM();
bool     isCKPSTimeout();
void     resetCKPS();
