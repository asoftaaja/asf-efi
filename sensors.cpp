#include "sensors.h"

// NTC thermistor lookup: {ADC value, temperature °C}
// Based on 10kΩ NTC (β=3950K) with 10kΩ pull-up to 5V.
// ADC = round(1023 × R(T) / (10000 + R(T))), R(T) = 10000 × exp(3950 × (1/T_K − 1/298.15))
// Entries sorted ADC descending (= temperature ascending).
struct TempEntry { uint16_t adc; int16_t temp_degc; };

static const TempEntry iat_table[IAT_BINS] = {
    { 998, -40 },
    { 934, -20 },
    { 789,   0 },
    { 684,  10 },
    { 512,  25 },
    { 354,  40 },
    { 270,  50 },
    { 204,  60 },
    { 115,  80 },
    {  67, 100 },
};

static const TempEntry et_table[ET_BINS] = {
    { 998, -40 },
    { 789,   0 },
    { 512,  25 },
    { 354,  40 },
    { 204,  60 },
    { 115,  80 },
    {  67, 100 },
    {  40, 120 },
    {  25, 140 },
    {  16, 160 },
};

static int16_t lookupTemp(uint16_t adc, const TempEntry *table, uint8_t bins)
{
    if (adc >= table[0].adc)        return table[0].temp_degc;
    if (adc <= table[bins - 1].adc) return table[bins - 1].temp_degc;

    for (uint8_t i = 0; i < bins - 1; i++) {
        if (adc <= table[i].adc && adc >= table[i + 1].adc) {
            int32_t adc_range  = (int32_t)table[i].adc - (int32_t)table[i + 1].adc;
            int32_t adc_offset = (int32_t)table[i].adc - (int32_t)adc;
            int32_t temp_delta = (int32_t)table[i + 1].temp_degc - (int32_t)table[i].temp_degc;
            // Multiply before divide; add half-divisor for rounding
            int32_t result = (int32_t)table[i].temp_degc
                           + (adc_offset * temp_delta + adc_range / 2) / adc_range;
            return (int16_t)result;
        }
    }
    return 25;
}

uint16_t readTPS()
{
    uint32_t pm = (uint32_t)analogRead(PIN_TPS) * 1000UL / 1023UL;
    return (uint16_t)(pm > 1000 ? 1000 : pm);
}

float readFPS()
{
    float voltage = analogRead(PIN_FPS) * (5.0f / 1023.0f);
    // 0.5 V = 0 bar, 4.5 V = 10 bar  →  bar = (V − 0.5) × 2.5
    return constrain((voltage - 0.5f) * 2.5f, 0.0f, 10.0f);
}

int16_t readIAT()
{
    return lookupTemp(analogRead(PIN_IAT), iat_table, IAT_BINS);
}

int16_t readET()
{
    return lookupTemp(analogRead(PIN_ET), et_table, ET_BINS);
}

float readBatV()
{
    // Voltage divider ratio 3.185:1 → Vbat = ADC × (5.0 × 3.185 / 1023.0)
    return analogRead(PIN_BAT) * (15.92f / 1023.0f);
}
