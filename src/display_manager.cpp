/**
 * display_manager.cpp - 数码管显示管理器 实现
 *
 * 复用用户原有代码中的 *   - num[10] 七段数码管段码表-9 *   - ShiftRegister74HC595<4> 驱动线SPI *   - DS3231 RTC 读写（RTClib *   - 数字翻页动画
 *
 * 改造点： *   - 移除 FreeRTOS 任务，改丽isplay_update() loop() 中调用 *   - 新增 4 种显示模式状态机
 *   - 天气/数字显示时自动格式化
 */

#include "display_manager.h"
#include <Wire.h>
#include <RTClib.h>
#include <ShiftRegister74HC595.h>
#include <Preferences.h>

// ============================================================
// 硬件实例（全局单例）
// ============================================================

static ShiftRegister74HC595<4> sr(PIN_DATA, PIN_CLK, PIN_LATCH);
static RTC_DS3231 rtc;

// ============================================================
// 七段数码管段码表-9// 段位定义：a-g + dp，共阳极（0=亮 1=灭）
// 用户原有代码中的 num[10]
// ============================================================

static const uint8_t SEGMENTS[10] = {
    B00000011,  // 0
    B10011111,  // 1
    B00100101,  // 2
    B00001101,  // 3
    B10011001,  // 4
    B01001001,  // 5
    B01000001,  // 6
    B00011111,  // 7
    B00000001,  // 8
    B00001001   // 9
};

/** 全灭（所有段 = 不亮*/
static const uint8_t SEGMENT_BLANK = B11111111;

// ============================================================
// 天气动画帧数据
// 4 位数码管 × 7段，每帧 = 4 个字节，每字节控制 1 位数码管
// 共阳极：0=亮 1=灭
// 使用用户原有代码中的动画帧：
//   ☀Sunshine (双帧太阳闪烁)
//   多云 ☁️ Love (爱心滚动)
//   🌧Raining (雨滴级联 两阶
//   ❄️ / ⛈️ Love (爱心滚动)
//   默认 Love (爱心滚动)
// ============================================================

/** 全灭 = 所有段熄灭 */
#define ALL_OFF 0xFF

/** Sunshine 双帧太阳（用户原有） */
static const uint8_t ANIM_SUNNY[2][4] = {
    {B01101101, B01100011, B00001111, B01101101},  // sun1：带光芒的太阳
    {B11111111, B01100011, B00001111, B11111111},  // sun2：光芒熄灭
};

/** Raining 雨滴级联（用户原有） */
static const uint8_t ANIM_RAINY[6][4] = {
    {B10111111, B10111111, B10111111, B10111111},  // rain1：左上段
    {B11011111, B11011111, B11011111, B11011111},  // rain2：中间
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
    {B11111011, B11111011, B11111011, B11111011},  // rain3：右下段
    {B11011111, B11011111, B11011111, B11011111},  // rain2：中间
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
};

/** 爱心滚动（用户原有 Love 动画）*/
// love[5] 基础图案：{B11100011, B00000011, B11000111, B01100001, B11111111}
// 6 帧滑动窗口
static const uint8_t LOVE_FRAMES[6][4] = {
    {B11100011, B00000011, B11000111, B01100001},
    {B11111111, B11100011, B00000011, B11000111},
    {B11111111, B11111111, B11100011, B00000011},
    {B01100001, B11111111, B11111111, B11100011},
    {B11000111, B01100001, B11111111, B11111111},
    {B00000011, B11000111, B01100001, B11111111},
};

