/**
 * display_manager.cpp - 数码管显示管理器 实现
 *
 * 复用用户原有代码中的   - num[10] 七段数码管段码表0-9   - ShiftRegister74HC595<4> 驱动3线SPI   - DS3231 RTC 读写（RTClib）   - 数字翻页动画
 *
 * 改造点：   - 移除 FreeRTOS 任务，改在 display_update() loop() 中调用   - 新增 4 种显示模式状态机
 *   - 天气/数字显示时自动格式化
 */

#include "display_manager.h"
#include <Wire.h>
#include <RTClib.h>
#include <ShiftRegister74HC595.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "pattern_manager.h"
#include <driver/gpio.h>

// ============================================================
// 硬件实例（全局单例）
// ============================================================

static ShiftRegister74HC595<4> sr(PIN_DATA, PIN_CLK, PIN_LATCH);
static RTC_DS3231 rtc;

// ============================================================
// 七段数码管段码表0-9 + H段
// 段位定义：a-g + h，共阳极（0=亮 1=灭）
// 用户原有代码中的 num[10]
// 位映射: bit7=A  bit6=B  bit5=C  bit4=D  bit3=E  bit2=F  bit1=G  bit0=H
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

/** 全灭（所有段 = 不亮）*/
static const uint8_t SEGMENT_BLANK = B11111111;

// ============================================================
// 天气动画帧数据
// 4 位数码管 x 7段+h，每帧 = 4 个字节，每字节控制 1 位数码管
// 共阳极：0=亮 1=灭
// ============================================================

/** 全灭 = 所有段熄灭 */
#define ALL_OFF 0xFF

/** Sunshine 双帧太阳（用户原有，H 段在光芒帧亮） */
static const uint8_t ANIM_SUNNY[2][4] = {
    {B01101100, B01100010, B00001110, B01101100},  // sun1：带光芒的太阳 + H亮
    {B11111111, B01100011, B00001111, B11111111},  // sun2：光芒熄灭 + H灭
};

/** Raining 雨滴级联（用户原有，H 段在中间帧亮起模拟水花） */
static const uint8_t ANIM_RAINY[6][4] = {
    {B10111111, B10111111, B10111111, B10111111},  // rain1：左上段
    {B11011110, B11011110, B11011110, B11011110},  // rain2：中间 + H亮（水花）
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
    {B11111011, B11111011, B11111011, B11111011},  // rain3：右下段
    {B11011110, B11011110, B11011110, B11011110},  // rain4：中间 + H亮
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
};

/** 爱心滚动（用户原有 Love 动画）*/
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

/** 多云飘移 4 帧 - 云团从右向左移动，H 段在云团右缘亮起 */
static const uint8_t ANIM_CLOUDY[4][4] = {
    {B01111110, B00111111, B11111111, B11111111},  // 云在左 (位0-1) + H亮
    {B11111111, B01111110, B00111111, B11111111},  // 云在左中 (位1-2) + H亮
    {B11111111, B11111111, B01111110, B00111111},  // 云在右中 (位2-3) + H亮
    {B11111111, B11111111, B11111111, B11111111},  // 全灭（间歇）
};

/** 雪花飘落 4 帧 - H 段在不同位闪烁模拟雪花 */
static const uint8_t ANIM_SNOW[4][4] = {
    {B11111110, B11111111, B11111111, B11111111},  // 雪在位0
    {B11111111, B11111110, B11111111, B11111111},  // 雪在位1
    {B11111111, B11111111, B11111110, B11111111},  // 雪在位2
    {B11111111, B11111111, B11111111, B11111110},  // 雪在位3
};

/** 雷阵雨闪电 4 帧 - 全亮闪烁模拟闪电 */
static const uint8_t ANIM_THUNDER[4][4] = {
    {B00000000, B00000000, B00000000, B00000000},  // 全亮（含H）→ 闪电闪白
    {B00000001, B00000001, B00000001, B00000001},  // A~G亮、H灭 → 闪电稍弱
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 全灭（黑暗）
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 全灭（等待）
};

