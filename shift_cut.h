#pragma once

#include <Arduino.h>

// Pin assignments
#define PIN_SHIFT_SENSOR 2    // D2, switch to ground (INPUT_PULLUP, active low)
#define PIN_IGN_CUT      7    // D7, high = ignition cut

// Direct port macros — the sensor is read inside the CKPS ISR (D2 = PD2, D7 = PD7)
#define IGN_CUT_ON()    (PORTD |=  (1 << PD7))
#define IGN_CUT_OFF()   (PORTD &= ~(1 << PD7))
#define SHIFT_PRESSED() (!(PIND & (1 << PD2)))

// Allowed range for the cut pulse length (ms)
#define SHIFT_CUT_MIN_MS  10
#define SHIFT_CUT_MAX_MS 100

// Allowed range for the post-shift lockout (ms)
#define SHIFT_LOCKOUT_MIN_MS  500
#define SHIFT_LOCKOUT_MAX_MS 1000

// Tunable parameters (stored in EEPROM, modifiable via serial)
extern uint8_t  shift_cut_enabled;      // 0 = off, 1 = on
extern uint16_t shift_cut_duration_ms;  // cut pulse length, SHIFT_CUT_MIN_MS..SHIFT_CUT_MAX_MS
extern uint16_t shift_cut_min_rpm;      // below this RPM the switch is ignored
extern uint16_t shift_cut_lockout_ms;   // switch ignored this long after a shift,
                                        // SHIFT_LOCKOUT_MIN_MS..SHIFT_LOCKOUT_MAX_MS

void initShiftCut();
void sampleShiftSensor(uint16_t current_rpm);  // called from the CKPS capture ISR
void updateShiftCut(uint32_t now_ms);          // call each main loop iteration
bool isShiftCutActive();
bool isShiftCutLockedOut();
void resetShiftCut();
