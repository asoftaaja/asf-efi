#pragma once

#include <Arduino.h>

// Pin assignments
#define PIN_PUMP      3    // D3, PWM output (Timer2 OC2B)
#define PIN_LED_GREEN 12   // D12, high = on

#define PRIME_DURATION_MS 2000   // pump prime run time

extern uint8_t pump_pwm;   // last analogWrite value written to pump (0–255)

void initPump();
void updatePump(uint8_t fps_eighth_bar, uint16_t rpm);  // PID iteration, call each loop
void disablePump();                            // immediate shutoff
void primePump();                              // start a timed prime cycle
bool isPriming();                              // true while prime cycle is active
