/* test_powerband.cpp
 *
 * Tests for the low-load (powerband) injection multiplier:
 *   - Linear ramp up over powerband_delay_rev crank revolutions
 *   - Linear ramp down when either threshold is crossed back
 *   - Reversal mid-ramp resumes from the current position (no snap)
 *   - delay_rev == 0 switches on the next revolution
 *   - Both RPM and TPS conditions required; comparison is >=
 *   - Multiplier of 1.00 is a no-op; multiplier above 1.00 ramps downward
 *   - resetPowerband() returns to fully-out without a stale delta
 *   - Revolution counter wraparound (255 -> 0) advances by one
 *
 * Only powerband.h is included, so Ceedling links powerband.cpp alone;
 * the module has no dependencies beyond Arduino.h.
 */

#include "unity.h"
#include "powerband.h"      // module under test (auto-links powerband.cpp)

/* ------------------------------------------------------------------ */
/* Helper: feed n revolutions to the ramp at the given rpm/tps        */
/* Returns the revolution counter value after the last update.        */
/* ------------------------------------------------------------------ */
static uint8_t feed_revs(uint8_t start_revs, uint8_t n, uint16_t rpm_val, uint8_t tps_val)
{
    uint8_t revs = start_revs;
    for (uint8_t i = 0; i < n; i++) {
        revs++;
        updatePowerband(rpm_val, tps_val, revs);
    }
    return revs;
}

/* ------------------------------------------------------------------ */
void setUp(void)
{
    powerband_multiplier    = 128;    // 0.50
    powerband_threshold_rpm = 9000;
    powerband_threshold_tps = 30;
    powerband_delay_rev     = 50;
    resetPowerband(0);
}

void tearDown(void) {}

/* ================================================================== */
/* Ramp up                                                            */
/* ================================================================== */

void test_starts_fully_out_of_powerband(void)
{
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
    TEST_ASSERT_FALSE(isPowerbandActive());
}

