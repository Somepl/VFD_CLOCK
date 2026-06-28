#include "button_handler.h"
#include <Preferences.h>

enum BtnState {
    BTN_IDLE,
    BTN_PRESSING,
    BTN_PRESSED,
    BTN_LONG_PRESSED
};

struct ButtonState {
    uint8_t pin;
    uint16_t threshold;
    uint16_t baseline;
    BtnState state;
    unsigned long pressTime;
    unsigned long lastReadTime;
    ButtonCallback onShort;
    ButtonCallback onLong;
};

static uint16_t touchHysteresis = TOUCH_HYSTERESIS;
static ButtonState buttons[3];
static bool calibrated = false;

static const uint8_t TOUCH_PINS[BUTTON_COUNT] = {
    TOUCH1_PIN,
    TOUCH2_PIN,
    TOUCH3_PIN
};

static const char* THR_KEYS[BUTTON_COUNT] = {
    PREFS_KEY_TOUCH_THR0,
    PREFS_KEY_TOUCH_THR1,
    PREFS_KEY_TOUCH_THR2
};

static void loadThresholds() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    for (int i = 0; i < BUTTON_COUNT; i++) {
        uint16_t def = TOUCH_THRESHOLD;
        buttons[i].threshold = prefs.getUShort(THR_KEYS[i], def);
    }
    touchHysteresis = prefs.getUShort(PREFS_KEY_TOUCH_HYST, TOUCH_HYSTERESIS);
    prefs.end();
}

void button_init() {
    Serial.println(F("[按键] 初始化开始..."));

    touchSetCycles(TOUCH_MEASURE_CYCLES, TOUCH_SLEEP_CYCLES);
    Serial.printf("[按键] touchSetCycles(measure=%d, sleep=%d)\n",
                  TOUCH_MEASURE_CYCLES, TOUCH_SLEEP_CYCLES);

    loadThresholds();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        buttons[i].pin = TOUCH_PINS[i];
        buttons[i].state = BTN_IDLE;
        buttons[i].pressTime = 0;
        buttons[i].lastReadTime = 0;
        buttons[i].onShort = nullptr;
        buttons[i].onLong = nullptr;
        buttons[i].baseline = 0;
        touchRead(buttons[i].pin);
        Serial.printf("[按键] 按键%d (T%d) 阈值=%d\n",
                      i + 1, TOUCH_PINS[i], buttons[i].threshold);
    }
    Serial.printf("[按键] 滞后=%d\n", touchHysteresis);
    Serial.println(F("[按键] 初始化完成（自动校准将在3秒后进行）"));
}

void button_auto_calibrate() {
    Serial.println(F("[按键] 自动校准开始..."));

    for (int i = 0; i < BUTTON_COUNT; i++) {
        uint32_t sum = 0;
        for (int s = 0; s < 16; s++) {
            sum += touchRead(buttons[i].pin);
            delay(5);
        }
        uint16_t avg = sum / 16;
        buttons[i].baseline = avg;

        uint16_t thr = (avg > TOUCH_PRESS_MARGIN) ? (avg - TOUCH_PRESS_MARGIN) : 5;
        buttons[i].threshold = thr;

        Preferences prefs;
        prefs.begin(PREFS_NAMESPACE, false);
        prefs.putUShort(THR_KEYS[i], thr);
        prefs.end();

        Serial.printf("[按键] 按键%d: 基线=%d, 阈值=%d\n", i + 1, avg, thr);
    }

    calibrated = true;
    Serial.println(F("[按键] 自动校准完成"));
}

static void update_baseline(int idx, uint16_t raw) {
    ButtonState &b = buttons[idx];

    if (raw >= b.baseline) {
        int16_t diff = (int16_t)raw - (int16_t)b.baseline;
        if (diff < 0) diff = -diff;
        if (diff > TOUCH_NOISE_SPIKE) {
            return;
        }
        int32_t newBase = (int32_t)b.baseline * (256 - TOUCH_IIR_ALPHA)
                        + (int32_t)raw * TOUCH_IIR_ALPHA;
        b.baseline = (uint16_t)(newBase / 256);
    }
}

void button_update() {
    unsigned long now = millis();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        ButtonState &b = buttons[i];
        uint16_t raw = touchRead(b.pin);

        update_baseline(i, raw);

        bool touched = (raw < b.threshold);

        switch (b.state) {
        case BTN_IDLE:
            if (touched) {
                b.state = BTN_PRESSING;
                b.pressTime = now;
            }
            break;
        case BTN_PRESSING:
            if (!touched) {
                b.state = BTN_IDLE;
            } else if (now - b.pressTime >= BTN_DEBOUNCE_MS) {
                b.state = BTN_PRESSED;
                Serial.printf("[按键] 按键%d 按下 (raw=%d, base=%d, thr=%d)\n",
                              i + 1, raw, b.baseline, b.threshold);
            }
            break;
        case BTN_PRESSED:
            if (!touched) {
                Serial.printf("[按键] 按键%d 短按\n", i + 1);
                if (b.onShort != nullptr) b.onShort();
                b.state = BTN_IDLE;
            } else if (now - b.pressTime >= BTN_LONG_PRESS_MS) {
                Serial.printf("[按键] 按键%d 长按\n", i + 1);
                if (b.onLong != nullptr) b.onLong();
                b.state = BTN_LONG_PRESSED;
            }
            break;
        case BTN_LONG_PRESSED:
            if (!touched) {
                b.state = BTN_IDLE;
            }
            break;
        }
    }
}

bool button_is_held(ButtonID btn) {
    if (btn >= BUTTON_COUNT) return false;
    return (buttons[btn].state == BTN_PRESSED || buttons[btn].state == BTN_LONG_PRESSED);
}

void button_on_short_press(ButtonID btn, ButtonCallback callback) {
    if (btn < BUTTON_COUNT) buttons[btn].onShort = callback;
}

void button_on_long_press(ButtonID btn, ButtonCallback callback) {
    if (btn < BUTTON_COUNT) buttons[btn].onLong = callback;
}

uint16_t button_get_raw(ButtonID btn) {
    if (btn >= BUTTON_COUNT) return 0;
    return (uint16_t)touchRead(buttons[btn].pin);
}

uint16_t button_get_baseline(ButtonID btn) {
    if (btn >= BUTTON_COUNT) return 0;
    return buttons[btn].baseline;
}

uint16_t button_get_threshold(ButtonID btn) {
    if (btn >= BUTTON_COUNT) return TOUCH_THRESHOLD;
    return buttons[btn].threshold;
}

void button_set_threshold(ButtonID btn, uint16_t value) {
    if (btn >= BUTTON_COUNT) return;
    value = constrain(value, 5, 250);
    buttons[btn].threshold = value;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUShort(THR_KEYS[btn], value);
    prefs.end();
}

uint16_t button_get_hysteresis() {
    return touchHysteresis;
}

void button_set_hysteresis(uint16_t value) {
    value = constrain(value, 1, 50);
    touchHysteresis = value;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUShort(PREFS_KEY_TOUCH_HYST, value);
    prefs.end();
}
