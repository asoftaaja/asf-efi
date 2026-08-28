#include "powerband.h"

uint16_t powerband_multiplier    = 128;    // 0.50 in Q8.8
uint16_t powerband_threshold_rpm = 9000;
uint8_t  powerband_threshold_tps = 30;
uint16_t powerband_delay_rev     = 50;

// Ramp position: 0 = fully out of the powerband, span = fully in
static uint16_t pb_progress   = 0;
static uint8_t  pb_last_revs  = 0;

/**
 * @brief Ramp length in revolutions, with 0 treated as 1 (immediate switch).
 * @return Number of revolutions for a full ramp, never zero.
 */
static uint16_t rampSpan()
{
    return powerband_delay_rev ? powerband_delay_rev : 1;
}

void updatePowerband(uint16_t rpm_val, uint8_t tps_val, uint8_t crank_revs_now)
{
    // Unsigned 8-bit subtraction wraps correctly when the counter rolls over
    uint8_t delta = (uint8_t)(crank_revs_now - pb_last_revs);
    pb_last_revs  = crank_revs_now;

    uint16_t span = rampSpan();
    if (pb_progress > span) pb_progress = span;   // delay may have been reduced at runtime

    bool in_band = (rpm_val >= powerband_threshold_rpm) &&
                   (tps_val >= powerband_threshold_tps);

    if (in_band) {
        pb_progress = (uint16_t)min((uint32_t)pb_progress + delta, (uint32_t)span);
    } else {
        pb_progress = (delta >= pb_progress) ? 0 : (uint16_t)(pb_progress - delta);
    }
}

uint16_t getPowerbandMultiplier()
{
    // Signed interpolation from powerband_multiplier (progress 0) to 256
    // (progress == span), so a configured multiplier above 1.00 ramps downward
    // just as correctly as the usual below-1.00 case.
    int32_t d = 256L - (int32_t)powerband_multiplier;
    return (uint16_t)((int32_t)powerband_multiplier
                      + (d * (int32_t)pb_progress) / (int32_t)rampSpan());
}

bool isPowerbandActive()
{
    return pb_progress >= rampSpan();
}

void resetPowerband(uint8_t crank_revs_now)
{
    pb_progress  = 0;
    pb_last_revs = crank_revs_now;
}