/** 笑脸 & Smile 眨眼（用户原有） */
static const uint8_t ANIM_SMILE[2][4] = {
    {B11000101, B00111011, B00111011, B11000101},  // 笑脸
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

/** 哭脸 & Sad 眨眼（用户原有） */
static const uint8_t ANIM_SAD[2][4] = {
    {B11010111, B00111001, B00111001, B11010111},  // 哭脸
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

/** 无表情 Nol 眨眼（用户原有） */
static const uint8_t ANIM_NOL[2][4] = {
    {B11111111, B01000100, B01000100, B11111111},  // 无表情
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

// ============================================================
// 显示状态
// ============================================================

static DisplayMode displayMode = DISPLAY_TIME;

// 最后显示的时间数字（用于检测变化触发动画）
static uint8_t lastDigits[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static bool firstRefresh = true;

// 非阻塞翻页动画状态
static bool animRunning = false;
static bool animComplete = false;
static uint8_t animTarget[4];
static uint8_t animCurrent[4];
static int animPos;
static int animFrame;
static unsigned long animLastStep;

// 亮度 & PWM
static uint8_t brightnessPct = DISPLAY_BRIGHTNESS_DEFAULT;
static unsigned long pwmLastToggle = 0;
static bool pwmShowing = true;
static uint8_t lastSegments[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};

static void segs_write(const uint8_t segs[4]) {
    sr.setAll(segs);
    memcpy(lastSegments, segs, 4);
}

// 夜间自动关屏
static bool nightEnabled = NIGHT_MODE_DEFAULT;
static uint8_t nightStart = NIGHT_START_DEFAULT;
static uint8_t nightEnd = NIGHT_END_DEFAULT;
static bool nightWake = false;
static unsigned long nightWakeTime = 0;

// 天气显示计时
static unsigned long weatherStartTime = 0;

// 用户输入的数字（持久存储）
static uint16_t userNumber = 0;

// RTC 是否已初始化（在 display_init 中检测）
static bool rtcReady = false;

    // 网页触发的纯动画展示状态
static uint8_t webAnimType = 0;
static unsigned long webAnimStartTime = 0;
#define WEB_ANIM_DURATION  5000  // 网页动画播放时长(ms)

// 用户自定图案/动画播放状态
static uint8_t userPatternData[4] = {0xFF, 0xFF, 0xFF, 0xFF};
#define PATTERN_DISPLAY_MS  6000  // 单帧图案显示时长(ms)

struct UserAnimFrame {
    uint8_t data[4];
    uint16_t duration;
};
#define USER_ANIM_MAX_FRAMES 30
static UserAnimFrame userAnimBuffer[USER_ANIM_MAX_FRAMES];
static uint8_t userAnimFrameCount = 0;
static uint8_t userAnimFrameIdx = 0;
static unsigned long userAnimFrameStart = 0;

// 天气动画状态（播放完自动切到温度）
#define WEATHER_ANIM_NONE    0
#define WEATHER_ANIM_SUN     1     // Sunshine
#define WEATHER_ANIM_CLOUD   2     // 多云 Love
#define WEATHER_ANIM_RAIN    3     // Raining
#define WEATHER_ANIM_SNOW    4     // Love
#define WEATHER_ANIM_THUNDER 5     // 雷阵Love
#define WEATHER_ANIM_DEFAULT 6     // 默认 Love
static uint8_t weatherAnimType = WEATHER_ANIM_NONE;
static unsigned long weatherAnimStartTime = 0;
static int16_t weatherDisplayTemp = 0;     // 动画结束后显示的温度
static uint8_t animCycleIndex = 0;         // 动画循环索引(按键2切换)
static int16_t lastAnimTemp = 0;           // 最近一次天气温度，循环动画时保持
#define WEATHER_ANIM_DURATION  3000  // 动画播放时长(ms) 延长
// ============================================================
// 内部辅助函数
// ============================================================

/**
 * 启动非阻塞翻页动画 * 对比 lastDigits target，对变化的位逐帧推进
 * 实际帧绘制由 display_anim_tick() 在 loop() 中完成 */
static void anim_start(uint8_t target[4]) {
    animRunning = true;
    animComplete = false;
    animPos = 0;
    animFrame = 0;
    animLastStep = millis();
    memcpy(animTarget, target, 4);

    for (int i = 0; i < 4; i++) {
        animCurrent[i] = (lastDigits[i] <= 9) ? lastDigits[i] : 10;
    }

    // 跳过不需要变化的位
    while (animPos < 4 && animCurrent[animPos] == animTarget[animPos]) {
        animPos++;
    }

    if (animPos >= 4) {
        // 所有位已匹配，无需动画
        animComplete = true;
        animRunning = false;
        memcpy(lastDigits, target, 4);
    }
}

/** 写入 4 位数字，无动画 */
static void write_digits_static(uint8_t d[4]) {
    uint8_t segs[4];
    for (int i = 0; i < 4; i++) {
        segs[i] = (d[i] <= 9) ? SEGMENTS[d[i]] : SEGMENT_BLANK;
    }
    segs_write(segs);
}

/** 全部熄灭 */
static void write_all_off() {
    uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
    segs_write(off);
}

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

/**
 * 将温度值格式化为 4 位显示 * 显示规则：正数 " XX°"（°用特殊段码），负数 "-XX°"
 * 简单策略：温度直接显示为 " XX" 两个数字，摄氏度用 C 表示
 * 实际用 4 位显示：如 " 25"（前两位置空，后两位显示温度） */
static void temperature_to_digits(int16_t temp, uint8_t d[4]) {
    // 温度范围限制：-9 ~ 99
    if (temp < -9) temp = -9;
    if (temp > 99) temp = 99;

    if (temp >= 0) {
        d[0] = 0xFF;  // 空白（不显示，用特殊值标记）
        d[1] = 0xFF;
        d[2] = (temp >= 10) ? (temp / 10) : 0xFF;  // 小于10时十位不显示
        d[3] = temp % 10;
    } else {
        // 负数：显示 "-X" 在最右侧两位
        int16_t absTemp = -temp;
        d[0] = 0xFF;
        d[1] = 0xFF;
        d[2] = 0xFE;       // 0xFE = 负号标记（实际渲染时显示中间横杠）
        d[3] = absTemp;
    }
}

// ============================================================
// 公开接口实现
// ============================================================

// ============================================================
// 加载/保存显示配置（NVS）
// ============================================================

static void load_display_config() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    brightnessPct = constrain(prefs.getUChar(PREFS_KEY_BRIGHTNESS, DISPLAY_BRIGHTNESS_DEFAULT), 0, 100);
    nightEnabled = prefs.getBool(PREFS_KEY_NIGHT_EN, NIGHT_MODE_DEFAULT);
    nightStart = constrain(prefs.getUChar(PREFS_KEY_NIGHT_START, NIGHT_START_DEFAULT), 0, 23);
    nightEnd = constrain(prefs.getUChar(PREFS_KEY_NIGHT_END, NIGHT_END_DEFAULT), 0, 23);
    prefs.end();
    Serial.printf("[显示] 配置: 亮度=%d%%, 夜间模式=%d (%d:00-%d:00)\n",
                  brightnessPct, nightEnabled, nightStart, nightEnd);
}

static void save_display_config_brightness() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_KEY_BRIGHTNESS, brightnessPct);
    prefs.end();
}

static void save_display_config_night() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(PREFS_KEY_NIGHT_EN, nightEnabled);
    prefs.putUChar(PREFS_KEY_NIGHT_START, nightStart);
    prefs.putUChar(PREFS_KEY_NIGHT_END, nightEnd);
    prefs.end();
}

