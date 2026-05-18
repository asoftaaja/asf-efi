/* test_ckps.cpp
 *
 * Tests for the CKPS (crankshaft position sensor) module:
 *   - RPM calculation from Timer1 capture values, including overflow handling
 *   - Race-condition fix (pending overflow flag at capture time)
 *   - RPM cap at 20 000
 *   - Pump-enable gating (first two pulses only arm the pump)
 *   - injection_trigger flag logic below/above RPM_SYNC_THRESHOLD
 *   - isCKPSTimeout() timing
 *   - resetCKPS()
 *
 * How ISRs are called:
 *   The mock avr/interrupt.h defines  ISR(vec) -> void vec(void)
 *   so ckps.cpp's ISR bodies become plain functions callable from tests.
 */

#include "unity.h"
#include "ckps.h"           // module under test (auto-links ckps.cpp)

/* Globals provided by test_globals.cpp (always linked via test/support) */
extern volatile uint16_t rpm;
extern volatile bool     pump_active;
extern uint32_t          mock_millis_val;

/* AVR register mocks -- declared in avr/io.h, defined in test_globals.cpp */
// ICR1, TIFR1, TOV1 already visible through ckps.h -> Arduino.h -> avr/io.h

/* ISR functions become ordinary C++ functions with the mock ISR() macro */
void TIMER1_CAPT_vect(void);
void TIMER1_OVF_vect(void);

/* ------------------------------------------------------------------ */
/* Helper: advance the startup gate past the two initial CKPS pulses  */
/* After this call: pulse_count == 2, pump_active == true             */
/* ------------------------------------------------------------------ */
static void advance_past_startup(void)
{
    resetCKPS();          // pulse_count = 0, injection_trigger = false
    pump_active        = false;
    ICR1               = 0;
    TIFR1              = 0;
    mock_millis_val    = 0;
    TIMER1_CAPT_vect();   // pulse 1
    TIMER1_CAPT_vect();   // pulse 2 -> pump_active = true
}

/* ------------------------------------------------------------------ */
void setUp(void)
{
    resetCKPS();
    pump_active     = false;
    rpm             = 0;
    ICR1            = 0;
    TIFR1           = 0;
    mock_millis_val = 0;
}

void tearDown(void) {}

/* ================================================================== */
/* RPM calculation                                                     */
/* ================================================================== */

/* Basic case: two captures with 20 000-tick gap -> 6 000 RPM.
 * Formula: 120 000 000 / 20 000 = 6 000.  No overflow needed (fits in uint16). */
void test_rpm_calculation_6000_rpm(void)
{
    advance_past_startup();   // prev_capture = 0, ovf_count = 0
    ICR1 = 20000;
    TIMER1_CAPT_vect();
    TEST_ASSERT_EQUAL_UINT16(6000, getRPM());
}

/* One counter overflow between captures simulates a slow engine (~1 000 RPM).
 * Period = 1 x 65536 + (54464 - 0) = 120 000 ticks -> 1 000 RPM. */
void test_rpm_calculation_with_one_overflow(void)
{
    advance_past_startup();   // prev_capture = 0
    TIMER1_OVF_vect();        // ovf_count = 1
    ICR1 = 54464;             // 120 000 - 65 536
    TIMER1_CAPT_vect();
    TEST_ASSERT_EQUAL_UINT16(1000, getRPM());
}

/* Two overflows -> very slow idle (~500 RPM).
 * Period = 2 x 65536 + (3928 - 0) = 135 000 ticks -> ~889 RPM. */
void test_rpm_calculation_with_two_overflows(void)
{
    advance_past_startup();
    TIMER1_OVF_vect();
    TIMER1_OVF_vect();
    ICR1 = 3928;              // 2*65536 + 3928 = 134 000? let's compute:
    // period = 2*65536 + 3928 = 135000; rpm = 120000000/135000 = 888
    TIMER1_CAPT_vect();
    TEST_ASSERT_UINT16_WITHIN(5, 888, getRPM());
}

/* Race-condition fix: TOV1 is set (pending overflow not yet counted by OVF ISR)
 * and capture value is small (< 0x8000), so the CAPT ISR must count the overflow.
 * Setup: prev_capture = 60 000, ICR1 = 1 000, ovf_count = 0, TIFR1.TOV1 = 1.
 * period = 1 x 65536 + (1000 - 60000) = 6536  -> rpm = 120 000 000 / 6536 ~= 18 360. */
void test_rpm_race_condition_fix(void)
{
    advance_past_startup();
    ICR1 = 60000;
    TIMER1_CAPT_vect();       // prev_capture = 60000, ovf_count reset to 0

    /* Simulate pending overflow flag without OVF ISR having fired */
    TIFR1 = (1 << TOV1);     // bit 0 set
    ICR1  = 1000;             // small value -> race condition applies
    TIMER1_CAPT_vect();
    /* 120 000 000 / 6536 ~= 18 360; accept +/-50 RPM for integer rounding */
    TEST_ASSERT_UINT16_WITHIN(50, 18360, getRPM());
}

/* Without the race fix (TIFR1.TOV1 not set), the same pair of captures gives a
 * different (incorrect) period and should NOT equal the race-fix result. */
