/**
 * display_manager.cpp - 数码管显示管理器 实现（协调器）
 *
 * 依赖：display_driver（硬件层）、display_config（NVS）、display_anim（动画引擎）
 */

#include "display_manager.h"
#include <Wire.h>
#include <RTClib.h>
#include <LittleFS.h>
#include "pattern_manager.h"
#include "ntp_sync.h"

// ============================================================
// 硬件实例（模块内全局）
// ============================================================

static RTC_DS3231 rtc;

// ============================================================
// 显示状态（模块内全局）
// ============================================================

static DisplayMode displayMode = DISPLAY_TIME;
static bool rtcReady = false;

// 夜间唤醒
static bool nightWake = false;
static unsigned long nightWakeTime = 0;

// 天气显示计时
static unsigned long weatherStartTime = 0;

// 用户输入的数字
static uint16_t userNumber = 0;

// 闪烁通知
static bool flashActive = false;
static bool flashVisible = true;
static unsigned long flashLastToggle = 0;
#define FLASH_INTERVAL_MS 500

/**
 * 将数字拆分为 4 个数码管 * 支持 0-9999，不足 4 位的左侧补零
 */
static void number_to_digits(uint16_t num, uint8_t d[4]) {
    if (num > 9999) num = 9999;
    d[0] = (num / 1000) % 10;
    d[1] = (num / 100) % 10;
    d[2] = (num / 10) % 10;
    d[3] = num % 10;
}

static bool is_night_time(uint8_t currentHour) {
    if (!nightEnabled) return false;
    if (nightStart > nightEnd) {
        // 跨午夜（如 23:00-07:00）
        return (currentHour >= nightStart || currentHour < nightEnd);
    }
    // 同一天（如 01:00-05:00）
    return (currentHour >= nightStart && currentHour < nightEnd);
}

void display_init() {
    // 硬件层初始化（74HC595 + PWM 定时器）
    display_driver_init();

    Serial.println(F("[显示] 初始化..."));

    // 初始化 I2C（DS3231）
    Wire.begin(I2C_SDA, I2C_SCL);

    // 检测 RTC
    if (rtc.begin()) {
        rtcReady = true;
        if (rtc.lostPower()) {
            Serial.println(F("[显示] 警告：RTC 电池掉电，时间可能不准确"));
        }
        Serial.println(F("[显示] DS3231 RTC 已就绪"));
        // 打印当前时间，方便调试
        DateTime now = rtc.now();
        Serial.printf("[显示] 当前RTC时间: %04d-%02d-%02d %02d:%02d:%02d\n",
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());
    } else {
        Serial.println(F("[显示] 未检测到 DS3231 RTC"));
    }

    // 初始状态：RTC 正常 -> 等首次刷新显示时间；RTC 异常 -> 立即显示 "----"
    displayMode = DISPLAY_TIME;
    firstRefresh = true;
    if (rtcReady) {
        write_all_off();
    } else {
        uint8_t dash[4] = {char_to_segments('-'), char_to_segments('-'), char_to_segments('-'), char_to_segments('-')};
        segs_write(dash);
    }

    load_display_config();
    pwmBrightness = brightnessPct;

    Serial.println(F("[显示] 初始化完成"));
}

// ============================================================
// 内部：获取当前时间（小时/分钟），优先 RTC，失败时回退到软件 RTC
// ============================================================

static bool get_current_hh_mm(uint8_t &hour, uint8_t &minute) {
    if (rtcReady) {
        DateTime now = rtc.now();
        hour = now.hour();
        minute = now.minute();
        return true;
    }
    return sw_rtc_get_hh_mm(hour, minute);
}

