#include "eeprom_map.h"
#include "accel_pump.h"
#include "shift_cut.h"
#include "asf_efi.h"
#include <EEPROM.h>

// ---- Float serialisation (big-endian, same convention as comms.cpp) ---------

static void eepromWriteFloat(int addr, float v)
{
    uint32_t raw;
    memcpy(&raw, &v, 4);
    EEPROM.write(addr,     (raw >> 24) & 0xFF);
    EEPROM.write(addr + 1, (raw >> 16) & 0xFF);
    EEPROM.write(addr + 2, (raw >>  8) & 0xFF);
    EEPROM.write(addr + 3,  raw        & 0xFF);
}

static float eepromReadFloat(int addr)
{
    uint32_t raw = ((uint32_t)EEPROM.read(addr)     << 24)
                 | ((uint32_t)EEPROM.read(addr + 1) << 16)
                 | ((uint32_t)EEPROM.read(addr + 2) <<  8)
                 |  (uint32_t)EEPROM.read(addr + 3);
    float v;
    memcpy(&v, &raw, 4);
    return v;
}

// ---- Default values written on first boot -----------------------------------

static void writeDefaults()
{
    // Injection map: all zeros (no injection until user programs the map)
    for (int i = 0; i < RPM_BINS * TPS_BINS; i++)
        EEPROM.write(EEPROM_ADDR_INJ_MAP + i, 0);

    // PID: reasonable starting values for fuel pressure control
    eepromWriteFloat(EEPROM_ADDR_PID,      20.0f);  // kp
    eepromWriteFloat(EEPROM_ADDR_PID + 4,   1.0f);  // ki
    eepromWriteFloat(EEPROM_ADDR_PID + 8,   0.5f);  // kd

    // Pressure table
    eepromWriteFloat(EEPROM_ADDR_PRESSURE,      2.0f); // low bar
    eepromWriteFloat(EEPROM_ADDR_PRESSURE + 4,  3.0f); // high bar
    EEPROM.write(EEPROM_ADDR_PRESSURE + 8, 0x0B);      // threshold rpm hi: 3000 >> 8
    EEPROM.write(EEPROM_ADDR_PRESSURE + 9, 0xB8);      // threshold rpm lo: 3000 & 0xFF

    // IAT and ET correction: all 256 (= 1.0 in Q8.8, no correction)
    for (uint8_t i = 0; i < IAT_CORR_BINS; i++) {
        EEPROM.write(EEPROM_ADDR_IAT_CORR + i * 2,     1);  // 256 >> 8
        EEPROM.write(EEPROM_ADDR_IAT_CORR + i * 2 + 1, 0);  // 256 & 0xFF
    }
    for (uint8_t i = 0; i < ET_CORR_BINS; i++) {
        EEPROM.write(EEPROM_ADDR_ET_CORR + i * 2,     1);
        EEPROM.write(EEPROM_ADDR_ET_CORR + i * 2 + 1, 0);
    }

    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
}

static void writeAxisDefaults()
{
    for (uint8_t i = 0; i < RPM_BINS; i++) {
        EEPROM.write(EEPROM_ADDR_RPM_AXIS + i * 2,     rpm_axis[i] >> 8);
        EEPROM.write(EEPROM_ADDR_RPM_AXIS + i * 2 + 1, rpm_axis[i] & 0xFF);
    }
    for (uint8_t i = 0; i < TPS_BINS; i++)
        EEPROM.write(EEPROM_ADDR_TPS_AXIS + i, tps_axis[i]);

    EEPROM.write(EEPROM_ADDR_AXIS_MAGIC, EEPROM_AXIS_MAGIC_VALUE);
}

// ---- Public API -------------------------------------------------------------

