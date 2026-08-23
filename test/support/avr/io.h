#pragma once
/* Mock AVR hardware registers for host-side unit tests.
 * All registers are plain global variables defined in test_globals.cpp.
 */
#include <stdint.h>

/* ---------- Timer/Counter 1 registers ---------- */
extern volatile uint16_t TCNT1;   // current counter value
extern volatile uint16_t ICR1;    // input capture register (CKPS edge time)
extern volatile uint16_t OCR1A;   // output compare A (injector close time)
extern volatile uint8_t  TCCR1A;  // control register A
extern volatile uint8_t  TCCR1B;  // control register B

/* TIFR1 -- Timer 1 interrupt flag register */
extern volatile uint8_t  TIFR1;
#define TOV1   0   // bit 0: overflow flag
#define OCF1A  1   // bit 1: output compare A flag
#define ICF1   5   // bit 5: input capture flag

/* TIMSK1 -- Timer 1 interrupt mask register */
extern volatile uint8_t  TIMSK1;
#define TOIE1  0   // bit 0: overflow interrupt enable
#define OCIE1A 1   // bit 1: output compare A interrupt enable
#define ICIE1  5   // bit 5: input capture interrupt enable

/* TCCR1B bit fields */
#define CS10   0
#define CS11   1
#define CS12   2
#define ICES1  6
#define ICNC1  7

/* ---------- Port D ---------- */
extern volatile uint8_t PORTD;
extern volatile uint8_t PIND;   // input register (shift sensor)
#define PD2    2   // shift sensor input pin
#define PD4    4   // injector output pin
#define PD7    7   // ignition cut output pin

/* ---------- Status register (interrupt enable flag) ---------- */
extern volatile uint8_t SREG_reg;
#define SREG SREG_reg

/* ---------- ADC pin aliases (match Arduino header) ---------- */
#define A0  14
#define A1  15
#define A2  16
#define A3  17
#define A7  21