void display_update() {
    uint8_t d[4];

    // 夜间自动关屏（仅在 TIME 模式下生效）
    if (nightEnabled && displayMode == DISPLAY_TIME) {
        uint8_t h;
        uint8_t m_unused;
        if (get_current_hh_mm(h, m_unused)) {
            if (is_night_time(h)) {
                if (!nightWake) {
                    display_set_mode(DISPLAY_OFF);
                } else if (millis() - nightWakeTime >= NIGHT_WAKE_MS) {
                    nightWake = false;
                    display_set_mode(DISPLAY_OFF);
                }
            } else {
                nightWake = false;
            }
        }
    }

    switch (displayMode) {
    case DISPLAY_TIME:
        {
            uint8_t h, m;
            if (!get_current_hh_mm(h, m)) {
                // RTC 和软件 RTC 都不可用 → 显示 "----"
                uint8_t dash[4] = {char_to_segments('-'), char_to_segments('-'), char_to_segments('-'), char_to_segments('-')};
                segs_write(dash);
                return;
            }
            uint8_t d[4] = {(uint8_t)(h / 10), (uint8_t)(h % 10), (uint8_t)(m / 10), (uint8_t)(m % 10)};

            if (firstRefresh) {
                firstRefresh = false;
                memcpy(lastDigits, d, 4);
                write_digits_static(d);
            } else if (animRunning) {
                // 动画未结束，且目标时间变化（如跨分钟）→ 重新开始动画
                if (memcmp(animTarget, d, 4) != 0) {
                    anim_start(d);
                }
            } else {
                if (memcmp(lastDigits, d, 4) != 0) {
                    anim_start(d);
                } else {
                    write_digits_static(d);
                }
            }
        }
        break;

    case DISPLAY_WEATHER: {
        // --- 检查天气动画超时 ---
        if (weatherAnimType != WEATHER_ANIM_NONE) {
            unsigned long elapsed = millis() - weatherAnimStartTime;
            if (elapsed >= WEATHER_ANIM_DURATION) {
                // 动画结束，切换到温度显示
                weatherAnimType = WEATHER_ANIM_NONE;
                wthrOvValid = false;
                userNumber = weatherDisplayTemp;
                weatherStartTime = millis();
                Serial.printf("[显示] 动画结束，显示温度 %d\n", weatherDisplayTemp);
            } else {
                return;  // 动画进行中，由 anim_tick 绘制帧
            }
        }

        // --- 检查是否超时（15秒自动恢复） ---
        if (millis() - weatherStartTime >= WEATHER_DISPLAY_MS) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
        } else if ((int16_t)userNumber == -99) {
            // 天气获取失败 -> 显示 "----"
            uint8_t dash[4] = {char_to_segments('-'), char_to_segments('-'), char_to_segments('-'), char_to_segments('-')};
            segs_write(dash);
        } else {
            int16_t temp = (int16_t)userNumber;
            uint8_t segs[4];
            // 管1：零上空白，零下负号（G 段）
            if (temp < 0) {
                segs[0] = char_to_segments('-');
                temp = -temp;
            } else {
                segs[0] = SEGMENT_BLANK;
            }
            // 管2-3：温度数值
            segs[1] = (temp >= 10) ? SEGMENTS[temp / 10] : SEGMENT_BLANK;
            segs[2] = SEGMENTS[temp % 10];
            // 管4：°C（A+F+B+G）
            segs[3] = char_to_segments(0xB0);
            segs_write(segs);
        }
        break;
    }

    case DISPLAY_NUMBER:
        number_to_digits(userNumber, d);
        write_digits_static(d);
        break;

    case DISPLAY_ANIMATION: {
        unsigned long elapsed = millis() - webAnimStartTime;
        if (elapsed >= WEB_ANIM_DURATION) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
        }
        // 动画帧由 anim_tick 绘制
        break;
    }

    case DISPLAY_PATTERN: {
        unsigned long elapsed = millis() - webAnimStartTime;
        if (elapsed >= PATTERN_DISPLAY_MS) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
            break;
        }
        segs_write(userPatternData);
        break;
    }

    case DISPLAY_ANIM_PLAY: {
        if (userAnimFrameCount == 0) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
        }
        // 动画帧由 anim_tick 绘制
        break;
    }

    case DISPLAY_AP: {
        // 管3 'A' (A+F+B+G+E+C), 管4 'P' (A+F+B+G+E)
        uint8_t apSegs[4] = {SEGMENT_BLANK, SEGMENT_BLANK, char_to_segments('A'), char_to_segments('P')};
        segs_write(apSegs);
        break;
    }

    case DISPLAY_OFF:
        write_all_off();
        break;
    }
}

void display_set_mode(DisplayMode mode) {
    if (displayMode == mode) return;

    Serial.printf("[显示] 模式切换: %d -> %d\n", displayMode, mode);
    displayMode = mode;

    if (mode == DISPLAY_TIME) {
        firstRefresh = true;
        animRunning = false;
        memset(lastDigits, 0xFF, 4);
    }
}

