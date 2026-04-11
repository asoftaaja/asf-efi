#include "comms.h"
#include "accel_pump.h"
#include "asf_efi.h"
#include "pump.h"
#include "eeprom_map.h"

// Maximum payload size: CMD_WRITE_MAP = 1(cmd) + 120(data) = 121 bytes
// CMD_WRITE_AXIS = 1(cmd) + 34(data) = 35 bytes
#define RX_BUF_SIZE 130

// Receive state machine
enum RxState { RX_IDLE, RX_LEN, RX_DATA, RX_CRC };
static RxState  rx_state     = RX_IDLE;
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint8_t  rx_len       = 0;   // expected total bytes (cmd + payload)
static uint8_t  rx_pos       = 0;   // bytes received so far into rx_buf

// ---- CRC-8/SMBUS (poly 0x07, init 0x00) ------------------------------------

static uint8_t calcCRC(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
        }
    }
    return crc;
}

// ---- Packet transmit helpers ------------------------------------------------

static void sendByte(uint8_t b)   { Serial.write(b); }

static void sendPacket(uint8_t cmd, const uint8_t *payload, uint8_t plen)
{
    sendByte(PKT_START);
    sendByte(1 + plen);          // LEN = cmd byte + payload
    sendByte(cmd);
    for (uint8_t i = 0; i < plen; i++) sendByte(payload[i]);

    uint8_t crc_buf[RX_BUF_SIZE];
    crc_buf[0] = cmd;
    for (uint8_t i = 0; i < plen; i++) crc_buf[1 + i] = payload[i];
    sendByte(calcCRC(crc_buf, 1 + plen));
}

static void sendACK()  { sendPacket(CMD_ACK,  nullptr, 0); }
static void sendNACK() { sendPacket(CMD_NACK, nullptr, 0); }

// ---- Float serialisation (big-endian) ---------------------------------------

static void packFloat(uint8_t *buf, float v)
{
    uint32_t raw;
    memcpy(&raw, &v, 4);
    buf[0] = (raw >> 24) & 0xFF;
    buf[1] = (raw >> 16) & 0xFF;
    buf[2] = (raw >>  8) & 0xFF;
    buf[3] =  raw        & 0xFF;
}

static float unpackFloat(const uint8_t *buf)
{
    uint32_t raw = ((uint32_t)buf[0] << 24)
                 | ((uint32_t)buf[1] << 16)
                 | ((uint32_t)buf[2] <<  8)
                 |  (uint32_t)buf[3];
    float v;
    memcpy(&v, &raw, 4);
    return v;
}

// ---- Command dispatcher -----------------------------------------------------

