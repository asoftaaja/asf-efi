#include "shift_cut.h"

uint8_t  shift_cut_enabled     = 1;
uint16_t shift_cut_duration_ms = 50;
uint16_t shift_cut_min_rpm     = 3000;

// Internal state
static volatile bool shift_trigger = false;  // set by the ISR, consumed by the main loop
static volatile bool armed         = true;   // false until the switch is seen released again
static bool          cutting       = false;
static uint32_t      cut_start_ms  = 0;

/**
 * @brief Configure the shift sensor input and the ignition cut output.
 *
 * The sensor pin uses the internal pull-up, so the switch only has to pull it
 * to ground. The cut output idles low (ignition enabled).
 */
void initShiftCut()
{
    pinMode(PIN_SHIFT_SENSOR, INPUT_PULLUP);
    pinMode(PIN_IGN_CUT, OUTPUT);
    IGN_CUT_OFF();
    armed         = true;
    shift_trigger = false;
    cutting       = false;
}

/**
 * @brief Sample the shift switch; called once per CKPS pulse from the capture ISR.
 *
 * The crank period acts as the debounce: the switch is read at most once per
 * revolution, and re-arming requires seeing it released, so holding the shift
 * lever down produces exactly one cut.
 *
 * @param current_rpm Engine speed at this pulse, used for the arming threshold.
 */
void sampleShiftSensor(uint16_t current_rpm)
{
    if (!shift_cut_enabled) return;

    if (!SHIFT_PRESSED()) {
        armed = true;
        return;
    }

    if (armed && current_rpm >= shift_cut_min_rpm) {
        armed         = false;
        shift_trigger = true;
    }
}

/**
 * @brief Start and time the ignition cut pulse. Call every main loop iteration.
 *
 * Timer1 cannot be used for this: at prescaler 8 it wraps every 32.768 ms, which
 * is shorter than the maximum cut length, so the pulse is timed against millis().
 *
 * @param now_ms Current millis() value.
 */
void updateShiftCut(uint32_t now_ms)
{
    if (shift_trigger) {
        shift_trigger = false;
        if (!cutting) {
            IGN_CUT_ON();
            cut_start_ms = now_ms;
            cutting      = true;
        }
    }

    if (cutting && (now_ms - cut_start_ms >= shift_cut_duration_ms)) {
        IGN_CUT_OFF();
        cutting = false;
    }
}

/**
 * @brief Returns true while the ignition cut output is asserted.
 */
bool isShiftCutActive()
{
    return cutting;
}

/**
 * @brief Drop the cut output and re-arm. Called when the engine stops.
 */
void resetShiftCut()
{
    IGN_CUT_OFF();
    cutting       = false;
    shift_trigger = false;
    armed         = true;
}