void test_rpm_no_false_race_fix_when_tov1_clear(void)
{
    advance_past_startup();
    ICR1  = 60000;
    TIMER1_CAPT_vect();

    TIFR1 = 0;               // no pending overflow
    ICR1  = 61000;           // simple forward advance of 1000 ticks
    TIMER1_CAPT_vect();
    /* period = 1000 -> rpm = 120 000 */
    TEST_ASSERT_EQUAL_UINT16(20000, getRPM());  // capped at 20 000
}

/* RPM is clamped at 20 000 regardless of how short the period is. */
void test_rpm_cap_at_20000(void)
{
    advance_past_startup();
    ICR1 = 3;                 // period = 3 -> 120 000 000 / 3 = 40 000 000 > cap
    TIMER1_CAPT_vect();
    TEST_ASSERT_EQUAL_UINT16(20000, getRPM());
}

/* ================================================================== */
/* Pump-enable gating                                                  */
/* ================================================================== */

/* pump_active must stay false until exactly the second CKPS pulse. */
void test_pump_enable_gating_two_pulses(void)
{
    resetCKPS();
    pump_active = false;
    ICR1        = 0;

    TIMER1_CAPT_vect();       // pulse 1
    TEST_ASSERT_FALSE(pump_active);

    TIMER1_CAPT_vect();       // pulse 2
    TEST_ASSERT_TRUE(pump_active);
}

/* Extra pulses must not toggle pump_active back off. */
void test_pump_stays_active_after_startup(void)
{
    advance_past_startup();
    ICR1 = 20000;
    TIMER1_CAPT_vect();       // pulse 3+
    TEST_ASSERT_TRUE(pump_active);
}

/* ================================================================== */
/* injection_trigger flag                                              */
/* ================================================================== */

/* At low RPM (below RPM_SYNC_THRESHOLD = 1500) the ISR sets the flag. */
void test_injection_trigger_set_below_threshold(void)
{
    advance_past_startup();   // prev_capture = 0
    /* 90 000 ticks -> 120 000 000 / 90 000 = 1 333 RPM < 1 500 */
    TIMER1_OVF_vect();
    ICR1              = 24464;   // 90 000 - 65 536
    injection_trigger = false;
    TIMER1_CAPT_vect();
    TEST_ASSERT_TRUE(injection_trigger);
}

/* At or above RPM_SYNC_THRESHOLD the flag must not be set. */
void test_injection_trigger_not_set_above_threshold(void)
{
    advance_past_startup();
    /* 20 000 ticks -> 6 000 RPM > 1 500 */
    ICR1              = 20000;
    injection_trigger = false;
    TIMER1_CAPT_vect();
    TEST_ASSERT_FALSE(injection_trigger);
}

/* Exactly at the threshold (1 500 RPM): period = 80 000 ticks.
 * 80 000 = 1 overflow + 14 464 ticks. rpm = 1 500 which is NOT < 1500 -> no trigger. */
void test_injection_trigger_not_set_at_exactly_threshold(void)
{
    advance_past_startup();
    TIMER1_OVF_vect();
    ICR1              = 14464;   // 80 000 - 65 536
    injection_trigger = false;
    TIMER1_CAPT_vect();
    TEST_ASSERT_FALSE(injection_trigger);
}

/* The startup pulses (1st and 2nd) must never set injection_trigger even if
 * the RPM is below the threshold. */
void test_injection_trigger_not_set_during_startup_pulses(void)
{
    resetCKPS();
    pump_active       = false;
    injection_trigger = false;
    ICR1              = 0;

    TIMER1_CAPT_vect();    // pulse 1
    TEST_ASSERT_FALSE(injection_trigger);

    TIMER1_CAPT_vect();    // pulse 2
    TEST_ASSERT_FALSE(injection_trigger);
}

/* ================================================================== */
/* Timeout detection                                                   */
/* ================================================================== */

/* No timeout when elapsed time < CKPS_TIMEOUT_MS (500 ms). */
void test_ckps_timeout_false_within_window(void)
{
    advance_past_startup();   // last_pulse_ms = 0
    mock_millis_val = 400;
    TEST_ASSERT_FALSE(isCKPSTimeout());
}

/* Timeout triggered when elapsed time > 500 ms. */
void test_ckps_timeout_true_after_window(void)
{
    advance_past_startup();   // last_pulse_ms = 0
    mock_millis_val = 600;
    TEST_ASSERT_TRUE(isCKPSTimeout());
}

/* Timeout at exactly 500 ms: millis() - last_pulse_ms == 500 is NOT > 500. */
void test_ckps_timeout_false_at_exact_boundary(void)
{
    advance_past_startup();
    mock_millis_val = 500;
    TEST_ASSERT_FALSE(isCKPSTimeout());
}

/* ================================================================== */
/* resetCKPS                                                           */
/* ================================================================== */

void test_reset_ckps_clears_injection_trigger(void)
{
    injection_trigger = true;
    resetCKPS();
    TEST_ASSERT_FALSE(injection_trigger);
}

/* After resetCKPS the pump-enable gate resets: first two pulses must re-arm. */
void test_reset_ckps_re_enables_startup_gate(void)
{
    advance_past_startup();   // pulse_count = 2, pump_active = true
    pump_active = false;      // force back to false

    resetCKPS();              // pulse_count = 0

    TIMER1_CAPT_vect();       // pulse 1 after reset
    TEST_ASSERT_FALSE(pump_active);

    TIMER1_CAPT_vect();       // pulse 2 after reset
    TEST_ASSERT_TRUE(pump_active);
}
