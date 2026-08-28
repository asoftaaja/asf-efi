/* test_comms.cpp
 *
 * Tests for the serial communication module:
 *   - CRC-8/SMBUS calculation (same poly 0x07 as the firmware)
 *   - Receive FSM: idle -> len -> data -> crc
 *   - Valid packet -> correct command dispatched, ACK sent
 *   - Bad CRC -> NACK sent, FSM returns to idle
 *   - Unknown command -> NACK
 *   - CMD_WRITE_MAP: map updated in memory, ACK
 *   - CMD_WRITE_PID: pid_kp/ki/kd updated, ACK
 *   - CMD_WRITE_PRESSURE: pressure params updated, ACK
 *   - CMD_PUMP_PRIME / CMD_PUMP_SET / CMD_PUMP_MODE
 *   - CMD_READ_SENSORS: response byte layout
 *   - CMD_READ_MAP / CMD_READ_AXIS
 *   - Wrong payload length -> NACK
 *   - Multiple packets queued and processed in order
 *
 * Only comms.cpp is linked from the source directory.  All symbols that
 * comms.cpp references from other modules are stubbed below.
 */

#include "unity.h"
#include "comms.h"    // auto-links comms.cpp

/* Map / table dimensions -- must match injection.h / sensors.h.
 * Defined here so we don't auto-link injection.cpp or pump.cpp
 * (which would duplicate the stubs defined below). */
#ifndef RPM_BINS
#define RPM_BINS       10
#define TPS_BINS        4
#define IAT_CORR_BINS   5
#define ET_CORR_BINS    5
#endif

/* ---------- Globals from test_globals.cpp (ino-level) ---------- */
extern volatile uint16_t rpm;
extern uint8_t           tps;
extern uint8_t           fps_sixteenth_bar;
extern int16_t           iat_degc, et_degc;
extern uint8_t           bat_v;
extern volatile bool     pump_active;
extern bool              pump_manual, pump_mode_always_on;
extern uint8_t           inj_map[RPM_BINS][TPS_BINS];
extern uint16_t          iat_correction[IAT_CORR_BINS];
extern uint16_t          et_correction[ET_CORR_BINS];
extern float             pid_kp, pid_ki, pid_kd;
extern float             pressure_low_bar, pressure_high_bar;
extern uint16_t          pressure_threshold_rpm;
extern uint16_t          tps_adc_closed, tps_adc_open;
extern uint16_t          mock_analog_read_val;
extern HardwareSerial    Serial;

/* ---------- Per-module stubs (their modules are NOT linked here) ---------- */

/* pump.cpp symbols */
uint8_t pump_pwm = 0;

/* injection.cpp symbols */
volatile uint32_t last_injection_ms   = 0;
volatile uint16_t last_pulse_width_us = 0;

/* injection.cpp also defines rpm_axis and tps_axis */
uint16_t rpm_axis[RPM_BINS] = { 1000, 4000, 7000, 9000, 11000,
                                 12500, 13500, 14500, 15500, 17000 };
uint8_t  tps_axis[TPS_BINS] = { 0, 30, 60, 100 };

/* accel_pump.cpp symbols */
uint16_t accel_threshold_pct_per_s = 50;
uint16_t accel_extra_us             = 500;
uint16_t accel_duration_ms          = 300;

/* shift_cut.cpp symbols */
uint8_t  shift_cut_enabled     = 1;
uint16_t shift_cut_duration_ms = 50;
uint16_t shift_cut_min_rpm     = 3000;
uint16_t shift_cut_lockout_ms  = 500;

/* Function stubs -- track calls for assertions */
static bool prime_pump_called    = false;
static bool disable_pump_called  = false;

void primePump()  { prime_pump_called   = true; }
void disablePump(){ disable_pump_called = true; }

bool isAccelPumpActive()                  { return false; }
void updateAccelPump(uint8_t, uint32_t)   {}
uint16_t getAccelPumpExtra(uint32_t)      { return 0; }

void saveInjectionMap()    {}
void savePIDParams()       {}
void savePressureTable()   {}
void saveIATCorrection()   {}
void saveETCorrection()    {}
void saveAxisBreakpoints() {}
void savePumpMode()        {}
void saveTpsCalibration()  {}
void saveAccelPump()       {}
void saveShiftCut()        {}
void loadFromEEPROM()      {}