static void dispatchCommand(const uint8_t *buf, uint8_t len)
{
    if (len < 1) { sendNACK(); return; }

    uint8_t cmd     = buf[0];
    const uint8_t *payload = buf + 1;
    uint8_t plen    = len - 1;

    switch (cmd) {

    case CMD_READ_SENSORS:
        sendSensorData();
        break;

    case CMD_WRITE_MAP:
        if (plen != RPM_BINS * TPS_BINS * 2) { sendNACK(); return; }
        for (uint8_t r = 0; r < RPM_BINS; r++)
            for (uint8_t t = 0; t < TPS_BINS; t++) {
                uint8_t idx = (r * TPS_BINS + t) * 2;
                inj_map[r][t] = ((uint16_t)payload[idx] << 8) | payload[idx + 1];
            }
        saveInjectionMap();
        sendACK();
        break;

    case CMD_WRITE_PID:
        if (plen != 12) { sendNACK(); return; }
        pid_kp = unpackFloat(payload);
        pid_ki = unpackFloat(payload + 4);
        pid_kd = unpackFloat(payload + 8);
        savePIDParams();
        sendACK();
        break;

    case CMD_WRITE_PRESSURE:
        if (plen != 10) { sendNACK(); return; }
        pressure_low_bar       = unpackFloat(payload);
        pressure_high_bar      = unpackFloat(payload + 4);
        pressure_threshold_rpm = ((uint16_t)payload[8] << 8) | payload[9];
        savePressureTable();
        sendACK();
        break;

    case CMD_PUMP_PRIME:
        primePump();
        sendACK();
        break;

    case CMD_PUMP_SET:
        if (plen != 1) { sendNACK(); return; }
        pump_manual = (payload[0] != 0);
        if (pump_manual) {
            pump_active = true;
        } else {
            pump_active = false;
            disablePump();
        }
        sendACK();
        break;

    case CMD_PUMP_MODE:
        if (plen != 1) { sendNACK(); return; }
        pump_mode_always_on = (payload[0] != 0);
        savePumpMode();
        sendACK();
        break;

    case CMD_WRITE_IAT_CORR:
        if (plen != IAT_CORR_BINS * 2) { sendNACK(); return; }
        for (uint8_t i = 0; i < IAT_CORR_BINS; i++)
            iat_correction[i] = ((uint16_t)payload[i * 2] << 8) | payload[i * 2 + 1];
        saveIATCorrection();
        sendACK();
        break;

    case CMD_WRITE_ET_CORR:
        if (plen != ET_CORR_BINS * 2) { sendNACK(); return; }
        for (uint8_t i = 0; i < ET_CORR_BINS; i++)
            et_correction[i] = ((uint16_t)payload[i * 2] << 8) | payload[i * 2 + 1];
        saveETCorrection();
        sendACK();
        break;

    case CMD_READ_MAP: {
        uint8_t buf[RPM_BINS * TPS_BINS * 2];
        for (uint8_t r = 0; r < RPM_BINS; r++)
            for (uint8_t t = 0; t < TPS_BINS; t++) {
                uint8_t idx = (r * TPS_BINS + t) * 2;
                buf[idx]     = inj_map[r][t] >> 8;
                buf[idx + 1] = inj_map[r][t] & 0xFF;
            }
        sendPacket(CMD_READ_MAP, buf, sizeof(buf));
        break;
    }

    case CMD_WRITE_AXIS: {
        // Payload: 12 × uint16 RPM (24 bytes) + 5 × uint8 TPS percent (5 bytes) = 29 bytes
        if (plen != RPM_BINS * 2 + TPS_BINS) { sendNACK(); return; }
        for (uint8_t i = 0; i < RPM_BINS; i++)
            rpm_axis[i] = ((uint16_t)payload[i * 2] << 8) | payload[i * 2 + 1];
        for (uint8_t i = 0; i < TPS_BINS; i++)
            tps_axis[i] = payload[RPM_BINS * 2 + i];
        saveAxisBreakpoints();
        sendACK();
        break;
    }

    case CMD_READ_AXIS: {
        uint8_t buf[RPM_BINS * 2 + TPS_BINS];
        for (uint8_t i = 0; i < RPM_BINS; i++) {
            buf[i * 2]     = rpm_axis[i] >> 8;
            buf[i * 2 + 1] = rpm_axis[i] & 0xFF;
        }
        for (uint8_t i = 0; i < TPS_BINS; i++)
            buf[RPM_BINS * 2 + i] = tps_axis[i];
        sendPacket(CMD_READ_AXIS, buf, sizeof(buf));
        break;
    }

    case CMD_READ_CORRECTIONS: {
        uint8_t buf[IAT_CORR_BINS * 2 + ET_CORR_BINS * 2];
        for (uint8_t i = 0; i < IAT_CORR_BINS; i++) {
            buf[i * 2]     = iat_correction[i] >> 8;
            buf[i * 2 + 1] = iat_correction[i] & 0xFF;
        }
        for (uint8_t i = 0; i < ET_CORR_BINS; i++) {
            uint8_t off = IAT_CORR_BINS * 2 + i * 2;
            buf[off]     = et_correction[i] >> 8;
            buf[off + 1] = et_correction[i] & 0xFF;
        }
        sendPacket(CMD_READ_CORRECTIONS, buf, sizeof(buf));
        break;
    }

    case CMD_READ_PUMP_CONFIG: {
        uint8_t buf[23];
        packFloat(buf,      pid_kp);
        packFloat(buf + 4,  pid_ki);
        packFloat(buf + 8,  pid_kd);
        packFloat(buf + 12, pressure_low_bar);
        packFloat(buf + 16, pressure_high_bar);
        buf[20] = pressure_threshold_rpm >> 8;
        buf[21] = pressure_threshold_rpm & 0xFF;
        buf[22] = pump_mode_always_on ? 1 : 0;
        sendPacket(CMD_READ_PUMP_CONFIG, buf, sizeof(buf));
        break;
    }

    case CMD_TPS_CAL_CLOSED:
        tps_adc_closed = analogRead(PIN_TPS);
        saveTpsCalibration();
        sendACK();
        break;

    case CMD_TPS_CAL_OPEN:
        tps_adc_open = analogRead(PIN_TPS);
        saveTpsCalibration();
        sendACK();
        break;

    case CMD_WRITE_ACCEL_PUMP:
        if (plen != 6) { sendNACK(); return; }
        accel_threshold_pct_per_s = ((uint16_t)payload[0] << 8) | payload[1];
        accel_extra_us             = ((uint16_t)payload[2] << 8) | payload[3];
        accel_duration_ms          = ((uint16_t)payload[4] << 8) | payload[5];
        saveAccelPump();
        sendACK();
        break;

    case CMD_READ_ACCEL_PUMP: {
        uint8_t buf[6];
        buf[0] = accel_threshold_pct_per_s >> 8;
        buf[1] = accel_threshold_pct_per_s & 0xFF;
        buf[2] = accel_extra_us >> 8;
        buf[3] = accel_extra_us & 0xFF;
        buf[4] = accel_duration_ms >> 8;
        buf[5] = accel_duration_ms & 0xFF;
        sendPacket(CMD_READ_ACCEL_PUMP, buf, sizeof(buf));
        break;
    }

    default:
        sendNACK();
        break;
    }
}

