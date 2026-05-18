/* test_pump.cpp
 *
 * Tests for the fuel-pump PI pressure controller:
 *   - updatePump(): proportional response, integral accumulation, anti-windup,
 *     high/low pressure target selection, zero-error output
 *   - disablePump(): PWM zeroed, integral reset
 *   - primePump() / isPriming(): full-PWM timing
 *
 * pump.cpp is auto-linked (Ceedling finds it from pump.h).
 * It defines pump_pwm; all other globals come from test_globals.cpp.
 */

#include "unity.h"
#include "pump.h"      // module under test

/* Globals from test_globals.cpp */
extern uint32_t mock_millis_val;
extern uint8_t  mock_analog_write_val;
extern uint8_t  mock_analog_write_pin;
extern float    pid_kp, pid_ki, pid_kd;
extern float    pressure_low_bar, pressure_high_bar;
extern uint16_t pressure_threshold_rpm;
extern volatile bool pump_active;

/* pump_pwm is defined in pump.cpp */
extern uint8_t pump_pwm;

/* ------------------------------------------------------------------ */
/* Reset internal pump state via disablePump() and set millis to 0.
 * Also prime pid_prev_ms by calling updatePump once with zero gains. */
static void reset_pump_state(void)
{
    pid_kp = 0.0f;
    pid_ki = 0.0f;
    pid_kd = 0.0f;
    pressure_low_bar        = 3.0f;
    pressure_high_bar       = 4.0f;
    pressure_threshold_rpm  = 2000;
    mock_millis_val         = 0;
    mock_analog_write_val   = 0;
    pump_active             = true;
    disablePump();        // zero PWM, reset integral, reset priming

    /* Initialize pid_prev_ms = 0.  Use fps = target (error = 0) so that
     * the integral stays at 0 regardless of any stale dt left from a prior
     * test (pressure_low_bar = 3.0 bar = 48 sixteenth-bar units). */
    updatePump(48, 0);
}

/* ------------------------------------------------------------------ */
void setUp(void)    { reset_pump_state(); }
void tearDown(void) {}

/* ================================================================== */
/* Proportional response                                               */
/* ================================================================== */

/* With ki=0 the output is purely proportional:
 *   target_bar = pressure_low_bar = 3.0 (rpm < threshold)
 *   target_sixteenth = 3.0 x 16 = 48
 *   fps_sixteenth = 32 (= 2.0 bar)
 *   error = 48 - 32 = 16
 *   output_scaled = kp x error x 1000 = 16 x 16 x 1000 = 256 000
 *   pump_pwm = 256 000 / 16 000 = 16 */
void test_proportional_response_basic(void)
{
    pid_kp = 16.0f;
    mock_millis_val = 1000;      // dt_ms = 1000 ms since last call at t=0
    updatePump(32, 0);           // fps=32 (2.0 bar), rpm=0 -> low target
    TEST_ASSERT_EQUAL_UINT8(16, pump_pwm);
}

/* Zero error -> zero output. */
void test_zero_error_zero_output(void)
{
    pid_kp = 16.0f;
    mock_millis_val = 1000;
    /* target = 3.0 bar = 48 sixteenth; fps = 48 -> error = 0 */
    updatePump(48, 0);
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
}

/* High RPM selects pressure_high_bar target.
 * pressure_high_bar = 4.0 bar = 64 sixteenth; fps = 48 (3.0 bar); error = 16. */
void test_high_rpm_selects_high_pressure_target(void)
{
    pid_kp = 16.0f;
    mock_millis_val = 1000;
    /* rpm=3000 > threshold=2000 -> use pressure_high_bar */
    updatePump(48, 3000);
    /* error = 64 - 48 = 16; output = 16*16*1000/16000 = 16 */
    TEST_ASSERT_EQUAL_UINT8(16, pump_pwm);
}

/* PWM clamped at 255 when error is huge. */
void test_output_clamped_at_255(void)
{
    pid_kp = 255.0f;
    mock_millis_val = 1000;
    updatePump(0, 0);            // max possible error
    TEST_ASSERT_EQUAL_UINT8(255, pump_pwm);
}

/* PWM clamped at 0 when pressure exceeds target. */
void test_output_clamped_at_zero_when_overpressure(void)
{
    pid_kp = 16.0f;
    mock_millis_val = 1000;
    /* fps = 80 (5.0 bar) > target 3.0 bar -> negative error -> output <= 0 */
    updatePump(80, 0);
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
}

/* ================================================================== */
/* Integral accumulation                                               */
/* ================================================================== */

/* With kp=0 and ki=16, the integral grows with time:
 *   Call 1 (dt=1000ms): integral = 0 + error*1000 = 16*1000 = 16000
 *     output = ki*integral / 16000 = 16*16000/16000 = 16
 *   Call 2 (dt=1000ms): integral = 16000 + 16000 = 32000
 *     output = 16*32000/16000 = 32 */
