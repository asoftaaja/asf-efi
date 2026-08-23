/* test_shift_cut.cpp
 *
 * Tests for the shift cut (ignition cut on gear shift) module:
 *   - enable flag and minimum arming RPM gating
 *   - switch press -> ignition cut output high
 *   - output drops again after exactly shift_cut_duration_ms
 *   - holding the switch produces exactly one cut (re-arm needs a release)
 *   - resetShiftCut() drops the output mid-pulse
 *
 * The output is checked through the PORTD mock (bit PD7); the sensor is driven
 * through the PIND mock (bit PD2, active low).
 */

#include "unity.h"
#include "shift_cut.h"      // module under test (auto-links shift_cut.cpp)

/* Mock state provided by test_globals.cpp */
extern uint32_t mock_millis_val;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void press_switch(void)   { PIND &= ~(1 << PD2); }   // active low
static void release_switch(void) { PIND |=  (1 << PD2); }

static bool cut_output_high(void) { return (PORTD & (1 << PD7)) != 0; }

/* ------------------------------------------------------------------ */
void setUp(void)
{
    PORTD = 0;
    PIND  = 0xFF;               // switch released (pull-up)
    mock_millis_val = 0;

    shift_cut_enabled     = 1;
    shift_cut_duration_ms = 50;
    shift_cut_min_rpm     = 3000;

    resetShiftCut();
}

void tearDown(void) {}

/* ================================================================== */
/* Initialisation                                                      */
/* ================================================================== */

void test_init_leaves_cut_output_low(void)
{
    PORTD = 0xFF;
    initShiftCut();
    TEST_ASSERT_FALSE(cut_output_high());
    TEST_ASSERT_FALSE(isShiftCutActive());
}

/* ================================================================== */
/* Gating                                                              */
/* ================================================================== */

void test_no_cut_when_disabled(void)
{
    shift_cut_enabled = 0;
    press_switch();
    sampleShiftSensor(5000);
    updateShiftCut(mock_millis_val);

    TEST_ASSERT_FALSE(cut_output_high());
}

void test_no_cut_below_min_rpm(void)
{
    press_switch();
    sampleShiftSensor(2999);
    updateShiftCut(mock_millis_val);

    TEST_ASSERT_FALSE(cut_output_high());
}

void test_cut_at_exactly_min_rpm(void)
{
    press_switch();
    sampleShiftSensor(3000);
    updateShiftCut(mock_millis_val);

    TEST_ASSERT_TRUE(cut_output_high());
}

/* ================================================================== */
/* Pulse timing                                                        */
/* ================================================================== */

void test_press_asserts_cut_output(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(mock_millis_val);

    TEST_ASSERT_TRUE(cut_output_high());
    TEST_ASSERT_TRUE(isShiftCutActive());
}

/* Output must stay high for the full duration and drop on the tick that
 * reaches it -- 50 ms here. */
void test_cut_lasts_configured_duration(void)
{
    shift_cut_duration_ms = 50;

    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    TEST_ASSERT_TRUE(cut_output_high());

    updateShiftCut(49);
    TEST_ASSERT_TRUE(cut_output_high());

    updateShiftCut(50);
    TEST_ASSERT_FALSE(cut_output_high());
    TEST_ASSERT_FALSE(isShiftCutActive());
}

void test_minimum_duration_pulse(void)
{
    shift_cut_duration_ms = SHIFT_CUT_MIN_MS;   // 10 ms

    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    updateShiftCut(9);
    TEST_ASSERT_TRUE(cut_output_high());

    updateShiftCut(10);
    TEST_ASSERT_FALSE(cut_output_high());
}

void test_maximum_duration_pulse(void)
{
    shift_cut_duration_ms = SHIFT_CUT_MAX_MS;   // 100 ms

    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    updateShiftCut(99);
    TEST_ASSERT_TRUE(cut_output_high());

    updateShiftCut(100);
    TEST_ASSERT_FALSE(cut_output_high());
}

/* ================================================================== */
/* Re-arm behaviour                                                    */
/* ================================================================== */

/* Holding the lever down must not produce a second cut, however many CKPS
 * pulses go by. */
void test_held_switch_gives_only_one_cut(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    TEST_ASSERT_TRUE(cut_output_high());

    updateShiftCut(50);
    TEST_ASSERT_FALSE(cut_output_high());

    /* Still held: further samples must not re-trigger */
    for (uint8_t i = 0; i < 10; i++) {
        sampleShiftSensor(6000);
        updateShiftCut(60 + i);
        TEST_ASSERT_FALSE(cut_output_high());
    }
}

void test_release_then_press_gives_second_cut(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    updateShiftCut(50);
    TEST_ASSERT_FALSE(cut_output_high());

    release_switch();
    sampleShiftSensor(6000);       // re-arms
    updateShiftCut(60);
    TEST_ASSERT_FALSE(cut_output_high());

    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(70);
    TEST_ASSERT_TRUE(cut_output_high());
}

/* A press while a cut is already running must not restart or extend it */
void test_retrigger_during_cut_does_not_extend_pulse(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);

    release_switch();
    sampleShiftSensor(6000);
    press_switch();
    sampleShiftSensor(6000);       // sets the trigger flag again
    updateShiftCut(20);            // consumed, but cut already running

    updateShiftCut(50);            // original pulse still ends at 50 ms
    TEST_ASSERT_FALSE(cut_output_high());
}

/* ================================================================== */
/* Reset                                                               */
/* ================================================================== */

void test_reset_drops_output_mid_pulse(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    TEST_ASSERT_TRUE(cut_output_high());

    resetShiftCut();
    TEST_ASSERT_FALSE(cut_output_high());
    TEST_ASSERT_FALSE(isShiftCutActive());
}

/* After a reset the switch must be re-armed even if it was never released,
 * but only once the rider lets go -- a stall with the lever held should not
 * fire a cut on the next start-up pulse. */
void test_reset_rearms_module(void)
{
    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    resetShiftCut();

    sampleShiftSensor(6000);       // still held
    updateShiftCut(10);
    TEST_ASSERT_TRUE(cut_output_high());   // reset re-armed it
}

/* ================================================================== */
/* Cut output must not touch the injector bit                          */
/* ================================================================== */

void test_cut_does_not_disturb_injector_bit(void)
{
    PORTD |= (1 << PD4);           // injector open

    press_switch();
    sampleShiftSensor(6000);
    updateShiftCut(0);
    TEST_ASSERT_TRUE(PORTD & (1 << PD4));

    updateShiftCut(50);
    TEST_ASSERT_TRUE(PORTD & (1 << PD4));
}
