#pragma once

#include <Arduino.h>

// Pin assignments
#define PIN_INJECTOR  4    // D4, high = open
#define PIN_LED_RED   13   // D13, high = on

// Injector port macros (D4 = PD4)
#define INJECTOR_ON()   (PORTD |=  (1 << PD4))
#define INJECTOR_OFF()  (PORTD &= ~(1 << PD4))

// Map dimensions
#define RPM_BINS  12
#define TPS_BINS   5

// RPM threshold: below = sync injection (1 pulse/rev), above = 60 Hz fixed
#define RPM_SYNC_THRESHOLD 1500

// millis() timestamp of the last injection firing (used for LED blink logic)
extern volatile uint32_t last_injection_ms;

// Last injector pulse width in µs (used for duty cycle reporting)
extern volatile uint16_t last_pulse_width_us;

void     initInjection();

// Compute final pulse width (µs) from map + corrections
uint16_t calculatePulseWidth(uint16_t rpm_val, uint16_t tps_val,
                             int16_t iat_degc, int16_t et_degc);

// Open injector and schedule close via Timer1 COMPA ISR
void     fireInjector(uint16_t pulse_width_us);

// Force injector closed immediately (engine stop / safety)
void     shutoffInjector();