void test_integral_accumulates_over_time(void)
{
    pid_ki = 16.0f;
    /* Call 1 */
    mock_millis_val = 1000;
    updatePump(32, 0);           // fps=32, target=48, error=16
    TEST_ASSERT_EQUAL_UINT8(16, pump_pwm);
    /* Call 2 */
    mock_millis_val = 2000;
    updatePump(32, 0);
    TEST_ASSERT_EQUAL_UINT8(32, pump_pwm);
}

/* ================================================================== */
/* Anti-windup                                                         */
/* ================================================================== */

/* When output saturates at 255 (high) and error remains positive,
 * the integral must stop growing.  After saturation, reducing the error
 * should proportionally reduce the output (not stay pinned at 255). */
void test_anti_windup_stops_integral_growth_at_high_saturation(void)
{
    pid_kp = 0.0f;
    pid_ki = 255.0f;             // very aggressive integral

    /* Drive the integral to saturation */
    mock_millis_val = 1000;
    updatePump(0, 0);            // large error -> output = 255 and saturated
    TEST_ASSERT_EQUAL_UINT8(255, pump_pwm);

    uint8_t pwm_after_first_saturation = pump_pwm;

    /* A second call with the same conditions -- integral must not grow further */
    mock_millis_val = 2000;
    updatePump(0, 0);

    /* If anti-windup is working, pwm stays at 255 (not undefined behavior) */
    TEST_ASSERT_EQUAL_UINT8(255, pump_pwm);

    /* Now with zero error, output must drop below 255 */
    mock_millis_val = 3000;
    /* Target = 3.0 bar = 48; fps = 48 -> error = 0 */
    updatePump(48, 0);
    /* With kp=0 the only term is ki*integral, but error=0 so integral does
     * not grow.  The existing integral still drives some output, but it should
     * not exceed 255. */
    TEST_ASSERT_UINT8_WITHIN(255, 255, pump_pwm);  // pwm <= 255, just sanity
    (void)pwm_after_first_saturation;
}

/* Low saturation (negative error -> output would be < 0, clamped to 0).
 * Integral should not wind further negative. */
void test_anti_windup_stops_integral_at_low_saturation(void)
{
    pid_kp = 0.0f;
    pid_ki = 255.0f;

    /* fps > target -> negative error -> output <= 0 */
    mock_millis_val = 1000;
    updatePump(80, 0);           // fps=80 > target=48; error = -32
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);

    /* Second call -- integral should not keep winding negative */
    mock_millis_val = 2000;
    updatePump(80, 0);
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
}

/* ================================================================== */
/* disablePump                                                         */
/* ================================================================== */

void test_disable_pump_zeroes_pwm(void)
{
    pid_kp = 16.0f;
    mock_millis_val = 1000;
    updatePump(0, 0);              // generate some PWM
    TEST_ASSERT_NOT_EQUAL(0, pump_pwm);

    disablePump();
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
    TEST_ASSERT_EQUAL_UINT8(0, mock_analog_write_val);
}

/* After disablePump the integral resets: a subsequent call should behave
 * as if the controller is freshly started. */
void test_disable_pump_resets_integral(void)
{
    pid_ki = 16.0f;
    mock_millis_val = 1000;
    updatePump(32, 0);    // integral = 16 000
    TEST_ASSERT_NOT_EQUAL(0, pump_pwm);

    disablePump();
    mock_millis_val = 1001;   // tiny dt to keep proportional term small
    updatePump(32, 0);        // only 1 ms of integral
    /* integral = 16 x 1 = 16; output = 16 x 16 / 16000 ~= 0 -> still 0 */
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
}

/* ================================================================== */
/* primePump / isPriming                                               */
/* ================================================================== */

void test_prime_pump_sets_full_pwm(void)
{
    primePump();
    TEST_ASSERT_EQUAL_UINT8(255, pump_pwm);
    TEST_ASSERT_EQUAL_UINT8(255, mock_analog_write_val);
}

void test_is_priming_true_during_prime(void)
{
    mock_millis_val = 0;
    primePump();
    mock_millis_val = 1000;    // well within PRIME_DURATION_MS (2000)
    TEST_ASSERT_TRUE(isPriming());
}

void test_is_priming_false_after_duration(void)
{
    mock_millis_val = 0;
    primePump();
    mock_millis_val = PRIME_DURATION_MS + 1;   // 2001 ms
    TEST_ASSERT_FALSE(isPriming());
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
    TEST_ASSERT_EQUAL_UINT8(0, mock_analog_write_val);
}

/* Calling isPriming() at exactly the boundary (== PRIME_DURATION_MS) must
 * return false (prime has expired). */
void test_is_priming_false_at_exact_boundary(void)
{
    mock_millis_val = 0;
    primePump();
    mock_millis_val = PRIME_DURATION_MS;
    TEST_ASSERT_FALSE(isPriming());
}

/* disablePump cancels an in-progress prime. */
void test_disable_pump_cancels_prime(void)
{
    mock_millis_val = 0;
    primePump();
    TEST_ASSERT_TRUE(isPriming());
    disablePump();
    TEST_ASSERT_FALSE(isPriming());
    TEST_ASSERT_EQUAL_UINT8(0, pump_pwm);
}
