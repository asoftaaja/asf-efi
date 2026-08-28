#include "sensors.h"
#include "ckps.h"
#include "injection.h"
#include "accel_pump.h"
#include "powerband.h"
#include "pump.h"
#include "comms.h"
#include "eeprom_map.h"

// ---- Global state (shared via asf_efi.h) ------------------------------------

uint8_t  tps      = 0;
uint8_t  fps_sixteenth_bar  = 0;
int16_t  iat_degc = 25;
int16_t  et_degc  = 25;
uint8_t  bat_v    = 0;

volatile uint16_t rpm         = 0;
volatile bool     pump_active = false;
bool              pump_manual = false;
bool              pump_mode_always_on = false;

uint8_t inj_map[RPM_BINS][TPS_BINS];
uint16_t iat_correction[IAT_CORR_BINS];
uint16_t et_correction[ET_CORR_BINS];

uint16_t pressure_threshold_rpm;
float    pressure_low_bar;
float    pressure_high_bar;

float    pid_kp, pid_ki, pid_kd;

uint16_t tps_adc_closed = 30;   // ADC count at fully closed throttle (loaded from EEPROM)
uint16_t tps_adc_open   = 730;  // ADC count at fully open throttle (loaded from EEPROM)

// ---- LED blink state --------------------------------------------------------

// Red LED blinks at 10 Hz (toggle every 50 ms) when injection is active
#define RED_BLINK_PERIOD_MS 50

static uint32_t red_blink_ms    = 0;
static bool     red_blink_state = false;

// Injector is considered "active" for LED purposes if it has fired recently
#define INJ_ACTIVE_TIMEOUT_MS 100

static void updateLEDs()
{
    uint32_t now = millis();

    if (now - red_blink_ms >= RED_BLINK_PERIOD_MS) {
        red_blink_ms    = now;
        red_blink_state = !red_blink_state;
    }

    // Green LED: solid on — system alive
    digitalWrite(PIN_LED_GREEN, HIGH);

    // Red LED: blinks at 10 Hz while injector has been firing recently
    bool inj_active = (now - last_injection_ms) < INJ_ACTIVE_TIMEOUT_MS;
    digitalWrite(PIN_LED_RED, (inj_active && red_blink_state) ? HIGH : LOW);
}

// ---- Injection pulse width --------------------------------------------------

/**
 * @brief Compute the pulse width for one injection event.
 *
 * Map interpolation and temperature corrections come from calculatePulseWidth();
 * the powerband multiplier scales that base value, then the accelerator-pump
 * shot is added at full value — a transient enrichment should not be leaned out
 * by the low-load multiplier.
 *
 * @param now_ms  Current millis() timestamp.
 * @return Pulse width in µs, clamped to MAX_PULSE_US.
 */
static uint16_t computeInjectionPulse(uint32_t now_ms)
{
    uint32_t pw = calculatePulseWidth(rpm, tps, iat_degc, et_degc);
    pw = pw * getPowerbandMultiplier() >> 8;   // low-load / powerband scaling
    pw += getAccelPumpExtra(now_ms);
    return (uint16_t)min(pw, (uint32_t)MAX_PULSE_US);
}

// ---- 60 Hz injection scheduler (high-RPM mode) ------------------------------

#define PERIOD_60HZ_MS 16   // ≈ 60 Hz

static uint32_t last_60hz_ms = 0;

static void handle60HzInjection()
{
    uint32_t now = millis();
    if (now - last_60hz_ms >= PERIOD_60HZ_MS) {
        last_60hz_ms = now;
        fireInjector(computeInjectionPulse(now));
    }
}

// ---- Arduino entry points ---------------------------------------------------

void setup()
{
    loadFromEEPROM();

    initCKPS();
    initInjection();
    initPump();
    initComms();

    // Green LED on — system initialised
    digitalWrite(PIN_LED_GREEN, HIGH);
}

void loop()
{
    // 1. Read sensors
    tps      = readTPS();
    updateAccelPump(tps, millis());
    updatePowerband(rpm, tps, getCrankRevs());
    fps_sixteenth_bar  = readFPS();
    iat_degc = readIAT();
    et_degc  = readET();
    bat_v    = readBatV();

    // 2. Safety: shut everything down if engine has stopped
    if (isCKPSTimeout()) {
        shutoffInjector();
        resetCKPS();
        resetPowerband(getCrankRevs());
        if (!pump_manual) {
            disablePump();
            pump_active = false;
        }
    } else {
        // 4. Injection (only when engine is running)
        if (pump_active) {
            if (rpm >= RPM_SYNC_THRESHOLD) {
                // High RPM: fixed 60 Hz injection
                handle60HzInjection();
            } else if (injection_trigger) {
                // Low RPM: synchronised to CKPS pulse (flag set by ISR)
                injection_trigger = false;
                fireInjector(computeInjectionPulse(millis()));
            }
        }
    }

    // 3. Pump control — runs regardless of engine state so manual test mode works
    if (isPriming()) {
        // primePump() already set full power; isPriming() handles the timeout
    } else if (pump_active) {
        if (pump_mode_always_on) {
            pump_pwm = 255;
            analogWrite(PIN_PUMP, 255);
        } else {
            updatePump(fps_sixteenth_bar, rpm);
        }
    }

    // 5. LED indicators
    updateLEDs();

    // 6. Serial communication
    processSerial();

#if 0
    static uint32_t last_debug = 0;
    if (millis() - last_debug >= 500) {
        last_debug = millis();
        printSensorDebug();
    }
#endif

}
