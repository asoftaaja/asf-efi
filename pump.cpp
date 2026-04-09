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

void updatePump(uint8_t fps_eighth_bar, uint16_t rpm_val)
{
    float target_bar = (rpm_val >= pressure_threshold_rpm)
                       ? pressure_high_bar
                       : pressure_low_bar;
    int16_t target = (int16_t)(target_bar * 8.0f + 0.5f);  // convert to 1/8 bar

    int16_t kp = (int16_t)pid_kp;
    int16_t ki = (int16_t)pid_ki;

    uint32_t now   = millis();
    uint16_t dt_ms = (uint16_t)(now - pid_prev_ms);
    if (dt_ms < 1) dt_ms = 1;
    pid_prev_ms = now;

    int16_t error  = target - (int16_t)fps_eighth_bar;          // 1/8 bar
    pid_integral  += (int32_t)error * dt_ms;
    pid_integral   = constrain(pid_integral, -160000L, 160000L); // anti-windup (≈±20 bar·s)

    // output = kp*(error/8) + ki*(integral/8000)
    //        = (kp*error*1000 + ki*integral) / 8000
    int32_t output_scaled = (int32_t)kp * error * 1000L
                          + (int32_t)ki * pid_integral;
    pump_pwm = (uint8_t)constrain(output_scaled / 8000L, 0L, 255L);
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
