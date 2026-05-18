/* test_injection.cpp
 *
 * Tests for the injection module:
 *   - calculatePulseWidth(): bilinear map interpolation, Q8.8 temperature
 *     corrections, edge clamping, zero map, MAX_PULSE_US cap
 *   - fireInjector(): PORTD bit set, OCR1A programmed, TIMSK1.OCIE1A enabled
 *   - shutoffInjector(): PORTD bit cleared, OCIE1A disabled
 *
 * injection.cpp auto-links (Ceedling finds injection.cpp from injection.h).
 * It defines rpm_axis[], tps_axis[], last_injection_ms, last_pulse_width_us.
 * inj_map[][], iat_correction[], et_correction[] come from test_globals.cpp.
 */

#include "unity.h"
#include "injection.h"     // module under test -- defines RPM_BINS, TPS_BINS, IAT_CORR_BINS, ET_CORR_BINS
#include "sensors.h"       // IAT_BINS, ET_BINS

/* Globals shared across modules */
extern uint8_t  inj_map[RPM_BINS][TPS_BINS];
extern uint16_t iat_correction[IAT_CORR_BINS];
extern uint16_t et_correction[ET_CORR_BINS];

/* rpm_axis and tps_axis are defined in injection.cpp -- accessed through it */
extern uint16_t rpm_axis[RPM_BINS];
extern uint8_t  tps_axis[TPS_BINS];

/* Hardware register mocks */
extern volatile uint8_t  PORTD;
extern volatile uint16_t TCNT1;
extern volatile uint16_t OCR1A;
extern volatile uint8_t  TIMSK1;
extern volatile uint8_t  TIFR1;

/* TIMER1_COMPA_vect is the ISR that closes the injector */
void TIMER1_COMPA_vect(void);

/* ------------------------------------------------------------------ */
static void set_flat_map(uint8_t val)
{
    for (int r = 0; r < RPM_BINS; r++)
        for (int t = 0; t < TPS_BINS; t++)
            inj_map[r][t] = val;
}

static void set_neutral_corrections(void)
{
    for (int i = 0; i < IAT_CORR_BINS; i++) iat_correction[i] = 256;
    for (int i = 0; i < ET_CORR_BINS;  i++) et_correction[i]  = 256;
}

/* ------------------------------------------------------------------ */
void setUp(void)
{
    set_flat_map(0);
    set_neutral_corrections();
    PORTD  = 0;
    TCNT1  = 0;
    OCR1A  = 0;
    TIMSK1 = 0;
    TIFR1  = 0;
    initInjection();
}

void tearDown(void) {}

/* ================================================================== */
/* calculatePulseWidth -- zero map                                      */
/* ================================================================== */

void test_zero_map_returns_zero_pulse_width(void)
{
    set_flat_map(0);
    TEST_ASSERT_EQUAL_UINT16(0, calculatePulseWidth(5000, 50, 20, 80));
}

/* ================================================================== */
/* calculatePulseWidth -- flat map, neutral corrections                 */
/* ================================================================== */

/* Map value 10 -> base PW = 10 x 100 = 1 000 us.
 * With both corrections at 256 (= 1.0) the output is unchanged. */
void test_flat_map_neutral_corrections(void)
{
    set_flat_map(10);
    /* Use RPM and TPS in the middle of the map to avoid interpolation artefacts */
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(1000, pw);
}

/* ================================================================== */
/* calculatePulseWidth -- bilinear interpolation                        */
/* ================================================================== */

/* Set a 2x2 block and verify interpolation at the exact corners first. */
void test_bilinear_exact_corner_lower_left(void)
{
    set_flat_map(0);
    inj_map[0][0] = 10;  // rpm_axis[0], tps_axis[0]
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(1000, pw);
}

void test_bilinear_exact_corner_lower_right(void)
{
    set_flat_map(0);
    inj_map[0][1] = 20;  // rpm_axis[0], tps_axis[1]
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[1], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(2000, pw);
}

/* Midpoint between two adjacent RPM breakpoints with equal TPS.
 * rpm_axis = {1000,4000,...}; midpoint RPM = 2500.
 * inj_map[0][0]=10, inj_map[1][0]=30 -> interpolated = 20 -> 2 000 us. */