DisplayMode display_get_mode() {
    return displayMode;
}

void display_show_ap() {
    Serial.println(F("[显示] AP配网模式"));
    display_set_mode(DISPLAY_AP);
}

void display_show_number(uint16_t number) {
    if (number > 9999) number = 9999;
    userNumber = number;
    Serial.printf("[显示] 设置数字: %d\n", userNumber);
    display_set_mode(DISPLAY_NUMBER);
}

void display_show_weather(int16_t temperature) {
    userNumber = temperature;
    lastAnimTemp = temperature;
    weatherStartTime = millis();
    weatherAnimType = WEATHER_ANIM_NONE;
    Serial.printf("[显示] 显示天气温度: %d\n", temperature);
    display_set_mode(DISPLAY_WEATHER);
}

void display_show_weather_with_anim(const char* weatherText, int16_t temperature) {
    weatherAnimType = WEATHER_ANIM_DEFAULT;  // 默认扫描
    if (strstr(weatherText, "晴") != nullptr) {
        weatherAnimType = WEATHER_ANIM_SUN;
    } else if (strstr(weatherText, "雨") != nullptr) {
        weatherAnimType = WEATHER_ANIM_RAIN;
    } else if (strstr(weatherText, "云") != nullptr ||
               strstr(weatherText, "阴") != nullptr) {
        weatherAnimType = WEATHER_ANIM_CLOUD;
    } else if (strstr(weatherText, "雪") != nullptr) {
        weatherAnimType = WEATHER_ANIM_SNOW;
    } else if (strstr(weatherText, "雷") != nullptr) {
        weatherAnimType = WEATHER_ANIM_THUNDER;
    }

    if (weatherAnimType == WEATHER_ANIM_DEFAULT) {
        // 未匹配天气 -> 直接显示温度，跳过动画
        Serial.printf("[天气] 未匹配动画 (%s)，直接显示温度: %d\n", weatherText, temperature);
        display_show_weather(temperature);
        return;
    }

    // 尝试加载用户覆写
    uint8_t builtinIdx = weatherAnimType <= 6 ? weatherToBuiltinIdx[weatherAnimType] : 9;
    if (!wthr_load_override(builtinIdx)) {
        wthrOvValid = false;
    }

    weatherDisplayTemp = temperature;
    lastAnimTemp = temperature;
    weatherAnimStartTime = millis();
    Serial.printf("[显示] 天气动画(type=%d) 播3秒 -> 温度: %d, %s\n",
                  weatherAnimType, temperature,
                  wthrOvValid ? "使用覆写" : "默认");
    display_set_mode(DISPLAY_WEATHER);
}

void display_show_web_anim(uint8_t animType) {
    if (animType > 5) animType = 0;
    const char* names[] = {"Sunshine", "Raining", "Love", "Smile", "Sad", "Nol"};
    Serial.printf("[显示] 网页触发动画: %s\n", names[animType]);

    // 检查是否有用户覆写
    DynamicJsonDocument overrideDoc(2048);
    if (pm_get_builtin_override(animType, overrideDoc)) {
        JsonArray frames = overrideDoc.as<JsonArray>();
        if (frames.size() > 0) {
            Serial.println(F("[显示] 使用覆写动画"));
            display_play_user_anim(frames);
            return;
        }
    }

    // 无覆写，使用默认
    webAnimType = animType;
    webAnimStartTime = millis();
    display_set_mode(DISPLAY_ANIMATION);
}

void display_show_raw(const uint8_t data[4]) {
    memcpy(userPatternData, data, 4);
    webAnimStartTime = millis();
    Serial.printf("[显示] 显示用户图案: %02X %02X %02X %02X\n",
                  data[0], data[1], data[2], data[3]);
    display_set_mode(DISPLAY_PATTERN);
}