void loadFromEEPROM()
{
    if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VALUE) {
        writeDefaults();
    }

    // Injection map
    for (uint8_t r = 0; r < RPM_BINS; r++)
        for (uint8_t t = 0; t < TPS_BINS; t++)
            inj_map[r][t] = EEPROM.read(EEPROM_ADDR_INJ_MAP + r * TPS_BINS + t);

    // PID
    pid_kp = eepromReadFloat(EEPROM_ADDR_PID);
    pid_ki = eepromReadFloat(EEPROM_ADDR_PID + 4);
    pid_kd = eepromReadFloat(EEPROM_ADDR_PID + 8);

    // Pressure table
    pressure_low_bar       = eepromReadFloat(EEPROM_ADDR_PRESSURE);
    pressure_high_bar      = eepromReadFloat(EEPROM_ADDR_PRESSURE + 4);
    pressure_threshold_rpm = ((uint16_t)EEPROM.read(EEPROM_ADDR_PRESSURE + 8) << 8)
                           |  (uint16_t)EEPROM.read(EEPROM_ADDR_PRESSURE + 9);

    // Correction tables (uint16_t Q8.8)
    for (uint8_t i = 0; i < IAT_CORR_BINS; i++)
        iat_correction[i] = ((uint16_t)EEPROM.read(EEPROM_ADDR_IAT_CORR + i * 2) << 8)
                          |  (uint16_t)EEPROM.read(EEPROM_ADDR_IAT_CORR + i * 2 + 1);
    for (uint8_t i = 0; i < ET_CORR_BINS; i++)
        et_correction[i]  = ((uint16_t)EEPROM.read(EEPROM_ADDR_ET_CORR  + i * 2) << 8)
                          |  (uint16_t)EEPROM.read(EEPROM_ADDR_ET_CORR  + i * 2 + 1);

    // Axis breakpoints — written by a separate magic so a firmware upgrade
    // that adds this section does not wipe out existing map/PID/correction data.
    if (EEPROM.read(EEPROM_ADDR_AXIS_MAGIC) != EEPROM_AXIS_MAGIC_VALUE) {
        writeAxisDefaults();   // already holds the compile-time defaults; just persist them
    } else {
        for (uint8_t i = 0; i < RPM_BINS; i++)
            rpm_axis[i] = ((uint16_t)EEPROM.read(EEPROM_ADDR_RPM_AXIS + i * 2) << 8)
                        |  (uint16_t)EEPROM.read(EEPROM_ADDR_RPM_AXIS + i * 2 + 1);
        for (uint8_t i = 0; i < TPS_BINS; i++)
            tps_axis[i] = EEPROM.read(EEPROM_ADDR_TPS_AXIS + i);
    }

    // Pump mode — separate magic so upgrading firmware doesn't reset other settings
    if (EEPROM.read(EEPROM_ADDR_PUMP_MODE_MAGIC) != EEPROM_PUMP_MODE_MAGIC_VALUE) {
        EEPROM.write(EEPROM_ADDR_PUMP_MODE, 0);
        EEPROM.write(EEPROM_ADDR_PUMP_MODE_MAGIC, EEPROM_PUMP_MODE_MAGIC_VALUE);
    } else {
        pump_mode_always_on = (EEPROM.read(EEPROM_ADDR_PUMP_MODE) != 0);
    }

    // TPS calibration — separate magic; defaults match previous hardcoded values
    if (EEPROM.read(EEPROM_ADDR_TPS_CAL_MAGIC) != EEPROM_TPS_CAL_MAGIC_VALUE) {
        tps_adc_closed = 30;
        tps_adc_open   = 730;
        saveTpsCalibration();
        EEPROM.write(EEPROM_ADDR_TPS_CAL_MAGIC, EEPROM_TPS_CAL_MAGIC_VALUE);
    } else {
        tps_adc_closed = ((uint16_t)EEPROM.read(EEPROM_ADDR_TPS_CAL)     << 8)
                       |  (uint16_t)EEPROM.read(EEPROM_ADDR_TPS_CAL + 1);
        tps_adc_open   = ((uint16_t)EEPROM.read(EEPROM_ADDR_TPS_CAL + 2) << 8)
                       |  (uint16_t)EEPROM.read(EEPROM_ADDR_TPS_CAL + 3);
    }

    // Accel pump — separate magic; safe to add without forcing re-init of other sections
    if (EEPROM.read(EEPROM_ADDR_ACCEL_PUMP_MAGIC) != EEPROM_ACCEL_PUMP_MAGIC_VALUE) {
        saveAccelPump();
        EEPROM.write(EEPROM_ADDR_ACCEL_PUMP_MAGIC, EEPROM_ACCEL_PUMP_MAGIC_VALUE);
    } else {
        accel_threshold_pct_per_s = ((uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP)     << 8)
                                  |  (uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP + 1);
        accel_extra_us             = ((uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP + 2) << 8)
                                  |  (uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP + 3);
        accel_duration_ms          = ((uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP + 4) << 8)
                                  |  (uint16_t)EEPROM.read(EEPROM_ADDR_ACCEL_PUMP + 5);
    }

    // Shift cut — separate magic; defaults are the values in shift_cut.cpp
    if (EEPROM.read(EEPROM_ADDR_SHIFT_CUT_MAGIC) != EEPROM_SHIFT_CUT_MAGIC_VALUE) {
        saveShiftCut();
        EEPROM.write(EEPROM_ADDR_SHIFT_CUT_MAGIC, EEPROM_SHIFT_CUT_MAGIC_VALUE);
    } else {
        shift_cut_enabled     = EEPROM.read(EEPROM_ADDR_SHIFT_CUT) ? 1 : 0;
        shift_cut_duration_ms = ((uint16_t)EEPROM.read(EEPROM_ADDR_SHIFT_CUT + 1) << 8)
                              |  (uint16_t)EEPROM.read(EEPROM_ADDR_SHIFT_CUT + 2);
        shift_cut_min_rpm     = ((uint16_t)EEPROM.read(EEPROM_ADDR_SHIFT_CUT + 3) << 8)
                              |  (uint16_t)EEPROM.read(EEPROM_ADDR_SHIFT_CUT + 4);

        // Guard against a corrupted cell producing an out-of-range cut length
        if (shift_cut_duration_ms < SHIFT_CUT_MIN_MS) shift_cut_duration_ms = SHIFT_CUT_MIN_MS;
        if (shift_cut_duration_ms > SHIFT_CUT_MAX_MS) shift_cut_duration_ms = SHIFT_CUT_MAX_MS;
    }
}