/** 默认扫描 4 帧 - B段+H 在4位间从左到右扫描 */
static const uint8_t ANIM_DEFAULT[4][4] = {
    {B01111110, B11111111, B11111111, B11111111},  // 位0 B+H亮
    {B11111111, B01111110, B11111111, B11111111},  // 位1 B+H亮
    {B11111111, B11111111, B01111110, B11111111},  // 位2 B+H亮
    {B11111111, B11111111, B11111111, B01111110},  // 位3 B+H亮
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

// 亮度
static uint8_t brightnessPct = DISPLAY_BRIGHTNESS_DEFAULT;
static uint8_t lastSegments[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};

// PWM 硬件定时器 ISR（替代 FreeRTOS 任务）
// 定时器 0，分频 80 → 1 tick = 1µs，自动重载（autoreload=true）
// 每 50µs 触发一次 ISR → 20kHz，100 个 tick = 1 个 PWM 周期 (5ms = 200Hz)
// 亮度 0~100 映射到由 tick 计数决定的占空比
static volatile uint8_t pwmSegs[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
static volatile uint8_t pwmBrightness = DISPLAY_BRIGHTNESS_DEFAULT;
static hw_timer_t *pwmTimer = NULL;

static void segs_write(const uint8_t segs[4]) {
    // 写入共享缓冲区，ISR 负责按亮度驱动硬件
    memcpy((void*)pwmSegs, segs, 4);
    memcpy(lastSegments, segs, 4);
}

// ============================================================
// 74HC595 快速移位操作（ISR 安全，直接 GPIO 寄存器写入）
// DATA→GPIO14(低32位), CLK→GPIO33(高32位), LATCH→GPIO32(高32位)
// ============================================================

static void IRAM_ATTR isr_shift_byte(uint8_t val) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(GPIO_NUM_14, (val >> i) & 1);  // DATA
        gpio_set_level(GPIO_NUM_33, 1);                // CLK ↑
        gpio_set_level(GPIO_NUM_33, 0);                // CLK ↓
    }
}

static void IRAM_ATTR isr_write_segments(const uint8_t segs[4]) {
    // 顺序同 ShiftRegister74HC595<4>::updateRegisters(): 管4→管3→管2→管1(MSBFIRST)
    isr_shift_byte(segs[3]);
    isr_shift_byte(segs[2]);
    isr_shift_byte(segs[1]);
    isr_shift_byte(segs[0]);
    gpio_set_level(GPIO_NUM_32, 1);  // LATCH ↑
    gpio_set_level(GPIO_NUM_32, 0);  // LATCH ↓
}

// ============================================================
// 硬件定时器 PWM（计数式，定时器自动重载 50µs → 20kHz）
// 100 ticks = 1 PWM 周期 (5ms = 200Hz)
//   tick 0: 输出段码（点亮）
//   tick = brightness: 输出全灭（熄灭），brightness=100 时永不熄灭
//   100 ticks 后重复
// ============================================================

static uint8_t pwm_tick = 0;

