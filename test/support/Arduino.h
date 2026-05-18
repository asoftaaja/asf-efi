#pragma once
/* Minimal Arduino API mock for host-side unit tests.
 *
 * Pulls in the AVR register/interrupt mocks so every firmware source file
 * gets everything it needs by including <Arduino.h> alone.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "avr/io.h"
#include "avr/interrupt.h"
#include "avr/pgmspace.h"

/* ---------- Pin mode / level constants ---------- */
#define INPUT  0
#define OUTPUT 1
#define HIGH   1
#define LOW    0

/* ---------- Arithmetic helpers (match Arduino macros) ---------- */
#define min(a, b)             ((a) < (b) ? (a) : (b))
#define max(a, b)             ((a) > (b) ? (a) : (b))
#define constrain(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define abs(x)                ((x) >= 0 ? (x) : -(x))

/* F() string macro -- on the host all strings are already in RAM */
#define F(s) (s)

/* ---------- Controllable mock state ---------- */
extern uint32_t mock_millis_val;       // returned by millis()
extern uint16_t mock_analog_read_val;  // returned by analogRead()
extern uint8_t  mock_analog_write_pin; // last pin passed to analogWrite()
extern uint8_t  mock_analog_write_val; // last value passed to analogWrite()

/* ---------- Arduino API stubs ---------- */
inline uint32_t millis()                        { return mock_millis_val; }
inline void     pinMode(uint8_t, uint8_t)       {}
inline void     digitalWrite(uint8_t, uint8_t)  {}
inline uint16_t analogRead(uint8_t)             { return mock_analog_read_val; }
inline void     analogWrite(uint8_t pin, uint8_t val) {
    mock_analog_write_pin = pin;
    mock_analog_write_val = val;
}

/* ---------- Mock Serial (HardwareSerial) ---------- */
class HardwareSerial {
public:
    /* Public buffers -- tests read/write these directly */
    uint8_t tx_buf[256];
    uint8_t tx_len;
    uint8_t rx_buf[256];
    uint8_t rx_len;
    uint8_t rx_pos;

    HardwareSerial() { reset(); }

    void reset() {
        tx_len = 0;
        rx_len = 0;
        rx_pos = 0;
    }

    /* Feed bytes into the receive buffer (simulates PC sending data) */
    void push_rx(const uint8_t *data, uint8_t len) {
        for (uint8_t i = 0; i < len && rx_len < 255; i++)
            rx_buf[rx_len++] = data[i];
    }

    /* Arduino Serial API */
    void    begin(uint32_t)        {}
    void    write(uint8_t b)       { if (tx_len < 255) tx_buf[tx_len++] = b; }
    int     read()                 { return (rx_pos < rx_len) ? rx_buf[rx_pos++] : -1; }
    int     available()            { return rx_len - rx_pos; }

    /* print overloads -- only need to compile, not produce testable output */
    void print(const char *)       {}
    void print(int)                {}
    void print(unsigned int)       {}
    void print(long)               {}
    void print(unsigned long)      {}
    void print(float, int = 2)     {}
    void println()                 {}
    void println(const char *)     {}
};

extern HardwareSerial Serial;
