/**
 * display_manager.h — 数码管显示管理器
 *
 * 职责：
 *   - 从 DS3231 RTC 读取时间并刷新 4 位数码管
 *   - 管理 4 种显示模式：时间 / 天气 / 数字 / 关闭
 *   - 数字翻页动画（复用用户原有代码逻辑）
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================
// 显示模式枚举
// ============================================================

enum DisplayMode {
    DISPLAY_TIME,       // 正常走时（默认）
    DISPLAY_WEATHER,    // 显示天气温度
    DISPLAY_NUMBER,     // 显示用户输入的数字
    DISPLAY_OFF,            // 关闭显示
    DISPLAY_ANIMATION,      // 网页触发的内置动画
    DISPLAY_PATTERN,        // 用户自定义单帧图案
    DISPLAY_ANIM_PLAY,      // 用户自定义动画播放
    DISPLAY_AP              // AP 配网模式显示 "AP"
};

// ============================================================
// 公开接口
// ============================================================

/** 立即关闭所有数码管段（在 setup 最开头调用，防止上电全亮） */
void display_force_off();

/** 初始化显示模块（74HC595 + DS3231 RTC） */
void display_init();

/** 每秒钟调用一次，根据当前模式刷新数码管 */
void display_update();

/** 非阻塞动画帧推进 + 亮度PWM，每圈 loop() 调用 */
void display_anim_tick();

/** 切换显示模式 */
void display_set_mode(DisplayMode mode);

/** 获取当前显示模式 */
DisplayMode display_get_mode();

/** 设置要显示的数字（自动切换到 DISPLAY_NUMBER 模式） */
void display_show_number(uint16_t number);

/** 显示天气温度，自动切换到 DISPLAY_WEATHER 模式 */
void display_show_weather(int16_t temperature);

/**
 * 播天气动画 → 自动切到温度显示
 * @param weatherText 天气文字（如"晴""阴""雨"），用于选择动画
 * @param temperature 动画结束后显示的温度值
 */
void display_show_weather_with_anim(const char* weatherText, int16_t temperature);

/** 循环到下一个动画类型（已显示天气时按按键2切换） */
void display_cycle_weather_anim();

/** 进入 AP 配网模式，显示 "AP"（管3-4） */
void display_show_ap();

/** 切换屏幕开关（TIME <-> OFF） */
void display_toggle_power();

/** 从 DS3231 读取当前小时和分钟 */
void display_get_hh_mm(uint8_t &hour, uint8_t &minute);

/** 读取 DS3231 内部温度传感器（摄氏度） */
float display_get_rtc_temp();

/** 校准 DS3231 RTC 时间（由 NTP 同步模块调用） */
void display_rtc_adjust(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t min, uint8_t sec);

/** 设置亮度 (0-100) */
void display_set_brightness(uint8_t pct);
/** 获取亮度 */
uint8_t display_get_brightness();

/** 设置夜间自动关屏配置 */
void display_set_night_config(bool enabled, uint8_t startHour, uint8_t endHour);
/** 获取夜间模式启用状态 */
bool display_get_night_enabled();
/** 获取夜间开始小时 */
uint8_t display_get_night_start();
/** 获取夜间结束小时 */
uint8_t display_get_night_end();
/** 按键唤醒夜间关屏（显示 NIGHT_WAKE_MS 时长） */
void display_night_wake();

// ============================================================
// 网页动画类型常量（与 animations.html 按钮对应）
// ============================================================

#define WEB_ANIM_SUNSHINE 0
#define WEB_ANIM_RAINING  1
#define WEB_ANIM_LOVE     2
#define WEB_ANIM_SMILE    3
#define WEB_ANIM_SAD      4
#define WEB_ANIM_NOL      5

/** 触发指定内置动画，5秒后自动恢复时间显示 */
void display_show_web_anim(uint8_t animType);

// ============================================================
// 用户自定图案/动画
// ============================================================

/** 在数码管上显示原始段数据，6秒后恢复时间 */
void display_show_raw(const uint8_t data[4]);

/** 播放用户自定动画（帧序列），播完后恢复时间 */
void display_play_user_anim(const JsonArray &frames);

// ============================================================
// 内置动画默认数据查询
// ============================================================

/** 内置动画名称列表 */
extern const char* BUILTIN_NAMES[10];

/**
 * 获取内置动画的硬编码默认帧数据
 * @param builtinIdx  0-9（Sunshine=0, Raining=1, Love=2, Smile=3, Sad=4, Nol=5, Cloudy=6, Snow=7, Thunder=8, Default=9）
 * @param frames      输出的 JsonArray，格式同用户动画帧
 * @return true 成功
 */
bool display_get_builtin_default_frames(uint8_t builtinIdx, JsonArray &frames);

// ============================================================
// 按键3动画配置
// ============================================================

#define BTN3_ANIM_OFF     0   // 无动作
#define BTN3_ANIM_BUILTIN 1   // 内置动画 (0-5)
#define BTN3_ANIM_USER    2   // 用户自定动画 (由 animations.json 中的 ID 标识)

/** 设置按键3短按触发的动画 */
void display_set_btn3_anim(uint8_t type, uint8_t id);
/** 获取按键3动画类型 */
uint8_t display_get_btn3_anim_type();
/** 获取按键3动画ID */
uint8_t display_get_btn3_anim_id();
/** 播放按键3配置的动画 */
void display_play_btn3_anim();

/** 启动闪烁通知模式（在当前显示内容上闪烁，500ms交替） */
void display_start_flash();
/** 停止闪烁，恢复时间显示 */
void display_stop_flash();
/** 查询是否正在闪烁 */
bool display_is_flash_active();

#endif // DISPLAY_MANAGER_H