static void IRAM_ATTR pwm_timer_isr(void) {
    pwm_tick++;
    if (pwm_tick >= 100) {
        // 新周期：重置计数，点亮段码
        pwm_tick = 0;
        if (pwmBrightness > 0) {
            // 原子读取 volatile 缓冲区到栈数组
            uint8_t segs[4];
            segs[0] = pwmSegs[0];
            segs[1] = pwmSegs[1];
            segs[2] = pwmSegs[2];
            segs[3] = pwmSegs[3];
            isr_write_segments(segs);
        } else {
            // 亮度 0：全灭
            uint8_t blank[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
            isr_write_segments(blank);
        }
    } else if (pwm_tick == pwmBrightness) {
        // 达到亮度阈值，熄灭段码（brightness=100 时永不触发）
        uint8_t blank[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
        isr_write_segments(blank);
    }
}

// 夜间自动关屏
static bool nightEnabled = NIGHT_MODE_DEFAULT;
static uint8_t nightStart = NIGHT_START_DEFAULT;
static uint8_t nightEnd = NIGHT_END_DEFAULT;
static bool nightWake = false;
static unsigned long nightWakeTime = 0;

// 按键3动画配置
static uint8_t btn3AnimType = BTN3_ANIM_OFF;
static uint8_t btn3AnimId = 0;

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
#define WEATHER_ANIM_CLOUD   2     // 多云
#define WEATHER_ANIM_RAIN    3     // Raining
#define WEATHER_ANIM_SNOW    4     // 雪
#define WEATHER_ANIM_THUNDER 5     // 雷阵
#define WEATHER_ANIM_DEFAULT 6     // 默认扫描
static uint8_t weatherAnimType = WEATHER_ANIM_NONE;
static unsigned long weatherAnimStartTime = 0;
static int16_t weatherDisplayTemp = 0;     // 动画结束后显示的温度
static uint8_t animCycleIndex = 0;         // 动画循环索引(按键2切换)
static int16_t lastAnimTemp = 0;           // 最近一次天气温度，循环动画时保持
#define WEATHER_ANIM_DURATION  3000  // 动画播放时长(ms)

// 闪烁通知模式（vibecoding 联动）
static bool flashActive = false;
static bool flashVisible = true;
static unsigned long flashLastToggle = 0;
#define FLASH_INTERVAL_MS 500

// 天气动画覆写缓冲（从 Preferences 加载用户自定义帧）
static bool wthrOvValid = false;
#define WTHR_OV_MAX_FRAMES 30
static uint8_t wthrOvData[WTHR_OV_MAX_FRAMES][4];
static uint16_t wthrOvDur[WTHR_OV_MAX_FRAMES];
static uint8_t wthrOvCount = 0;
static uint8_t wthrOvIdx = 0;
static unsigned long wthrOvLastMs = 0;

/** 检查内置动画 idx 是否有用户覆写；若有则填充 wthrOv* 缓冲并返回 true */
static bool wthr_load_override(uint8_t builtinIdx) {
    Preferences prefs;
    prefs.begin("builtin", true);
    String val = prefs.getString(("ov" + String(builtinIdx)).c_str(), "");
    prefs.end();
    if (val.length() == 0) return false;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, val);
    if (err != DeserializationError::Ok) return false;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) return false;

    wthrOvCount = 0;
    for (JsonVariant fv : arr) {
        if (wthrOvCount >= WTHR_OV_MAX_FRAMES) break;
        JsonObject f = fv.as<JsonObject>();
        JsonArray d = f["data"].as<JsonArray>();
        if (d.size() != 4) continue;
        for (int i = 0; i < 4; i++) {
            wthrOvData[wthrOvCount][i] = d[i].as<uint8_t>();
        }
        wthrOvDur[wthrOvCount] = f["duration"].as<uint16_t>();
        if (wthrOvDur[wthrOvCount] < 50) wthrOvDur[wthrOvCount] = 50;
        wthrOvCount++;
    }
    if (wthrOvCount == 0) return false;
    wthrOvIdx = 0;
    wthrOvLastMs = millis();
    wthrOvValid = true;
    return true;
}

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
 * 将温度值格式化为 4 位显示 * 显示规则：正数 " XX"（2位数字），负数 "-XX" */
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
    btn3AnimType = constrain(prefs.getUChar(PREFS_KEY_BTN3_TYPE, BTN3_ANIM_OFF), 0, 2);
    btn3AnimId = prefs.getUChar(PREFS_KEY_BTN3_ID, 0);
    prefs.end();
    Serial.printf("[显示] 配置: 亮度=%d%%, 夜间模式=%d (%d:00-%d:00), 按键3动画=%d id=%d\n",
                  brightnessPct, nightEnabled, nightStart, nightEnd, btn3AnimType, btn3AnimId);
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

static void save_btn3_anim_config() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_KEY_BTN3_TYPE, btn3AnimType);
    prefs.putUChar(PREFS_KEY_BTN3_ID, btn3AnimId);
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
    // 缓冲区熄灭，不破坏 lastSegments（便于恢复）
    uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
    memcpy((void*)pwmSegs, off, 4);
}