void test_bilinear_rpm_midpoint_interpolation(void)
{
    set_flat_map(0);
    inj_map[0][0] = 10;
    inj_map[1][0] = 30;
    /* rpm fraction at 2500 = (2500-1000)/(4000-1000) = 0.5
     * result = 10 + (30-10)x0.5 = 20 -> 2 000 us */
    uint16_t pw = calculatePulseWidth(2500, tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(2000, pw);
}

/* Midpoint along TPS axis. */
void test_bilinear_tps_midpoint_interpolation(void)
{
    set_flat_map(0);
    inj_map[0][0] = 0;
    inj_map[0][1] = 40;   // tps_axis[0]=0, tps_axis[1]=30; mid=15 -> frac=0.5
    /* At TPS=15: (0+40)/2 = 20 -> 2 000 us */
    uint16_t pw = calculatePulseWidth(rpm_axis[0], 15, 20, 80);
    TEST_ASSERT_EQUAL_UINT16(2000, pw);
}

/* Full bilinear: different values in a 2x2 block. */
void test_bilinear_2x2_block_center(void)
{
    set_flat_map(0);
    inj_map[0][0] = 10;  // v00
    inj_map[1][0] = 20;  // v10
    inj_map[0][1] = 30;  // v01
    inj_map[1][1] = 40;  // v11
    /* rpm_axis[0]=1000,rpm_axis[1]=4000; tps_axis[0]=0,tps_axis[1]=30
     * RPM=2500 (frac=0.5), TPS=15 (frac=0.5):
     *   v0 = 10 + (20-10)*0.5 = 15
     *   v1 = 30 + (40-30)*0.5 = 35
     *   r  = 15 + (35-15)*0.5 = 25 -> 2 500 us */
    uint16_t pw = calculatePulseWidth(2500, 15, 20, 80);
    TEST_ASSERT_EQUAL_UINT16(2500, pw);
}

/* ================================================================== */
/* calculatePulseWidth -- boundary clamping                             */
/* ================================================================== */

/* RPM below the lowest breakpoint -> uses first column (clamps to min). */
void test_rpm_below_axis_min_clamps(void)
{
    set_flat_map(0);
    inj_map[0][0] = 10;
    uint16_t pw = calculatePulseWidth(100, tps_axis[0], 20, 80);  // 100 RPM < 1000
    TEST_ASSERT_EQUAL_UINT16(1000, pw);
}

/* RPM above the highest breakpoint -> uses last column (clamps to max). */
void test_rpm_above_axis_max_clamps(void)
{
    set_flat_map(0);
    inj_map[RPM_BINS - 1][0] = 10;
    uint16_t pw = calculatePulseWidth(30000, tps_axis[0], 20, 80);  // >17000
    TEST_ASSERT_EQUAL_UINT16(1000, pw);
}

/* TPS above max clamps to the last TPS column. */
void test_tps_above_axis_max_clamps(void)
{
    set_flat_map(0);
    inj_map[0][TPS_BINS - 1] = 15;
    uint16_t pw = calculatePulseWidth(rpm_axis[0], 110, 20, 80);  // 110% > 100%
    TEST_ASSERT_EQUAL_UINT16(1500, pw);
}

/* ================================================================== */
/* calculatePulseWidth -- temperature corrections (Q8.8)               */
/* ================================================================== */

/* IAT correction of 512 (= 2.0) doubles the base pulse width. */
void test_iat_correction_doubles_pulse_width(void)
{
    set_flat_map(10);   // base = 1 000 us
    /* IAT_CORR_TEMPS = {-20, 0, 20, 40, 70}; set correction at 20 degC = 512 */
    iat_correction[2] = 512;  // index 2 corresponds to 20 degC
    /* With ET correction at 1.0, result = 1000 * 2.0 = 2000 */
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(2000, pw);
}

/* ET correction of 128 (= 0.5) halves the base pulse width. */
void test_et_correction_halves_pulse_width(void)
{
    set_flat_map(10);   // base = 1 000 us
    /* ET_CORR_TEMPS = {0, 25, 50, 80, 100}; set correction at 80 degC = 128 */
    et_correction[3] = 128;  // index 3 corresponds to 80 degC
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(500, pw);
}

/* Both corrections combine multiplicatively. */
void test_both_corrections_combined(void)
{
    set_flat_map(10);   // base = 1 000 us
    iat_correction[2] = 512;  // x2.0 at 20 degC
    et_correction[3]  = 128;  // x0.5 at 80 degC
    /* 1000 x 2.0 x 0.5 = 1000 us */
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(1000, pw);
}

/* Correction interpolation: temperature between two breakpoints.
 * IAT_CORR_TEMPS[0]=-20 (corr=256), IAT_CORR_TEMPS[1]=0 (corr=512);
 * At -10 degC (midpoint): correction = (256+512)/2 = 384.
 * Base PW = 1000us; result = 1000*384/256 = 1500us. */
void test_correction_interpolation_at_midpoint(void)
{
    set_flat_map(10);
    iat_correction[0] = 256;   // -20 degC
    iat_correction[1] = 512;   //   0 degC
    /* -10 degC is the midpoint; fraction = 10/20 = 0.5 -> correction = 384 */
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], -10, 80);
    TEST_ASSERT_EQUAL_UINT16(1500, pw);
}