void display_play_user_anim(const JsonArray &frames) {
    userAnimFrameCount = 0;
    userAnimFrameIdx = 0;
    for (JsonVariant f : frames) {
        if (userAnimFrameCount >= USER_ANIM_MAX_FRAMES) break;
        JsonObject frame = f.as<JsonObject>();
        JsonArray dataArr = frame["data"].as<JsonArray>();
        if (dataArr.size() != 4) continue;
        for (int i = 0; i < 4; i++) {
            userAnimBuffer[userAnimFrameCount].data[i] = dataArr[i].as<uint8_t>();
        }
        userAnimBuffer[userAnimFrameCount].duration = frame["duration"].as<uint16_t>();
        if (userAnimBuffer[userAnimFrameCount].duration < 50) {
            userAnimBuffer[userAnimFrameCount].duration = 50;
        }
        userAnimFrameCount++;
    }
    if (userAnimFrameCount > 0) {
        userAnimFrameStart = millis();
        userAnimFrameIdx = 0;
    }
    Serial.printf("[显示] 播放用户动画: %d帧\n", userAnimFrameCount);
    display_set_mode(DISPLAY_ANIM_PLAY);
}

void display_cycle_weather_anim() {
    static const uint8_t animTypes[3] = {
        WEATHER_ANIM_SUN,
        WEATHER_ANIM_CLOUD,
        WEATHER_ANIM_RAIN,
    };

    animCycleIndex = (animCycleIndex + 1) % 3;
    weatherAnimType = animTypes[animCycleIndex];
    weatherAnimStartTime = millis();
    weatherDisplayTemp = (displayMode == DISPLAY_WEATHER) ?
                          (int16_t)userNumber : lastAnimTemp;

    Serial.printf("[显示] 循环动画(type=%d) -> 温度: %d\n",
                  weatherAnimType, weatherDisplayTemp);
    display_set_mode(DISPLAY_WEATHER);
}

void display_toggle_power() {
    if (displayMode == DISPLAY_OFF) {
        if (nightEnabled) {
            display_night_wake();
        } else {
            Serial.println(F("[显示] 打开屏幕"));
            display_set_mode(DISPLAY_TIME);
        }
    } else if (displayMode == DISPLAY_AP) {
        Serial.println(F("[显示] 关闭屏幕（AP模式）"));
        display_set_mode(DISPLAY_OFF);
    } else {
        Serial.println(F("[显示] 关闭屏幕"));
        display_set_mode(DISPLAY_OFF);
    }
}

