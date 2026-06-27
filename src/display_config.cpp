/**
 * display_config.cpp — 数码管 NVS 配置持久化 实现
 */

#include "display_config.h"
#include <Preferences.h>
#include "config.h"

// ============================================================
// 配置变量定义
// ============================================================

uint8_t brightnessPct = DISPLAY_BRIGHTNESS_DEFAULT;

bool nightEnabled = NIGHT_MODE_DEFAULT;
uint8_t nightStart = NIGHT_START_DEFAULT;
uint8_t nightEnd = NIGHT_END_DEFAULT;

uint8_t btn3AnimType = BTN3_ANIM_OFF;
uint8_t btn3AnimId = 0;

// ============================================================
// 持久化实现
// ============================================================

void load_display_config() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    brightnessPct = constrain(prefs.getUChar(PREFS_KEY_BRIGHTNESS, DISPLAY_BRIGHTNESS_DEFAULT), 0, 100);
    nightEnabled = prefs.getBool(PREFS_KEY_NIGHT_EN, NIGHT_MODE_DEFAULT);
    nightStart = constrain(prefs.getUChar(PREFS_KEY_NIGHT_START, NIGHT_START_DEFAULT), 0, 23);
    nightEnd = constrain(prefs.getUChar(PREFS_KEY_NIGHT_END, NIGHT_END_DEFAULT), 0, 23);
    btn3AnimType = constrain(prefs.getUChar(PREFS_KEY_BTN3_TYPE, BTN3_ANIM_OFF), 0, 2);
    btn3AnimId = prefs.getUChar(PREFS_KEY_BTN3_ID, 0);
    prefs.end();
    Serial.printf("[显示] 配置: 亮度=%d%%, 夜间模式=%d (%d:00-%d:00), 按键3动画=%d id=%d\n",
                  brightnessPct, nightEnabled, nightStart, nightEnd, btn3AnimType, btn3AnimId);
}

void save_display_config_brightness() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_KEY_BRIGHTNESS, brightnessPct);
    prefs.end();
}

void save_display_config_night() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(PREFS_KEY_NIGHT_EN, nightEnabled);
    prefs.putUChar(PREFS_KEY_NIGHT_START, nightStart);
    prefs.putUChar(PREFS_KEY_NIGHT_END, nightEnd);
    prefs.end();
}

void save_btn3_anim_config() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_KEY_BTN3_TYPE, btn3AnimType);
    prefs.putUChar(PREFS_KEY_BTN3_ID, btn3AnimId);
    prefs.end();
}