// ---- Public API -------------------------------------------------------------

void initComms()
{
    Serial.begin(SERIAL_BAUD);
}

void processSerial()
{
    while (Serial.available()) {
        uint8_t byte_in = (uint8_t)Serial.read();

        switch (rx_state) {
        case RX_IDLE:
            if (byte_in == PKT_START) rx_state = RX_LEN;
            break;

        case RX_LEN:
            rx_len   = byte_in;
            rx_pos   = 0;
            if (rx_len == 0 || rx_len > RX_BUF_SIZE) {
                rx_state = RX_IDLE;   // invalid length
            } else {
                rx_state = RX_DATA;
            }
            break;

        case RX_DATA:
            rx_buf[rx_pos++] = byte_in;
            if (rx_pos >= rx_len) rx_state = RX_CRC;
            break;

        case RX_CRC:
            if (byte_in == calcCRC(rx_buf, rx_len)) {
                dispatchCommand(rx_buf, rx_len);
            } else {
                sendNACK();
            }
            rx_state = RX_IDLE;
            break;
        }
    }
}

void printSensorDebug()
{
    Serial.print(F("RPM:"));    Serial.print(rpm);
    Serial.print(F(" TPS:"));   Serial.print(tps); Serial.print(F("%"));
    Serial.print(F(" FPS:"));   Serial.print(fps_sixteenth_bar / 16.0f, 2); Serial.print(F("bar"));
    Serial.print(F(" IAT:"));   Serial.print(iat_degc); Serial.print(F("C("));
    Serial.print(analogRead(PIN_IAT)); Serial.print(F(")"));
    Serial.print(F(" ET:"));    Serial.print(et_degc);  Serial.print(F("C("));
    Serial.print(analogRead(PIN_ET));  Serial.print(F(")"));
    Serial.print(F(" PUMP:"));  Serial.print(pump_active ? F("ON") : F("OFF"));
    Serial.println();
}

void sendSensorData()
{
    uint8_t buf[13];
    // rpm: uint16
    buf[0] = rpm >> 8;
    buf[1] = rpm & 0xFF;
    // tps: uint8, 0–100 percent
    buf[2] = tps;
    // fps: uint8, units = 0.0625 bar (1/16 bar, 0–160)
    buf[3] = fps_sixteenth_bar;
    // iat: int16, value × 10 (0.1 °C resolution)
    int16_t iat_i = (int16_t)((int32_t)iat_degc * 10);
    buf[4]  = (uint8_t)(iat_i >> 8);
    buf[5]  = (uint8_t)(iat_i & 0xFF);
    // et: int16, value × 10
    int16_t et_i = (int16_t)((int32_t)et_degc * 10);
    buf[6]  = (uint8_t)(et_i >> 8);
    buf[7]  = (uint8_t)(et_i & 0xFF);
    // pump active flag
    buf[8] = pump_active ? 1 : 0;
    // bat_v: uint8, 1/16 V per count
    buf[9] = bat_v;
    // pump duty: raw PWM (0–255)
    buf[10] = pump_pwm;
    // injector duty: per-mille (0–1000)
    uint16_t inj_duty = 0;
    if (rpm > 0 && last_pulse_width_us > 0) {
        uint32_t period_us = (rpm >= RPM_SYNC_THRESHOLD)
                             ? 16667UL
                             : 60000000UL / rpm;
        inj_duty = (uint16_t)((uint32_t)last_pulse_width_us * 1000UL / period_us);
        if (inj_duty > 1000) inj_duty = 1000;
    }
    buf[11] = inj_duty >> 8;
    buf[12] = inj_duty & 0xFF;

    sendPacket(CMD_READ_SENSORS, buf, sizeof(buf));
}