void test_ramp_up_reaches_unity_at_delay_revolutions(void)
{
    feed_revs(0, 50, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
    TEST_ASSERT_TRUE(isPowerbandActive());
}

void test_ramp_up_not_complete_one_revolution_early(void)
{
    feed_revs(0, 49, 12000, 80);
    TEST_ASSERT_FALSE(isPowerbandActive());
    TEST_ASSERT_TRUE(getPowerbandMultiplier() < 256);
}

void test_ramp_up_is_linear_at_the_halfway_point(void)
{
    feed_revs(0, 25, 12000, 80);
    /* 128 + (256-128) * 25/50 = 192 */
    TEST_ASSERT_EQUAL_UINT16(192, getPowerbandMultiplier());
}

void test_ramp_saturates_when_held_in_band(void)
{
    feed_revs(0, 200, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
    TEST_ASSERT_TRUE(isPowerbandActive());
}

/* ================================================================== */
/* Ramp down                                                          */
/* ================================================================== */

void test_ramp_down_from_fully_in_over_delay_revolutions(void)
{
    uint8_t revs = feed_revs(0, 50, 12000, 80);
    TEST_ASSERT_TRUE(isPowerbandActive());

    feed_revs(revs, 50, 5000, 80);            // RPM dropped out of the band
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
    TEST_ASSERT_FALSE(isPowerbandActive());
}

void test_flag_clears_on_the_first_revolution_out_of_band(void)
{
    uint8_t revs = feed_revs(0, 50, 12000, 80);
    feed_revs(revs, 1, 12000, 10);            // TPS dropped out of the band
    TEST_ASSERT_FALSE(isPowerbandActive());
    TEST_ASSERT_TRUE(getPowerbandMultiplier() < 256);
}

void test_reversal_mid_ramp_resumes_from_current_position(void)
{
    uint8_t revs = feed_revs(0, 25, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(192, getPowerbandMultiplier());

    revs = feed_revs(revs, 10, 5000, 80);     // back out for 10 revolutions
    /* progress 25 -> 15: 128 + 128 * 15/50 = 166 (integer division) */
    TEST_ASSERT_EQUAL_UINT16(166, getPowerbandMultiplier());

    feed_revs(revs, 5, 12000, 80);            // back in for 5
    /* progress 15 -> 20: 128 + 128 * 20/50 = 179 */
    TEST_ASSERT_EQUAL_UINT16(179, getPowerbandMultiplier());
}

void test_ramp_down_stops_at_the_below_multiplier(void)
{
    uint8_t revs = feed_revs(0, 50, 12000, 80);
    feed_revs(revs, 200, 1000, 5);
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
}

/* ================================================================== */
/* Threshold conditions                                               */
/* ================================================================== */

void test_rpm_alone_does_not_activate(void)
{
    feed_revs(0, 50, 12000, 10);              // RPM in band, TPS below
    TEST_ASSERT_FALSE(isPowerbandActive());
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
}

void test_tps_alone_does_not_activate(void)
{
    feed_revs(0, 50, 5000, 80);               // TPS in band, RPM below
    TEST_ASSERT_FALSE(isPowerbandActive());
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
}

void test_exactly_at_both_thresholds_counts_as_in_band(void)
{
    feed_revs(0, 50, 9000, 30);               // both exactly at threshold
    TEST_ASSERT_TRUE(isPowerbandActive());
}

void test_one_below_threshold_is_out_of_band(void)
{
    feed_revs(0, 50, 8999, 30);
    TEST_ASSERT_FALSE(isPowerbandActive());
    feed_revs(0, 50, 9000, 29);
    TEST_ASSERT_FALSE(isPowerbandActive());
}

/* ================================================================== */
/* Multiplier values                                                  */
/* ================================================================== */

void test_multiplier_of_one_is_a_no_op(void)
{
    powerband_multiplier = 256;               // 1.00 = feature disabled
    resetPowerband(0);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
    feed_revs(0, 25, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
}

void test_multiplier_above_one_ramps_downward(void)
{
    powerband_multiplier = 512;               // 2.00 out of band
    resetPowerband(0);
    TEST_ASSERT_EQUAL_UINT16(512, getPowerbandMultiplier());
    feed_revs(0, 25, 12000, 80);
    /* 512 + (256-512) * 25/50 = 384 */
    TEST_ASSERT_EQUAL_UINT16(384, getPowerbandMultiplier());
    feed_revs(0, 25, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
}

void test_zero_multiplier_cuts_fuel_out_of_band(void)
{
    powerband_multiplier = 0;
    resetPowerband(0);
    TEST_ASSERT_EQUAL_UINT16(0, getPowerbandMultiplier());
    feed_revs(0, 50, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
}

/* ================================================================== */
/* Delay parameter edge cases                                         */
/* ================================================================== */

void test_zero_delay_switches_on_the_next_revolution(void)
{
    powerband_delay_rev = 0;
    resetPowerband(0);
    feed_revs(0, 1, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
    TEST_ASSERT_TRUE(isPowerbandActive());

    feed_revs(1, 1, 5000, 80);
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
    TEST_ASSERT_FALSE(isPowerbandActive());
}

void test_delay_reduced_at_runtime_clamps_progress(void)
{
    uint8_t revs = feed_revs(0, 50, 12000, 80);
    TEST_ASSERT_TRUE(isPowerbandActive());

    powerband_delay_rev = 10;                 // shortened while fully in band
    feed_revs(revs, 1, 12000, 80);
    TEST_ASSERT_EQUAL_UINT16(256, getPowerbandMultiplier());
    TEST_ASSERT_TRUE(isPowerbandActive());
}

/* ================================================================== */
/* Reset and counter wraparound                                       */
/* ================================================================== */

void test_reset_returns_to_fully_out_of_band(void)
{
    uint8_t revs = feed_revs(0, 50, 12000, 80);
    resetPowerband(revs);
    TEST_ASSERT_EQUAL_UINT16(128, getPowerbandMultiplier());
    TEST_ASSERT_FALSE(isPowerbandActive());
}

void test_reset_does_not_leave_a_stale_revolution_delta(void)
{
    /* Run the counter well past the reset point, then reset at that value:
     * the next update must see a delta of 1, not the whole gap. */
    uint8_t revs = feed_revs(0, 200, 5000, 5);
    resetPowerband(revs);
    feed_revs(revs, 1, 12000, 80);
    /* One revolution of a 50-revolution ramp: 128 + 128 * 1/50 = 130 */
    TEST_ASSERT_EQUAL_UINT16(130, getPowerbandMultiplier());
}

void test_revolution_counter_wraparound_advances_by_one(void)
{
    resetPowerband(255);
    updatePowerband(12000, 80, 0);            // 255 -> 0 is a delta of 1
    TEST_ASSERT_EQUAL_UINT16(130, getPowerbandMultiplier());
}

void test_no_revolutions_leaves_the_ramp_unchanged(void)
{
    feed_revs(0, 25, 12000, 80);
    uint16_t before = getPowerbandMultiplier();
    updatePowerband(12000, 80, 25);           // same counter value, no new revolution
    updatePowerband(12000, 80, 25);
    TEST_ASSERT_EQUAL_UINT16(before, getPowerbandMultiplier());
}
