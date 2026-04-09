#include "pump.h"
#include "asf_efi.h"

uint8_t pump_pwm = 0;   // last PWM value written to pump output

// PID state
static float    pid_integral  = 0.0f;
static float    pid_prev_err  = 0.0f;
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

void updatePump(uint8_t fps_eighth_bar, uint16_t rpm_val)
{
    float fps_bar_val = fps_eighth_bar / 8.0f;
    float target = (rpm_val >= pressure_threshold_rpm)
                   ? pressure_high_bar
                   : pressure_low_bar;

    uint32_t now = millis();
    float dt = (float)(now - pid_prev_ms) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    pid_prev_ms = now;

    float error      = target - fps_bar_val;
    pid_integral    += error * dt;
    pid_integral     = constrain(pid_integral, -20.0f, 20.0f);  // anti-windup
    float derivative = (error - pid_prev_err) / dt;
    pid_prev_err     = error;

    float output = pid_kp * error + pid_ki * pid_integral + pid_kd * derivative;
    pump_pwm = (uint8_t)constrain(output, 0.0f, 255.0f);
    analogWrite(PIN_PUMP, pump_pwm);
}

void disablePump()
{
    priming = false;
    pid_integral  = 0.0f;
    pid_prev_err  = 0.0f;
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