void resetShiftCut()       {}
bool isShiftCutActive()    { return false; }

/* ================================================================== */
/* Packet building helpers                                             */
/* ================================================================== */

/* CRC-8/SMBUS: poly 0x07, init 0x00, no reflection */
static uint8_t test_crc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

/* Build a framed packet into out[].  Returns total byte count. */
static uint8_t build_packet(uint8_t *out, uint8_t cmd,
                             const uint8_t *payload, uint8_t plen)
{
    out[0] = PKT_START;
    out[1] = (uint8_t)(1 + plen);
    out[2] = cmd;
    for (uint8_t i = 0; i < plen; i++) out[3 + i] = payload[i];

    uint8_t crc_buf[130];
    crc_buf[0] = cmd;
    for (uint8_t i = 0; i < plen; i++) crc_buf[1 + i] = payload[i];
    out[3 + plen] = test_crc(crc_buf, (uint8_t)(1 + plen));
    return (uint8_t)(4 + plen);
}

static void feed_packet(uint8_t cmd, const uint8_t *payload, uint8_t plen)
{
    uint8_t pkt[140];
    uint8_t len = build_packet(pkt, cmd, payload, plen);
    Serial.push_rx(pkt, len);
}

static void feed_packet_bad_crc(uint8_t cmd, const uint8_t *payload, uint8_t plen)
{
    uint8_t pkt[140];
    uint8_t len = build_packet(pkt, cmd, payload, plen);
    pkt[len - 1] ^= 0xFF;   // corrupt CRC
    Serial.push_rx(pkt, len);
}

/* Pack a float in big-endian order */
static void pack_float_be(uint8_t *buf, float v)
{
    uint32_t raw;
    memcpy(&raw, &v, sizeof(raw));
    buf[0] = (uint8_t)(raw >> 24);
    buf[1] = (uint8_t)(raw >> 16);
    buf[2] = (uint8_t)(raw >>  8);
    buf[3] = (uint8_t) raw;
}

static float unpack_float_be(const uint8_t *buf)
{
    uint32_t raw = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                 | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    float v;
    memcpy(&v, &raw, sizeof(v));
    return v;
}

/* Check that the tx buffer starts with a valid packet of the given command */
static bool tx_has_cmd(uint8_t expected_cmd)
{
    return Serial.tx_len >= 4
        && Serial.tx_buf[0] == PKT_START
        && Serial.tx_buf[2] == expected_cmd;
}

/* ================================================================== */
void setUp(void)
{
    Serial.reset();
    prime_pump_called   = false;
    disable_pump_called = false;
    pump_manual         = false;
    pump_active         = false;
    pump_mode_always_on = false;
    pump_pwm            = 0;
    last_pulse_width_us = 0;
    for (int r = 0; r < RPM_BINS; r++)
        for (int t = 0; t < TPS_BINS; t++)
            inj_map[r][t] = 0;
    for (int i = 0; i < IAT_CORR_BINS; i++) iat_correction[i] = 256;
    for (int i = 0; i < ET_CORR_BINS;  i++) et_correction[i]  = 256;
    pid_kp = 0.0f; pid_ki = 0.0f; pid_kd = 0.0f;
    pressure_low_bar = 3.0f; pressure_high_bar = 4.0f;
    pressure_threshold_rpm = 2000;
    mock_analog_read_val = 512;
}

void tearDown(void) {}

/* ================================================================== */
/* CRC sanity                                                          */
/* ================================================================== */

/* {0x01} -> CRC-8/SMBUS = 0x07 */
void test_crc_known_single_byte(void)
{
    uint8_t data[] = { 0x01 };
    TEST_ASSERT_EQUAL_HEX8(0x07, test_crc(data, 1));
}

/* Empty packet (cmd only = 0x00) -> 0x00 */
void test_crc_zero_byte(void)
{
    uint8_t data[] = { 0x00 };
    TEST_ASSERT_EQUAL_HEX8(0x00, test_crc(data, 1));
}

/* {0xAA, 0x55} must not equal {0x55, 0xAA} (order matters) */
void test_crc_order_dependent(void)
{
    uint8_t d1[] = { 0xAA, 0x55 };
    uint8_t d2[] = { 0x55, 0xAA };
    TEST_ASSERT_NOT_EQUAL(test_crc(d1, 2), test_crc(d2, 2));
}