void saveInjectionMap()
{
    for (uint8_t r = 0; r < RPM_BINS; r++)
        for (uint8_t t = 0; t < TPS_BINS; t++)
            EEPROM.update(EEPROM_ADDR_INJ_MAP + r * TPS_BINS + t, inj_map[r][t]);
}

void savePIDParams()
{
    eepromWriteFloat(EEPROM_ADDR_PID,     pid_kp);
    eepromWriteFloat(EEPROM_ADDR_PID + 4, pid_ki);
    eepromWriteFloat(EEPROM_ADDR_PID + 8, pid_kd);
}

void savePressureTable()
{
    eepromWriteFloat(EEPROM_ADDR_PRESSURE,     pressure_low_bar);
    eepromWriteFloat(EEPROM_ADDR_PRESSURE + 4, pressure_high_bar);
    EEPROM.update(EEPROM_ADDR_PRESSURE + 8, pressure_threshold_rpm >> 8);
    EEPROM.update(EEPROM_ADDR_PRESSURE + 9, pressure_threshold_rpm & 0xFF);
}

void saveIATCorrection()
{
    for (uint8_t i = 0; i < IAT_CORR_BINS; i++) {
        EEPROM.update(EEPROM_ADDR_IAT_CORR + i * 2,     iat_correction[i] >> 8);
        EEPROM.update(EEPROM_ADDR_IAT_CORR + i * 2 + 1, iat_correction[i] & 0xFF);
    }
}

void saveETCorrection()
{
    for (uint8_t i = 0; i < ET_CORR_BINS; i++) {
        EEPROM.update(EEPROM_ADDR_ET_CORR + i * 2,     et_correction[i] >> 8);
        EEPROM.update(EEPROM_ADDR_ET_CORR + i * 2 + 1, et_correction[i] & 0xFF);
    }
}

void savePumpMode()
{
    EEPROM.update(EEPROM_ADDR_PUMP_MODE, pump_mode_always_on ? 1 : 0);
}

void saveTpsCalibration()
{
    EEPROM.update(EEPROM_ADDR_TPS_CAL,     tps_adc_closed >> 8);
    EEPROM.update(EEPROM_ADDR_TPS_CAL + 1, tps_adc_closed & 0xFF);
    EEPROM.update(EEPROM_ADDR_TPS_CAL + 2, tps_adc_open   >> 8);
    EEPROM.update(EEPROM_ADDR_TPS_CAL + 3, tps_adc_open   & 0xFF);
}

void saveAxisBreakpoints()
{
    for (uint8_t i = 0; i < RPM_BINS; i++) {
        EEPROM.update(EEPROM_ADDR_RPM_AXIS + i * 2,     rpm_axis[i] >> 8);
        EEPROM.update(EEPROM_ADDR_RPM_AXIS + i * 2 + 1, rpm_axis[i] & 0xFF);
    }
    for (uint8_t i = 0; i < TPS_BINS; i++)
        EEPROM.update(EEPROM_ADDR_TPS_AXIS + i, tps_axis[i]);
}

void saveAccelPump()
{
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP,     accel_threshold_pct_per_s >> 8);
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP + 1, accel_threshold_pct_per_s & 0xFF);
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP + 2, accel_extra_us >> 8);
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP + 3, accel_extra_us & 0xFF);
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP + 4, accel_duration_ms >> 8);
    EEPROM.update(EEPROM_ADDR_ACCEL_PUMP + 5, accel_duration_ms & 0xFF);
}

void saveShiftCut()
{
    EEPROM.update(EEPROM_ADDR_SHIFT_CUT,     shift_cut_enabled ? 1 : 0);
    EEPROM.update(EEPROM_ADDR_SHIFT_CUT + 1, shift_cut_duration_ms >> 8);
    EEPROM.update(EEPROM_ADDR_SHIFT_CUT + 2, shift_cut_duration_ms & 0xFF);
    EEPROM.update(EEPROM_ADDR_SHIFT_CUT + 3, shift_cut_min_rpm >> 8);
    EEPROM.update(EEPROM_ADDR_SHIFT_CUT + 4, shift_cut_min_rpm & 0xFF);
}
