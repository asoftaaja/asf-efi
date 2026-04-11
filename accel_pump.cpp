#include "accel_pump.h"

uint16_t accel_threshold_pct_per_s = 50;
uint16_t accel_extra_us             = 500;
uint16_t accel_duration_ms          = 300;

static int8_t   prev_tps       = 0;
static uint32_t prev_sample_ms = 0;
static uint32_t accel_start_ms = 0;
static bool     accel_active   = false;

void updateAccelPump(uint8_t current_tps, uint32_t now_ms)
{
    if (now_ms - prev_sample_ms >= 20) {
        int16_t  delta_tps = (int16_t)current_tps - (int16_t)prev_tps;
        uint32_t delta_ms  = now_ms - prev_sample_ms;
        int32_t  rate      = ((int32_t)delta_tps * 1000L) / (int32_t)delta_ms;

        if (rate > (int32_t)accel_threshold_pct_per_s) {
            accel_active   = true;
            accel_start_ms = now_ms;  // re-trigger resets the decay timer
        }

        prev_tps       = (int8_t)current_tps;
        prev_sample_ms = now_ms;
    }

    if (accel_active && (now_ms - accel_start_ms >= accel_duration_ms))
        accel_active = false;
}

uint16_t getAccelPumpExtra(uint32_t now_ms)
{
    if (!accel_active) return 0;
    uint32_t elapsed = now_ms - accel_start_ms;
    if (elapsed >= accel_duration_ms) return 0;
    return (uint16_t)((uint32_t)accel_extra_us * (accel_duration_ms - elapsed) / accel_duration_ms);
}
