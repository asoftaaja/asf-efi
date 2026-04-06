#include "ckps.h"
#include "asf_efi.h"

// Defined here, declared extern in ckps.h
volatile bool injection_trigger = false;

// Internal state
static volatile uint16_t prev_capture   = 0;
static volatile uint8_t  ovf_count      = 0;   // Timer1 overflows between two captures
static volatile uint8_t  pulse_count    = 0;   // pulses since startup (gate for pump enable)
static volatile uint32_t last_pulse_ms  = 0;   // millis() at last capture (timeout detection)

void initCKPS()
{
    pinMode(PIN_CKPS, INPUT);

    // Timer1: normal mode, noise canceller on, falling edge capture, prescaler 8 (0.5 µs/tick)
    TCCR1A = 0;
    TCCR1B = (1 << ICNC1) | (1 << CS11);   // ICES1=0 → falling edge; CS11=1 → clk/8
    TIFR1  = (1 << ICF1) | (1 << TOV1);    // clear any pending flags
    TIMSK1 = (1 << ICIE1) | (1 << TOIE1);  // enable input capture + overflow interrupts
}

uint16_t getRPM()
{
    return rpm;
}

bool isCKPSTimeout()
{
    return (millis() - last_pulse_ms) > CKPS_TIMEOUT_MS;
}

void resetCKPS()
{
    pulse_count       = 0;
    injection_trigger = false;
}

// Track Timer1 overflows so we can compute a 32-bit period even at low RPM
ISR(TIMER1_OVF_vect)
{
    if (ovf_count < 255) ovf_count++;
}

// Fires on each falling CKPS edge
ISR(TIMER1_CAPT_vect)
{
    uint16_t capture = ICR1;
    uint8_t  ovf     = ovf_count;

    // Race fix: if overflow flag is set but OVF ISR hasn't run yet, and the capture
    // happened after the overflow (small capture value), count that overflow now.
    if ((TIFR1 & (1 << TOV1)) && capture < 0x8000) {
        ovf++;
    }
    ovf_count = 0;

    // Use signed difference so the overflow count isn't double-counted when the
    // 16-bit counter wraps: (uint16_t) subtraction would add 65536 on its own,
    // but ovf * 65536 already accounts for that same wrap.
    int32_t  signed_diff  = (int32_t)capture - (int32_t)prev_capture;
    uint32_t period_ticks = (uint32_t)((int32_t)ovf * 65536L + signed_diff);
    prev_capture = capture;

    if (period_ticks > 0) {
        // rpm = 60 s/rev × 1/(period_s)  = 60 × 2 000 000 / period_ticks
        uint32_t new_rpm = 120000000UL / period_ticks;
        rpm = (uint16_t)min(new_rpm, (uint32_t)20000);
    }

    last_pulse_ms = millis();

    if (pulse_count < 2) {
        pulse_count++;
        if (pulse_count == 2) {
            pump_active = true;
        }
        return;
    }

    // Below the sync threshold: flag a synchronised injection in the main loop
    if (rpm < RPM_SYNC_THRESHOLD) {
        injection_trigger = true;
    }
}
