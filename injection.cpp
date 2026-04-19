#include "injection.h"
#include "asf_efi.h"

volatile uint32_t last_injection_ms   = 0;
volatile uint16_t last_pulse_width_us = 0;

// RPM and TPS axis breakpoints for the 12×5 injection map
// Mutable so they can be updated via the serial command CMD_WRITE_AXIS and saved to EEPROM.
uint16_t rpm_axis[RPM_BINS] = { 1000, 4000, 7000, 9000, 11000,
                                12500, 13500, 14500, 15500, 17000 };
uint8_t  tps_axis[TPS_BINS] = { 0, 30, 60, 100 };  // 0–100 percent

// Temperature breakpoints for the correction coefficient arrays (stored in EEPROM)
static const int16_t IAT_CORR_TEMPS[IAT_CORR_BINS] = { -20,  0, 20, 40,  70 };
static const int16_t ET_CORR_TEMPS[ET_CORR_BINS]   = {   0, 25, 50, 80, 100 };

// ---- Internal helpers -------------------------------------------------------

static uint8_t interpolateMap(uint16_t rpm_val, uint8_t tps_val)
{
    // Find surrounding RPM cell
    uint8_t ri = 0;
    while (ri < RPM_BINS - 2 && rpm_val >= rpm_axis[ri + 1]) ri++;
    uint8_t ri2 = ri + 1;

    // Find surrounding TPS cell
    uint8_t ti = 0;
    while (ti < TPS_BINS - 2 && tps_val >= tps_axis[ti + 1]) ti++;
    uint8_t ti2 = ti + 1;

    // Clamp deltas to cell bounds, then compute Q16 fractions (0x0000–0xFFFF = 0.0–1.0)
    uint16_t rpm_range = rpm_axis[ri2] - rpm_axis[ri];
    uint16_t tps_range = tps_axis[ti2] - tps_axis[ti];
    uint16_t rpm_delta = (rpm_val > rpm_axis[ri2]) ? rpm_range :
                         (rpm_val < rpm_axis[ri])  ? 0 : rpm_val - rpm_axis[ri];
    uint16_t tps_delta = (tps_val > tps_axis[ti2]) ? tps_range :
                         (tps_val < tps_axis[ti])  ? 0 : tps_val - tps_axis[ti];
    uint32_t rpm_frac = (rpm_range > 0) ? ((uint32_t)rpm_delta << 16) / rpm_range : 0;
    uint32_t tps_frac = (tps_range > 0) ? ((uint32_t)tps_delta << 16) / tps_range : 0;

    // Bilinear interpolation using int32 to handle signed deltas safely
    int32_t v00 = inj_map[ri ][ti ], v10 = inj_map[ri2][ti ];
    int32_t v01 = inj_map[ri ][ti2], v11 = inj_map[ri2][ti2];
    int32_t v0 = v00 + (((v10 - v00) * (int32_t)rpm_frac) >> 16);
    int32_t v1 = v01 + (((v11 - v01) * (int32_t)rpm_frac) >> 16);
    int32_t r  = v0  + (((v1  - v0 ) * (int32_t)tps_frac) >> 16);

    if (r <= 0) return 0;
    return (uint8_t)(r > 255 ? 255 : r);
}

// corrections[] is Q8.8: 256 = 1.0. Returns Q8.8.
static uint16_t interpolateCorrection(int16_t temp,
                                      const int16_t  *temps,
                                      const uint16_t *corrections,
                                      uint8_t         bins)
{
    if (temp <= temps[0])        return corrections[0];
    if (temp >= temps[bins - 1]) return corrections[bins - 1];

    for (uint8_t i = 0; i < bins - 1; i++) {
        if (temp >= temps[i] && temp < temps[i + 1]) {
            // Q16 fraction of position within this cell
            uint32_t frac = ((uint32_t)(uint16_t)(temp - temps[i]) << 16)
                          / (uint16_t)(temps[i + 1] - temps[i]);
            int32_t delta  = (int32_t)corrections[i + 1] - (int32_t)corrections[i];
            int32_t result = (int32_t)corrections[i] + (int32_t)((delta * (int32_t)frac) >> 16);
            if (result < 0) result = 0;
            return (uint16_t)result;
        }
    }
    return 256;  // 1.0 in Q8.8
}

// ---- Public API -------------------------------------------------------------

void initInjection()
{
    pinMode(PIN_INJECTOR, OUTPUT);
    pinMode(PIN_LED_RED,  OUTPUT);
    INJECTOR_OFF();                  // injector off
    digitalWrite(PIN_LED_RED,  LOW);
}

uint16_t calculatePulseWidth(uint16_t rpm_val, uint8_t tps_val,
                             int16_t iat_degc_val, int16_t et_degc_val)
{
    uint8_t  base_units = interpolateMap(rpm_val, tps_val);  // units of 100 µs
    if (base_units == 0) return 0;
    uint16_t base_pw = (uint16_t)base_units * 100u;          // convert to µs

    uint16_t iat_corr = interpolateCorrection(iat_degc_val,
                                              IAT_CORR_TEMPS, iat_correction, IAT_CORR_BINS);
    uint16_t et_corr  = interpolateCorrection(et_degc_val,
                                              ET_CORR_TEMPS,  et_correction,  ET_CORR_BINS);

    // Apply Q8.8 corrections: multiply then shift right by 8
    uint32_t pw = (uint32_t)base_pw * iat_corr >> 8;
    pw          = pw          * et_corr  >> 8;
    if (pw > MAX_PULSE_US) pw = MAX_PULSE_US;
    return (uint16_t)pw;
}

void fireInjector(uint16_t pulse_width_us)
{
    if (pulse_width_us == 0) return;
    last_pulse_width_us = pulse_width_us;

    // Convert µs to Timer1 ticks (prescaler 8, 0.5 µs/tick → multiply by 2)
    uint16_t ticks = (uint16_t)min((uint32_t)pulse_width_us * 2UL, (uint32_t)65000U);

    INJECTOR_ON();                   // injector on
    last_injection_ms = millis();

    // Schedule close via Timer1 output compare A
    uint8_t sreg = SREG;
    cli();
    OCR1A  = TCNT1 + ticks;
    TIFR1 |= (1 << OCF1A);       // clear any stale compare flag
    TIMSK1 |= (1 << OCIE1A);     // enable compare-A interrupt
    SREG = sreg;
}

void shutoffInjector()
{
    TIMSK1 &= ~(1 << OCIE1A);    // disable compare interrupt
    INJECTOR_OFF();              // injector off
}

// Timer1 output compare A ISR — fires at the scheduled injector close time
ISR(TIMER1_COMPA_vect)
{
    TIMSK1 &= ~(1 << OCIE1A);
    INJECTOR_OFF();              // injector off
}