/* ================================================================== */
/* Receive FSM -- framing                                               */
/* ================================================================== */

void test_valid_packet_no_garbage_response_before_processing(void)
{
    /* Just verifying that tx buffer is clean before processSerial */
    TEST_ASSERT_EQUAL_UINT8(0, Serial.tx_len);
}

void test_bad_start_byte_produces_no_response(void)
{
    uint8_t garbage[] = { 0x55, 0x01, 0x01, 0x07 };
    Serial.push_rx(garbage, sizeof(garbage));
    processSerial();
    TEST_ASSERT_EQUAL_UINT8(0, Serial.tx_len);
}

void test_zero_length_packet_produces_no_response(void)
{
    uint8_t bad[] = { PKT_START, 0x00 };
    Serial.push_rx(bad, sizeof(bad));
    processSerial();
    TEST_ASSERT_EQUAL_UINT8(0, Serial.tx_len);
}

/* ================================================================== */
/* CRC checking                                                        */
/* ================================================================== */

void test_correct_crc_gets_processed(void)
{
    /* CMD_READ_SENSORS: no payload */
    uint8_t payload[] = {};
    feed_packet(CMD_READ_SENSORS, payload, 0);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_READ_SENSORS));
}

void test_bad_crc_sends_nack(void)
{
    uint8_t payload[] = {};
    feed_packet_bad_crc(CMD_READ_SENSORS, payload, 0);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

/* ================================================================== */
/* Unknown command                                                     */
/* ================================================================== */

void test_unknown_command_sends_nack(void)
{
    uint8_t payload[] = {};
    feed_packet(0xFF, payload, 0);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

/* ================================================================== */
/* CMD_READ_SENSORS                                                    */
/* ================================================================== */

void test_cmd_read_sensors_response_contains_rpm(void)
{
    rpm = 3000;
    uint8_t payload[] = {};
    feed_packet(CMD_READ_SENSORS, payload, 0);
    processSerial();

    /* Response layout: [0xAA][LEN][CMD][rpm_hi][rpm_lo]... */
    TEST_ASSERT_EQUAL_HEX8(CMD_READ_SENSORS, Serial.tx_buf[2]);
    uint16_t rx_rpm = ((uint16_t)Serial.tx_buf[3] << 8) | Serial.tx_buf[4];
    TEST_ASSERT_EQUAL_UINT16(3000, rx_rpm);
}

void test_cmd_read_sensors_response_contains_tps_and_fps(void)
{
    tps               = 75;
    fps_sixteenth_bar = 48;   // 3.0 bar
    uint8_t payload[] = {};
    feed_packet(CMD_READ_SENSORS, payload, 0);
    processSerial();
    TEST_ASSERT_EQUAL_UINT8(75, Serial.tx_buf[5]);   // tps at offset 2 of payload
    TEST_ASSERT_EQUAL_UINT8(48, Serial.tx_buf[6]);   // fps at offset 3
}

/* ================================================================== */
/* CMD_WRITE_MAP                                                       */
/* ================================================================== */

void test_cmd_write_map_updates_inj_map_and_acks(void)
{
    uint8_t map[RPM_BINS * TPS_BINS];
    for (int i = 0; i < RPM_BINS * TPS_BINS; i++) map[i] = (uint8_t)(i & 0xFF);
    feed_packet(CMD_WRITE_MAP, map, sizeof(map));
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    /* Check a few cells (row-major: [r][t] -> map[r*TPS_BINS+t]) */
    TEST_ASSERT_EQUAL_UINT8(0, inj_map[0][0]);
    TEST_ASSERT_EQUAL_UINT8(1, inj_map[0][1]);
    TEST_ASSERT_EQUAL_UINT8(TPS_BINS, inj_map[1][0]);
}

void test_cmd_write_map_wrong_length_sends_nack(void)
{
    uint8_t short_map[5] = { 1, 2, 3, 4, 5 };
    feed_packet(CMD_WRITE_MAP, short_map, sizeof(short_map));
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

/* ================================================================== */
/* CMD_WRITE_PID                                                       */
/* ================================================================== */

void test_cmd_write_pid_updates_pid_params_and_acks(void)
{
    uint8_t payload[12];
    pack_float_be(payload,     1.5f);   // kp
    pack_float_be(payload + 4, 0.3f);   // ki
    pack_float_be(payload + 8, 0.0f);   // kd
    feed_packet(CMD_WRITE_PID, payload, sizeof(payload));
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, pid_kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3f, pid_ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid_kd);
}

void test_cmd_write_pid_wrong_length_sends_nack(void)
{
    uint8_t payload[8] = {};
    feed_packet(CMD_WRITE_PID, payload, sizeof(payload));
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

/* ================================================================== */
/* CMD_WRITE_PRESSURE                                                  */
/* ================================================================== */

void test_cmd_write_pressure_updates_params_and_acks(void)
{
    uint8_t payload[10];
    pack_float_be(payload,     2.5f);   // low_bar
    pack_float_be(payload + 4, 4.5f);   // high_bar
    payload[8] = 0x0B;                  // threshold RPM high byte
    payload[9] = 0xB8;                  // threshold RPM low byte (= 3000)
    feed_packet(CMD_WRITE_PRESSURE, payload, sizeof(payload));
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, pressure_low_bar);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, pressure_high_bar);
    TEST_ASSERT_EQUAL_UINT16(3000, pressure_threshold_rpm);
}

/* ================================================================== */
/* CMD_PUMP_PRIME                                                      */
/* ================================================================== */

void test_cmd_pump_prime_calls_primepump_and_acks(void)
{
    uint8_t payload[] = {};
    feed_packet(CMD_PUMP_PRIME, payload, 0);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_TRUE(prime_pump_called);
}

/* ================================================================== */
/* CMD_PUMP_SET                                                        */
/* ================================================================== */

void test_cmd_pump_set_on_enables_manual_mode(void)
{
    uint8_t payload[] = { 1 };
    feed_packet(CMD_PUMP_SET, payload, 1);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_TRUE(pump_manual);
    TEST_ASSERT_TRUE(pump_active);
}

void test_cmd_pump_set_off_disables_manual_mode(void)
{
    pump_manual = true;
    uint8_t payload[] = { 0 };
    feed_packet(CMD_PUMP_SET, payload, 1);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_FALSE(pump_manual);
    TEST_ASSERT_FALSE(pump_active);
    TEST_ASSERT_TRUE(disable_pump_called);
}

void test_cmd_pump_set_wrong_length_sends_nack(void)
{
    uint8_t payload[] = {};
    feed_packet(CMD_PUMP_SET, payload, 0);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

/* ================================================================== */
/* CMD_PUMP_MODE                                                       */
/* ================================================================== */

void test_cmd_pump_mode_sets_always_on(void)
{
    uint8_t payload[] = { 1 };
    feed_packet(CMD_PUMP_MODE, payload, 1);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_TRUE(pump_mode_always_on);
}

void test_cmd_pump_mode_sets_pid(void)
{
    pump_mode_always_on = true;
    uint8_t payload[] = { 0 };
    feed_packet(CMD_PUMP_MODE, payload, 1);
    processSerial();
    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_FALSE(pump_mode_always_on);
}

/* ================================================================== */
/* CMD_READ_MAP                                                        */
/* ================================================================== */

void test_cmd_read_map_returns_current_map(void)
{
    inj_map[2][1] = 42;
    uint8_t payload[] = {};
    feed_packet(CMD_READ_MAP, payload, 0);
    processSerial();

    TEST_ASSERT_EQUAL_HEX8(CMD_READ_MAP, Serial.tx_buf[2]);
    /* Map payload starts at byte 3; [2][1] is at row-major offset 2*TPS_BINS+1 */
    uint8_t offset = (uint8_t)(3 + 2 * TPS_BINS + 1);
    TEST_ASSERT_EQUAL_UINT8(42, Serial.tx_buf[offset]);
}

/* ================================================================== */
/* CMD_WRITE_IAT_CORR / CMD_WRITE_ET_CORR                             */
/* ================================================================== */

void test_cmd_write_iat_corr_updates_table_and_acks(void)
{
    uint8_t payload[IAT_CORR_BINS * 2];
    for (int i = 0; i < IAT_CORR_BINS; i++) {
        uint16_t v = (uint16_t)(300 + i);  // arbitrary Q8.8 values
        payload[i * 2]     = (uint8_t)(v >> 8);
        payload[i * 2 + 1] = (uint8_t)(v & 0xFF);
    }
    feed_packet(CMD_WRITE_IAT_CORR, payload, sizeof(payload));
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_EQUAL_UINT16(300, iat_correction[0]);
    TEST_ASSERT_EQUAL_UINT16(304, iat_correction[4]);
}

void test_cmd_write_et_corr_updates_table_and_acks(void)
{
    uint8_t payload[ET_CORR_BINS * 2];
    for (int i = 0; i < ET_CORR_BINS; i++) {
        uint16_t v = (uint16_t)(200 + i);
        payload[i * 2]     = (uint8_t)(v >> 8);
        payload[i * 2 + 1] = (uint8_t)(v & 0xFF);
    }
    feed_packet(CMD_WRITE_ET_CORR, payload, sizeof(payload));
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_EQUAL_UINT16(200, et_correction[0]);
    TEST_ASSERT_EQUAL_UINT16(204, et_correction[4]);
}

/* ================================================================== */
/* Multiple packets processed in one processSerial() call             */
/* ================================================================== */

void test_two_packets_processed_sequentially(void)
{
    /* First: CMD_PUMP_PRIME -> ACK */
    uint8_t p1[] = {};
    feed_packet(CMD_PUMP_PRIME, p1, 0);

    /* Second: CMD_PUMP_MODE with value 1 -> ACK */
    uint8_t p2[] = { 1 };
    feed_packet(CMD_PUMP_MODE, p2, 1);

    processSerial();

    /* Both packets handled; two ACKs in tx buffer.
     * Each ACK packet is: [0xAA][0x01][0x06][CRC] = 4 bytes. */
    TEST_ASSERT_EQUAL_UINT8(8, Serial.tx_len);
    TEST_ASSERT_EQUAL_HEX8(PKT_START, Serial.tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_ACK,   Serial.tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(PKT_START, Serial.tx_buf[4]);
    TEST_ASSERT_EQUAL_HEX8(CMD_ACK,   Serial.tx_buf[6]);
}

/* A bad-CRC packet followed by a valid packet: NACK then ACK. */
void test_bad_crc_then_valid_packet(void)
{
    uint8_t p[] = {};
    feed_packet_bad_crc(CMD_READ_SENSORS, p, 0);   // -> NACK
    feed_packet(CMD_PUMP_PRIME, p, 0);             // -> ACK

    processSerial();

    TEST_ASSERT_EQUAL_HEX8(CMD_NACK, Serial.tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(CMD_ACK,  Serial.tx_buf[6]);
}

/* ================================================================== */
/* CMD_READ_AXIS                                                       */
/* ================================================================== */

void test_cmd_read_axis_returns_current_axes(void)
{
    /* Set a known RPM axis value */
    rpm_axis[0] = 1234;
    uint8_t payload[] = {};
    feed_packet(CMD_READ_AXIS, payload, 0);
    processSerial();

    TEST_ASSERT_EQUAL_HEX8(CMD_READ_AXIS, Serial.tx_buf[2]);
    /* First two payload bytes are rpm_axis[0] big-endian */
    uint16_t v = ((uint16_t)Serial.tx_buf[3] << 8) | Serial.tx_buf[4];
    TEST_ASSERT_EQUAL_UINT16(1234, v);
    rpm_axis[0] = 1000;   // restore
}

/* ================================================================== */
/* CMD_WRITE_ACCEL_PUMP / CMD_READ_ACCEL_PUMP                         */
/* ================================================================== */

void test_cmd_write_accel_pump_updates_params_and_acks(void)
{
    uint8_t payload[6];
    payload[0] = 0x01; payload[1] = 0xF4;   // threshold = 500 %/s
    payload[2] = 0x03; payload[3] = 0xE8;   // extra_us   = 1000
    payload[4] = 0x01; payload[5] = 0x2C;   // duration   = 300 ms
    feed_packet(CMD_WRITE_ACCEL_PUMP, payload, 6);
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_EQUAL_UINT16(500,  accel_threshold_pct_per_s);
    TEST_ASSERT_EQUAL_UINT16(1000, accel_extra_us);
    TEST_ASSERT_EQUAL_UINT16(300,  accel_duration_ms);
}

void test_cmd_read_accel_pump_returns_current_params(void)
{
    accel_threshold_pct_per_s = 500;
    accel_extra_us             = 1000;
    accel_duration_ms          = 300;
    uint8_t payload[] = {};
    feed_packet(CMD_READ_ACCEL_PUMP, payload, 0);
    processSerial();

    TEST_ASSERT_EQUAL_HEX8(CMD_READ_ACCEL_PUMP, Serial.tx_buf[2]);
    uint16_t thr = ((uint16_t)Serial.tx_buf[3] << 8) | Serial.tx_buf[4];
    TEST_ASSERT_EQUAL_UINT16(500, thr);
}

/* ================================================================== */
/* CMD_WRITE_SHIFT_CUT / CMD_READ_SHIFT_CUT                           */
/* ================================================================== */

void test_cmd_write_shift_cut_updates_params_and_acks(void)
{
    uint8_t payload[7];
    payload[0] = 0x01;                      // enabled
    payload[1] = 0x00; payload[2] = 0x50;   // duration = 80 ms
    payload[3] = 0x0F; payload[4] = 0xA0;   // min RPM  = 4000
    payload[5] = 0x02; payload[6] = 0xEE;   // lockout  = 750 ms
    feed_packet(CMD_WRITE_SHIFT_CUT, payload, 7);
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_ACK));
    TEST_ASSERT_EQUAL_UINT8(1,     shift_cut_enabled);
    TEST_ASSERT_EQUAL_UINT16(80,   shift_cut_duration_ms);
    TEST_ASSERT_EQUAL_UINT16(4000, shift_cut_min_rpm);
    TEST_ASSERT_EQUAL_UINT16(750,  shift_cut_lockout_ms);
}

/* Duration outside SHIFT_CUT_MIN_MS..SHIFT_CUT_MAX_MS must be rejected
 * outright rather than silently clamped, so the tuner sees the error. */
void test_cmd_write_shift_cut_rejects_out_of_range_duration(void)
{
    shift_cut_duration_ms = 50;

    uint8_t payload[7];
    payload[0] = 0x01;
    payload[1] = 0x00; payload[2] = 0xC8;   // 200 ms -> too long
    payload[3] = 0x0B; payload[4] = 0xB8;
    payload[5] = 0x01; payload[6] = 0xF4;   // 500 ms lockout, in range
    feed_packet(CMD_WRITE_SHIFT_CUT, payload, 7);
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
    TEST_ASSERT_EQUAL_UINT16(50, shift_cut_duration_ms);   // unchanged
}

void test_cmd_write_shift_cut_rejects_out_of_range_lockout(void)
{
    shift_cut_lockout_ms = 500;

    uint8_t payload[7];
    payload[0] = 0x01;
    payload[1] = 0x00; payload[2] = 0x32;   // 50 ms duration, in range
    payload[3] = 0x0B; payload[4] = 0xB8;
    payload[5] = 0x00; payload[6] = 0xC8;   // 200 ms lockout -> too short
    feed_packet(CMD_WRITE_SHIFT_CUT, payload, 7);
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
    TEST_ASSERT_EQUAL_UINT16(500, shift_cut_lockout_ms);   // unchanged
}

void test_cmd_write_shift_cut_rejects_wrong_length(void)
{
    uint8_t payload[5] = { 1, 0, 50, 0, 0 };
    feed_packet(CMD_WRITE_SHIFT_CUT, payload, 5);
    processSerial();

    TEST_ASSERT_TRUE(tx_has_cmd(CMD_NACK));
}

void test_cmd_read_shift_cut_returns_current_params(void)
{
    shift_cut_enabled     = 1;
    shift_cut_duration_ms = 80;
    shift_cut_min_rpm     = 4000;
    shift_cut_lockout_ms  = 750;
    uint8_t payload[] = {};
    feed_packet(CMD_READ_SHIFT_CUT, payload, 0);
    processSerial();

    TEST_ASSERT_EQUAL_HEX8(CMD_READ_SHIFT_CUT, Serial.tx_buf[2]);
    TEST_ASSERT_EQUAL_UINT8(1, Serial.tx_buf[3]);
    uint16_t dur     = ((uint16_t)Serial.tx_buf[4] << 8) | Serial.tx_buf[5];
    uint16_t min_rpm = ((uint16_t)Serial.tx_buf[6] << 8) | Serial.tx_buf[7];
    uint16_t lockout = ((uint16_t)Serial.tx_buf[8] << 8) | Serial.tx_buf[9];
    TEST_ASSERT_EQUAL_UINT16(80,   dur);
    TEST_ASSERT_EQUAL_UINT16(4000, min_rpm);
    TEST_ASSERT_EQUAL_UINT16(750,  lockout);
}