/* ================================================================== */
/* calculatePulseWidth -- MAX_PULSE_US cap (25 000 us)                 */
/* ================================================================== */

void test_pulse_width_clamped_to_max(void)
{
    set_flat_map(255);          // max map -> 25 500 us base
    iat_correction[2] = 512;   // x 2.0 -> would be 51 000 us
    uint16_t pw = calculatePulseWidth(rpm_axis[0], tps_axis[0], 20, 80);
    TEST_ASSERT_EQUAL_UINT16(MAX_PULSE_US, pw);
}

/* ================================================================== */
/* fireInjector                                                        */
/* ================================================================== */

/* fireInjector must turn on the injector (PORTD bit 4) and program
 * OCR1A = TCNT1 + ticks, then enable OCIE1A. */
void test_fire_injector_sets_portd_and_ocr1a(void)
{
    TCNT1  = 1000;
    PORTD  = 0;
    TIMSK1 = 0;
    fireInjector(500);           // 500 us -> 1000 ticks (x2)
    TEST_ASSERT_BITS(1 << PD4, 1 << PD4, PORTD);      // injector on
    TEST_ASSERT_EQUAL_UINT16(2000, OCR1A);             // 1000 + 1000 ticks
    TEST_ASSERT_BITS(1 << OCIE1A, 1 << OCIE1A, TIMSK1); // interrupt enabled
}

/* Zero pulse width must be a no-op (injector stays off). */
void test_fire_injector_zero_pulse_is_noop(void)
{
    PORTD = 0;
    fireInjector(0);
    TEST_ASSERT_EQUAL_UINT8(0, PORTD & (1 << PD4));
}

/* Pulse width x 2 must never overflow the uint16 counter (clamped to 65000). */
void test_fire_injector_ticks_clamped_at_65000(void)
{
    TCNT1 = 0;
    fireInjector(MAX_PULSE_US);  // 25 000 x 2 = 50 000 ticks < 65 000 -> no clamp
    TEST_ASSERT_EQUAL_UINT16(50000, OCR1A);

    /* Test with a value that would overflow 16 bits */
    TCNT1 = 0;
    fireInjector(33000);         // 33 000 x 2 = 66 000 > 65 000 -> clamped to 65 000
    TEST_ASSERT_EQUAL_UINT16(65000, OCR1A);
}

/* ================================================================== */
/* shutoffInjector                                                     */
/* ================================================================== */

void test_shutoff_injector_clears_portd_and_disables_ocie1a(void)
{
    fireInjector(1000);
    shutoffInjector();
    TEST_ASSERT_EQUAL_UINT8(0, PORTD & (1 << PD4));          // injector off
    TEST_ASSERT_EQUAL_UINT8(0, TIMSK1 & (1 << OCIE1A));     // interrupt disabled
}

/* ================================================================== */
/* TIMER1_COMPA_vect ISR (injector close via compare interrupt)        */
/* ================================================================== */

/* The close ISR disables OCIE1A and turns off the injector. */
void test_compa_isr_closes_injector(void)
{
    fireInjector(1000);
    TEST_ASSERT_BITS(1 << PD4, 1 << PD4, PORTD);      // open
    TIMER1_COMPA_vect();
    TEST_ASSERT_EQUAL_UINT8(0, PORTD & (1 << PD4));   // closed
    TEST_ASSERT_EQUAL_UINT8(0, TIMSK1 & (1 << OCIE1A));
}