// ============================================================
// 判断当前是否处于夜间时段
// ============================================================

static bool is_night_time(uint8_t currentHour) {
    if (!nightEnabled) return false;
    if (nightStart > nightEnd) {
        // 跨午夜（如 23:00-07:00）
        return (currentHour >= nightStart || currentHour < nightEnd);
    }
    // 同一天（如 01:00-05:00）
    return (currentHour >= nightStart && currentHour < nightEnd);
}

void display_force_off() {
    uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
    sr.setAll(off);
}

void display_init() {
    // 立即熄灭所有段，覆盖 74HC595 构造函数默认的全 LOW 状态
    display_force_off();
    Serial.println(F("[显示] log"));

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
        Serial.println(F("[显示] log"));
    }

    // 初始状态：RTC 正常 -> 等首次刷新显示时间；RTC 异常 -> 立即显示 "----"
    displayMode = DISPLAY_TIME;
    firstRefresh = true;
    if (rtcReady) {
        write_all_off();
    } else {
        uint8_t dash[4] = {B11111101, B11111101, B11111101, B11111101};
        segs_write(dash);
    }

    load_display_config();

    Serial.println(F("[显示] 初始化完成"));
}

void display_update() {
    uint8_t d[4];

    // 夜间自动关屏（仅在 TIME 模式下生效）
    if (nightEnabled && displayMode == DISPLAY_TIME && rtcReady) {
        DateTime now = rtc.now();
        uint8_t h = now.hour();
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

    switch (displayMode) {
    case DISPLAY_TIME:
        if (!rtcReady) {
            uint8_t dash[4] = {B11111101, B11111101, B11111101, B11111101};
            segs_write(dash);
            return;
        }
        {
            DateTime now = rtc.now();
            uint8_t h = now.hour();
            uint8_t m = now.minute();
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
        // --- 优先：检查是否在播天气动画 ---
        if (weatherAnimType != WEATHER_ANIM_NONE) {
            unsigned long elapsed = millis() - weatherAnimStartTime;
            if (elapsed >= WEATHER_ANIM_DURATION) {
                // 动画结束，切换到温度显示
                weatherAnimType = WEATHER_ANIM_NONE;
                userNumber = weatherDisplayTemp;
                weatherStartTime = millis();
                Serial.printf("[显示] 动画结束，显示温度 %d\n", weatherDisplayTemp);
            } else {
                // 播动画帧：根据动画类型查找
                const uint8_t (*frames)[4] = nullptr;
                uint8_t frameCount = 0;
                uint16_t frameMs = 150;

                // 用户原有动画时序
                switch (weatherAnimType) {
                case WEATHER_ANIM_SUN:
                    frames = ANIM_SUNNY; frameCount = 2; frameMs = 400; break;
                case WEATHER_ANIM_RAIN:
                    frames = ANIM_RAINY; frameCount = 6; frameMs = 150; break;
                default:  // 默认 爱心滚动
                    frames = LOVE_FRAMES; frameCount = 6; frameMs = 180; break;
                }

                if (frames != nullptr) {
                    int idx = (elapsed / frameMs) % frameCount;
                    segs_write(frames[idx]);
                }
                return;
            }
        }

        // --- 检查是否超时（15秒自动恢复） ---
        if (millis() - weatherStartTime >= WEATHER_DISPLAY_MS) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;  // 回到时间模式时强制刷新
        } else if ((int16_t)userNumber == -99) {
            // 天气获取失败 -> 显示 "----"
            uint8_t dash[4] = {B11111101, B11111101, B11111101, B11111101};
            segs_write(dash);
        } else {
            int16_t temp = (int16_t)userNumber;
            temperature_to_digits(temp, d);
            // 温度渲染：特殊处理 0xFF（空白）和 0xFE（负号）
            uint8_t segs[4];
            for (int i = 0; i < 4; i++) {
                if (d[i] == 0xFF) {
                    segs[i] = SEGMENT_BLANK;
                } else if (d[i] == 0xFE) {
                    segs[i] = B11111101;  // 只亮中间横杠（g段）
                } else {
                    segs[i] = SEGMENTS[d[i]];
                }
            }
            segs_write(segs);
        }
        break;
    }

    case DISPLAY_NUMBER:
        number_to_digits(userNumber, d);
        write_digits_static(d);  // 静态显示，无动画
        break;

    case DISPLAY_ANIMATION: {
        unsigned long elapsed = millis() - webAnimStartTime;
        if (elapsed >= WEB_ANIM_DURATION) {
            displayMode = DISPLAY_TIME;
            firstRefresh = true;
            break;
        }
        const uint8_t (*frames)[4] = nullptr;
        uint8_t frameCount = 0;
        uint16_t frameMs = 300;

        switch (webAnimType) {
        case WEB_ANIM_SUNSHINE:
            frames = ANIM_SUNNY; frameCount = 2; frameMs = 400; break;
        case WEB_ANIM_RAINING:
            frames = ANIM_RAINY; frameCount = 6; frameMs = 150; break;
        case WEB_ANIM_LOVE:
            frames = LOVE_FRAMES; frameCount = 6; frameMs = 180; break;
        case WEB_ANIM_SMILE:
            frames = ANIM_SMILE; frameCount = 2; frameMs = 300; break;
        case WEB_ANIM_SAD:
            frames = ANIM_SAD; frameCount = 2; frameMs = 300; break;
        case WEB_ANIM_NOL:
            frames = ANIM_NOL; frameCount = 2; frameMs = 300; break;
        }
        if (frames != nullptr) {
            int idx = (elapsed / frameMs) % frameCount;
            segs_write(frames[idx]);
        }
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
            break;
        }
        unsigned long elapsed = millis() - userAnimFrameStart;
        if (elapsed >= userAnimBuffer[userAnimFrameIdx].duration) {
            userAnimFrameIdx++;
            if (userAnimFrameIdx >= userAnimFrameCount) {
                displayMode = DISPLAY_TIME;
                firstRefresh = true;
                break;
            }
            userAnimFrameStart = millis();
        }
        segs_write(userAnimBuffer[userAnimFrameIdx].data);
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
    Serial.printf("[显示] 显示天气温度: %d°\n", temperature);
    display_set_mode(DISPLAY_WEATHER);
}

void display_show_weather_with_anim(const char* weatherText, int16_t temperature) {
    // 根据天气文字选择动画类型（使用用户原有动画）
    weatherAnimType = WEATHER_ANIM_CLOUD;  // 默认雨爱心滚动
    if (strstr(weatherText, "晴") != nullptr) {
        weatherAnimType = WEATHER_ANIM_SUN;  // Sunshine
    } else if (strstr(weatherText, "雨") != nullptr) {
        weatherAnimType = WEATHER_ANIM_RAIN;  // Raining
    }

    weatherDisplayTemp = temperature;
    lastAnimTemp = temperature;
    weatherAnimStartTime = millis();
    Serial.printf("[显示] 天气动画(type=%d) 播3秒 → 温度: %d°\n",
                  weatherAnimType, temperature);
    display_set_mode(DISPLAY_WEATHER);
}

void display_show_web_anim(uint8_t animType) {
    if (animType > 5) animType = 0;
    webAnimType = animType;
    webAnimStartTime = millis();
    const char* names[] = {"Sunshine", "Raining", "Love", "Smile", "Sad", "Nol"};
    Serial.printf("[显示] 网页触发动画: %s\n", names[animType]);
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
    userAnimFrameStart = millis();
    Serial.printf("[显示] 播放用户动画: %d帧\n", userAnimFrameCount);
    display_set_mode(DISPLAY_ANIM_PLAY);
}

void display_cycle_weather_anim() {
    // 3 种动画类型循环
    static const uint8_t animTypes[3] = {
        WEATHER_ANIM_SUN,
        WEATHER_ANIM_CLOUD,
        WEATHER_ANIM_RAIN,
    };

    animCycleIndex = (animCycleIndex + 1) % 3;
    weatherAnimType = animTypes[animCycleIndex];
    weatherAnimStartTime = millis();
    // 保留当前温度，动画结束后继续显示
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
    } else {
        Serial.println(F("[显示] 关闭屏幕"));
        display_set_mode(DISPLAY_OFF);
    }
}

void display_anim_tick() {
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
        // 动画期间全亮度（不做PWM），动画结束fall through PWM
    }

    // --- 亮度 PWM（仅在非翻页动画时生效）---
    if (!animRunning && brightnessPct < 100 && displayMode != DISPLAY_OFF) {
        unsigned long now = micros();
        unsigned long elapsed = now - pwmLastToggle;
        unsigned long period = 5000; // 200Hz
        unsigned long onTime = ((unsigned long)brightnessPct * period) / 100;

        if (pwmShowing && elapsed >= onTime) {
            uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
            sr.setAll(off);
            pwmShowing = false;
            pwmLastToggle = now;
        } else if (!pwmShowing && elapsed >= (period - onTime)) {
            segs_write(lastSegments);
            pwmShowing = true;
            pwmLastToggle = now;
        }
    }
}

void display_set_brightness(uint8_t pct) {
    brightnessPct = constrain(pct, 0, 100);
    if (brightnessPct >= 100) {
        // Restore full brightness immediately
        segs_write(lastSegments);
    }
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
    if (rtcReady) {
        DateTime now = rtc.now();
        hour = now.hour();
        minute = now.minute();
    } else {
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