void display_anim_tick() {
    // --- 闪烁通知模式（最高优先级，冻结一切其他显示操作）---
    if (flashActive) {
        unsigned long now = millis();
        if (now - flashLastToggle >= FLASH_INTERVAL_MS) {
            flashVisible = !flashVisible;
            flashLastToggle = now;
            if (flashVisible) {
                memcpy((void*)pwmSegs, lastSegments, 4);
            } else {
                uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
                memcpy((void*)pwmSegs, off, 4);
            }
        }
        return;
    }

    // --- 翻页动画 ---
    if (animRunning && !animComplete && displayMode == DISPLAY_TIME) {
        unsigned long now = millis();
        if (now - animLastStep >= DISPLAY_ANIM_DELAY) {
            animLastStep = now;

            uint8_t segs[4];
            for (int i = 0; i < 4; i++) {
                if (i == animPos) {
                    segs[i] = SEGMENTS[animFrame];
                } else if (animCurrent[i] <= 9) {
                    segs[i] = SEGMENTS[animCurrent[i]];
                } else {
                    segs[i] = SEGMENT_BLANK;
                }
            }
            segs_write(segs);

            animFrame++;
            if (animFrame >= 10) {
                animCurrent[animPos] = animTarget[animPos];
                animFrame = 0;
                animPos++;
                while (animPos < 4 && animCurrent[animPos] == animTarget[animPos]) {
                    animCurrent[animPos] = animTarget[animPos];
                    animPos++;
                }
                if (animPos >= 4) {
                    animComplete = true;
                    animRunning = false;
                    uint8_t finalSegs[4] = {
                        SEGMENTS[animTarget[0]], SEGMENTS[animTarget[1]],
                        SEGMENTS[animTarget[2]], SEGMENTS[animTarget[3]]
                    };
                    segs_write(finalSegs);
                    memcpy(lastDigits, animTarget, 4);
                }
            }
        }
        return;  // 动画期间全亮度，跳过 PWM
    }

    // --- 天气动画帧 ---
    if (displayMode == DISPLAY_WEATHER && weatherAnimType != WEATHER_ANIM_NONE) {
        unsigned long now = millis();
        unsigned long elapsed = now - weatherAnimStartTime;
        if (elapsed < WEATHER_ANIM_DURATION) {
            if (wthrOvValid && wthrOvCount > 0) {
                // 使用覆写：逐帧推进
                if (now - wthrOvLastMs >= wthrOvDur[wthrOvIdx]) {
                    wthrOvIdx++;
                    if (wthrOvIdx >= wthrOvCount) wthrOvIdx = 0;
                    wthrOvLastMs = now;
                }
                segs_write(wthrOvData[wthrOvIdx]);
            } else {
                const uint8_t (*frames)[4] = nullptr;
                uint8_t frameCount = 0;
                uint16_t frameMs = 150;

                switch (weatherAnimType) {
                case WEATHER_ANIM_SUN:
                    frames = ANIM_SUNNY; frameCount = 2; frameMs = 400; break;
                case WEATHER_ANIM_CLOUD:
                    frames = ANIM_CLOUDY; frameCount = 4; frameMs = 200; break;
                case WEATHER_ANIM_RAIN:
                    frames = ANIM_RAINY; frameCount = 6; frameMs = 150; break;
                case WEATHER_ANIM_SNOW:
                    frames = ANIM_SNOW; frameCount = 4; frameMs = 250; break;
                case WEATHER_ANIM_THUNDER:
                    frames = ANIM_THUNDER; frameCount = 4; frameMs = 200; break;
                default:
                    frames = ANIM_DEFAULT; frameCount = 4; frameMs = 250; break;
                }

                if (frames != nullptr) {
                    int idx = (elapsed / frameMs) % frameCount;
                    segs_write(frames[idx]);
                }
            }
            return;  // 动画期间跳过 PWM
        }
    }

    // --- 网页动画帧 ---
    if (displayMode == DISPLAY_ANIMATION) {
        unsigned long now = millis();
        unsigned long elapsed = now - webAnimStartTime;
        if (elapsed < WEB_ANIM_DURATION) {
            const uint8_t (*frames)[4] = nullptr;
            uint8_t frameCount = 0;
            uint16_t frameMs = 300;

            switch (webAnimType) {
            case WEB_ANIM_SUNSHINE: frames = ANIM_SUNNY;    frameCount = 2; frameMs = 400; break;
            case WEB_ANIM_RAINING:  frames = ANIM_RAINY;    frameCount = 6; frameMs = 150; break;
            case WEB_ANIM_LOVE:     frames = LOVE_FRAMES;   frameCount = 6; frameMs = 180; break;
            case WEB_ANIM_SMILE:    frames = ANIM_SMILE;    frameCount = 2; frameMs = 300; break;
            case WEB_ANIM_SAD:      frames = ANIM_SAD;      frameCount = 2; frameMs = 300; break;
            case WEB_ANIM_NOL:      frames = ANIM_NOL;      frameCount = 2; frameMs = 300; break;
            }
            if (frames != nullptr) {
                int idx = (elapsed / frameMs) % frameCount;
                segs_write(frames[idx]);
            }
            return;  // 动画期间跳过 PWM
        }
    }

    // --- 用户自定义动画帧 ---
    if (displayMode == DISPLAY_ANIM_PLAY) {
        if (userAnimFrameCount == 0 || userAnimFrameIdx >= userAnimFrameCount) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
            return;
        }
        unsigned long now = millis();
        unsigned long elapsed = now - userAnimFrameStart;
        if (elapsed >= userAnimBuffer[userAnimFrameIdx].duration) {
            userAnimFrameIdx++;
            if (userAnimFrameIdx >= userAnimFrameCount) {
                displayMode = DISPLAY_TIME;
                firstRefresh = true;
                return;
            }
            userAnimFrameStart = now;
        }
        segs_write(userAnimBuffer[userAnimFrameIdx].data);
        return;
    }
}

void display_set_brightness(uint8_t pct) {
    brightnessPct = constrain(pct, 0, 100);
    pwmBrightness = brightnessPct;
    save_display_config_brightness();
    Serial.printf("[显示] 亮度设为: %d%%\n", brightnessPct);
}

uint8_t display_get_brightness() {
    return brightnessPct;
}

void display_set_night_config(bool enabled, uint8_t startHour, uint8_t endHour) {
    nightEnabled = enabled;
    nightStart = constrain(startHour, 0, 23);
    nightEnd = constrain(endHour, 0, 23);
    if (!enabled) {
        nightWake = false;
    }
    save_display_config_night();
    Serial.printf("[显示] 夜间模式: %d (%d:00-%d:00)\n", nightEnabled, nightStart, nightEnd);
}

