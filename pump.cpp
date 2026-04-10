#include "pump.h"
#include "asf_efi.h"

uint8_t pump_pwm = 0;   // last PWM value written to pump output

// PID state
static int32_t  pid_integral  = 0;    // (1/8 bar) * ms
static uint32_t pid_prev_ms   = 0;

// Priming state
static bool     priming       = false;
static uint32_t prime_end_ms  = 0;

void initPump()
{
    pinMode(PIN_PUMP,      OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    analogWrite(PIN_PUMP, 0);
    digitalWrite(PIN_LED_GREEN, LOW);
}

void updatePump(uint8_t fps_sixteenth_bar, uint16_t rpm_val)
{
    float target_bar = (rpm_val >= pressure_threshold_rpm)
                       ? pressure_high_bar
                       : pressure_low_bar;
    int16_t target = (int16_t)(target_bar * 16.0f + 0.5f);  // convert to 1/16 bar

    int16_t kp = (int16_t)pid_kp;
    int16_t ki = (int16_t)pid_ki;

    uint32_t now   = millis();
    uint16_t dt_ms = (uint16_t)(now - pid_prev_ms);
    if (dt_ms < 1) dt_ms = 1;
    pid_prev_ms = now;

    int16_t error = target - (int16_t)fps_sixteenth_bar;  // 1/16 bar

    // Tentative integral update
    int32_t new_integral = constrain(
        pid_integral + (int32_t)error * dt_ms, -320000L, 320000L);

    // output = kp*(error/16) + ki*(integral/16000)
    //        = (kp*error*1000 + ki*integral) / 16000
    int32_t output_scaled = (int32_t)kp * error * 1000L
                          + (int32_t)ki * new_integral;

    // Conditional integration: don't wind further when output is already saturated
    // in the same direction as the error (pump can't actively lower pressure)
    bool saturated_low  = (output_scaled <= 0L        && error < 0);
    bool saturated_high = (output_scaled >= 4080000L   && error > 0);  // 255*16000
    if (!saturated_low && !saturated_high) {
        pid_integral = new_integral;
    }

    pump_pwm = (uint8_t)constrain(output_scaled / 16000L, 0L, 255L);
    analogWrite(PIN_PUMP, pump_pwm);
}

void disablePump()
{
    priming = false;
    pid_integral = 0;
    pump_pwm = 0;
    analogWrite(PIN_PUMP, 0);
}

void primePump()
{
    priming      = true;
    prime_end_ms = millis() + PRIME_DURATION_MS;
    pump_pwm = 255;
    analogWrite(PIN_PUMP, 255);
}

bool isPriming()
{
    if (priming && millis() >= prime_end_ms) {
        pump_pwm = 0;
        analogWrite(PIN_PUMP, 0);
        priming = false;
    }
    return priming;
}
