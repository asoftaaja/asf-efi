#pragma once

#include <Arduino.h>

/**
 * @file powerband.h
 * @brief Low-load (below-powerband) injection multiplier.
 *
 * A two-stroke needs far less fuel at light load below its powerband than the
 * injection map — tuned for on-pipe running — delivers. This module tracks a
 * "powerband" state (RPM *and* TPS both at or above their thresholds) and
 * produces a multiplier applied to the computed pulse width:
 *
 *   - fully in the powerband  → 1.00 (pulse width unchanged)
 *   - fully out               → powerband_multiplier (0.50 by default)
 *
 * The multiplier ramps linearly between those two values over
 * powerband_delay_rev crank revolutions, in both directions, so crossing a
 * threshold never produces a fuelling step. Progress is counted in engine
 * revolutions rather than milliseconds so the transition scales with engine
 * speed.
 */

// ---- Tunable parameters (stored in EEPROM, modifiable via serial) -----------

extern uint16_t powerband_multiplier;      ///< Below-powerband multiplier, Q8.8 (256 = 1.00)
extern uint16_t powerband_threshold_rpm;   ///< RPM at/above which the powerband condition is met
extern uint8_t  powerband_threshold_tps;   ///< TPS percent at/above which the condition is met
extern uint16_t powerband_delay_rev;       ///< Crank revolutions for a full ramp (0 = immediate)

// ---- Public API -------------------------------------------------------------

/**
 * @brief Advance the powerband ramp. Call once per main loop iteration.
 * @param rpm_val         Current engine speed (RPM).
 * @param tps_val         Current throttle position (0–100 %).
 * @param crank_revs_now  Free-running revolution counter from getCrankRevs().
 */
void updatePowerband(uint16_t rpm_val, uint8_t tps_val, uint8_t crank_revs_now);

/**
 * @brief Current effective injection multiplier.
 * @return Q8.8 multiplier (256 = 1.00), interpolated along the ramp.
 */
uint16_t getPowerbandMultiplier();

/**
 * @brief Powerband flag for telemetry.
 * @return true only when the ramp has fully reached the in-powerband end.
 */
bool isPowerbandActive();

/**
 * @brief Reset to the fully out-of-powerband state (engine stopped).
 * @param crank_revs_now  Current revolution counter, resampled to avoid a
 *                        stale delta on the next update.
 */
void resetPowerband(uint8_t crank_revs_now);