bool display_get_night_enabled() {
    return nightEnabled;
}

uint8_t display_get_night_start() {
    return nightStart;
}

uint8_t display_get_night_end() {
    return nightEnd;
}

void display_night_wake() {
    if (nightEnabled && displayMode == DISPLAY_OFF) {
        nightWake = true;
        nightWakeTime = millis();
        display_set_mode(DISPLAY_TIME);
        Serial.println(F("[显示] 夜间按键唤醒"));
    }
}

void display_get_hh_mm(uint8_t &hour, uint8_t &minute) {
    if (!get_current_hh_mm(hour, minute)) {
        hour = 0;
        minute = 0;
    }
}

float display_get_rtc_temp() {
    if (rtcReady) {
        return rtc.getTemperature();
    }
    return -127.0f;
}

void display_rtc_adjust(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t min, uint8_t sec) {
    if (!rtcReady) {
        Wire.begin(I2C_SDA, I2C_SCL);
        if (!rtc.begin()) {
            Serial.println(F("[显示] RTC 不可用，无法校准"));
            return;
        }
        rtcReady = true;
    }
    rtc.adjust(DateTime(year, month, day, hour, min, sec));
    Serial.printf("[显示] RTC 已校准: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, min, sec);
    firstRefresh = true;
}

// ============================================================
// 闪烁通知模式
// ============================================================

void display_start_flash() {
    if (flashActive) return;
    flashActive = true;
    flashVisible = true;
    flashLastToggle = millis();
    Serial.println(F("[显示] 闪烁通知：开始"));
    memcpy((void*)pwmSegs, lastSegments, 4);
}

void display_stop_flash() {
    if (!flashActive) return;
    flashActive = false;
    flashVisible = true;
    // 立刻恢复上次显示的段码，不等 display_update() 的下一次轮询
    memcpy((void*)pwmSegs, lastSegments, 4);
    Serial.println(F("[显示] 闪烁通知：停止，恢复时钟"));
    display_set_mode(DISPLAY_TIME);
}

bool display_is_flash_active() {
    return flashActive;
}

// ============================================================
// 按键3动画
// ============================================================

void display_set_btn3_anim(uint8_t type, uint8_t id) {
    btn3AnimType = constrain(type, 0, 2);
    btn3AnimId = id;
    save_btn3_anim_config();
    Serial.printf("[显示] 按键3动画: type=%d id=%d\n", btn3AnimType, btn3AnimId);
}

uint8_t display_get_btn3_anim_type() {
    return btn3AnimType;
}

uint8_t display_get_btn3_anim_id() {
    return btn3AnimId;
}

void display_play_btn3_anim() {
    if (btn3AnimType == BTN3_ANIM_OFF) {
        Serial.println(F("[显示] 按键3动画: 未配置"));
        return;
    }

    if (btn3AnimType == BTN3_ANIM_BUILTIN) {
        if (btn3AnimId <= 5) {
            Serial.printf("[显示] 按键3动画: 内置 %d\n", btn3AnimId);
            display_show_web_anim(btn3AnimId);
        } else {
            Serial.println(F("[显示] 按键3动画: 内置ID无效"));
        }
        return;
    }

    if (btn3AnimType == BTN3_ANIM_USER) {
        Serial.printf("[显示] 按键3动画: 用户 %d\n", btn3AnimId);
        if (!LittleFS.begin(false)) {
            Serial.println(F("[显示] LittleFS挂载失败"));
            return;
        }
        File file = LittleFS.open("/animations.json", "r");
        if (!file) {
            Serial.println(F("[显示] 无 animations.json"));
            return;
        }
        StaticJsonDocument<4096> doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        if (err) {
            Serial.println(F("[显示] animations.json 解析失败"));
            return;
        }
        JsonArray list = doc.as<JsonArray>();
        for (JsonVariant v : list) {
            if (v["id"].as<uint8_t>() == btn3AnimId) {
                JsonArray frames = v["frames"].as<JsonArray>();
                if (frames.size() > 0) {
                    display_play_user_anim(frames);
                }
                return;
            }
        }
        Serial.printf("[显示] 按键3动画: 未找到用户动画 id=%d\n", btn3AnimId);
    }
}