void display_init() {
    // 硬件初始化：PWM 任务未启动，直接写 74HC595
    {
        uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
        sr.setAll(off);
    }
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
        uint8_t dash[4] = {B11111101, B11111101, B11111101, B11111101};
        segs_write(dash);
    }

    load_display_config();
    pwmBrightness = brightnessPct;

    // 启动硬件定时器 PWM（20kHz 自动重载 + tick 计数 → 200Hz PWM）
    pwmTimer = timerBegin(0, 80, true);  // timer0, APB/80=1MHz→1µs/tick
    timerAttachInterrupt(pwmTimer, pwm_timer_isr, true);
    timerAlarmWrite(pwmTimer, 50, true);  // 50µs 自动重载 → 20kHz
    timerAlarmEnable(pwmTimer);
    Serial.println(F("[显示] 硬件定时器 PWM 已启动 (20kHz ISR, 100-tick→200Hz)"));

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
            uint8_t dash[4] = {B11111101, B11111101, B11111101, B11111101};
            segs_write(dash);
        } else {
            int16_t temp = (int16_t)userNumber;
            uint8_t segs[4];
            // 管1：零上空白，零下负号（G 段）
            if (temp < 0) {
                segs[0] = 0xFD;
                temp = -temp;
            } else {
                segs[0] = SEGMENT_BLANK;
            }
            // 管2-3：温度数值
            segs[1] = (temp >= 10) ? SEGMENTS[temp / 10] : SEGMENT_BLANK;
            segs[2] = SEGMENTS[temp % 10];
            // 管4：°C（A+F+B+G）
            segs[3] = 0x39;
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
        uint8_t apSegs[4] = {SEGMENT_BLANK, SEGMENT_BLANK, 0x11, 0x31};
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

/**
 * 天气动画 → 内置动画类型索引映射
 * 0=Sunshine, 1=Raining, 2=Love, 3=Smile, 4=Sad, 5=Nol,
 * 6=Cloudy, 7=Snow, 8=Thunder, 9=Default
 */
static const uint8_t weatherToBuiltinIdx[] = {
    0,  // 0: NONE
    0,  // 1: SUN → builtin 0
    6,  // 2: CLOUD → builtin 6
    1,  // 3: RAIN → builtin 1
    7,  // 4: SNOW → builtin 7
    8,  // 5: THUNDER → builtin 8
    9,  // 6: DEFAULT → builtin 9
};

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

// ============================================================
// 内置动画默认数据
// ============================================================
const char* BUILTIN_NAMES[10] = {
    "Sunshine",  // 0
    "Raining",   // 1
    "Love",      // 2
    "Smile",     // 3
    "Sad",       // 4
    "Nol",       // 5
    "Cloudy",    // 6
    "Snow",      // 7
    "Thunder",   // 8
    "Default",   // 9
};

/**
 * 将硬编码帧数组（const uint8_t(*)[4], frameCount, frameMs）
 * 转为 JsonArray 帧序列格式
 */
static bool frames_to_json(JsonArray &out, const uint8_t (*frames)[4],
                           uint8_t count, uint16_t ms) {
    for (uint8_t i = 0; i < count; i++) {
        JsonObject f = out.createNestedObject();
        JsonArray d = f["data"].to<JsonArray>();
        for (int j = 0; j < 4; j++) {
            d.add(frames[i][j]);
        }
        f["duration"] = ms;
    }
    return true;
}

bool display_get_builtin_default_frames(uint8_t builtinIdx, JsonArray &frames) {
    switch (builtinIdx) {
    case 0: // Sunshine
        return frames_to_json(frames, ANIM_SUNNY, 2, 400);
    case 1: // Raining
        return frames_to_json(frames, ANIM_RAINY, 6, 150);
    case 2: // Love
        return frames_to_json(frames, LOVE_FRAMES, 6, 180);
    case 3: // Smile
        return frames_to_json(frames, ANIM_SMILE, 2, 300);
    case 4: // Sad
        return frames_to_json(frames, ANIM_SAD, 2, 300);
    case 5: // Nol
        return frames_to_json(frames, ANIM_NOL, 2, 300);
    case 6: // Cloudy
        return frames_to_json(frames, ANIM_CLOUDY, 4, 200);
    case 7: // Snow
        return frames_to_json(frames, ANIM_SNOW, 4, 250);
    case 8: // Thunder
        return frames_to_json(frames, ANIM_THUNDER, 4, 200);
    case 9: // Default
        return frames_to_json(frames, ANIM_DEFAULT, 4, 250);
    default:
        return false;
    }
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
