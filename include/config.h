/**
 * config.h — 集中管理所有引脚定义、阈值和系统常量
 *
 * 修改硬件接线或调整参数时，只需改这个文件
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// 引脚定义（与硬件接线一一对应，不可随意修改）
// ============================================================

// --- 74HC595 数码管驱动（3线 SPI）---
#define PIN_DATA       14      // 数据线 (SER)
#define PIN_LATCH      32      // 锁存线 (RCLK)
#define PIN_CLK        33      // 时钟线 (SRCLK)

// --- DS3231 RTC 时钟模块（I2C）---
#define I2C_SDA        22      // I2C 数据线
#define I2C_SCL        21      // I2C 时钟线

// --- 电容触摸按键（ESP32 内置触摸传感器）---
#define TOUCH1_PIN     T0      // 按键1：配网控制（GPIO4）
#define TOUCH2_PIN     T2      // 按键2：天气显示（GPIO2）
#define TOUCH3_PIN     T3      // 按键3：屏幕开关（GPIO15）

// ============================================================
// 触摸按键参数
// ============================================================

#define TOUCH_THRESHOLD     40      // 默认阈值（auto-calibrate 后会被覆盖）
#define TOUCH_HYSTERESIS    5       // 释放滞后量（delta 小的场景用 3-5）
#define BTN_DEBOUNCE_MS     50      // 消抖时间(ms)
#define BTN_LONG_PRESS_MS   2000    // 长按判定时间(ms)

// --- 触摸灵敏度增强 ---
#define TOUCH_MEASURE_CYCLES    4000    // 测量周期数（默认~1000，越大越灵敏）
#define TOUCH_SLEEP_CYCLES      2000    // 休眠周期数（默认~1000）
#define TOUCH_IIR_ALPHA         25      // 基线跟踪系数 (0-255, 越小越慢)
                                         //   25 ≈ 0.1, 跟踪快；8 ≈ 0.03, 跟踪慢
#define TOUCH_PRESS_MARGIN      1       // 按下判定＝基线－此值（亚克力 delta 仅 3-4，需用 1）
#define TOUCH_NOISE_SPIKE       5       // 单次变化超过此值视为噪声，忽略（小 delta 场景用 5）
#define TOUCH_AUTO_CAL_MS       3000    // 开机后延迟多久自动校准

// ============================================================
// 显示参数
// ============================================================

#define DISPLAY_REFRESH_MS  1000    // 时间刷新间隔(ms)，每秒更新数码管
#define DISPLAY_ANIM_DELAY  50      // 翻页动画每帧延时(ms)
#define WEATHER_DISPLAY_MS  8000    // 天气信息显示时长(ms)，8秒后自动恢复
#define DISPLAY_BRIGHTNESS_DEFAULT 100  // 默认亮度 (0-100)

// 夜间自动关屏（亮度为0且处于TIME模式时触发）
#define NIGHT_MODE_DEFAULT      0       // 默认禁用
#define NIGHT_START_DEFAULT     23      // 默认23:00
#define NIGHT_END_DEFAULT       7       // 默认07:00
#define NIGHT_WAKE_MS           60000   // 按键唤醒后亮1分钟

// ============================================================
// WiFi 参数
// ============================================================

#define AP_SSID             "Clock-Setup"   // 配网热点名称
#define AP_PASSWORD         ""              // 配网热点密码（空=开放）
#define AP_TIMEOUT_MS       180000          // 配网超时(ms)，3分钟无人连接自动关闭
#define AP_CHANNEL           1              // WiFi 信道
#define WIFI_RECONNECT_MS   5000            // WiFi 断连后重试间隔(ms)

// ============================================================
// 网络参数
// ============================================================

#define MDNS_HOSTNAME       "clock"         // mDNS 域名 clock.local
#define NTP_SERVER          "pool.ntp.org"  // NTP 校时服务器（主）
#define NTP_SERVER2         "time.google.com"   // NTP 备用服务器
#define NTP_SERVER3         "ntp.aliyun.com"    // NTP 备用服务器（国内）
#define NTP_INTERVAL_MS     21600000        // NTP 校时间隔(ms) = 6小时
#define NTP_TIMEOUT_MS      10000           // NTP 同步超时(ms)
#define GMT_OFFSET_SEC      (8 * 3600)      // 时区偏移(秒) UTC+8 北京时间
#define DAYLIGHT_OFFSET_SEC 0               // 夏令时偏移(秒)，中国无夏令时

// ============================================================
// 天气 API（心知天气 + 高德IP定位）
// ============================================================

#define WEATHER_API_KEY     "YOUR_WEATHER_API_KEY_HERE"                         // API 密钥
#define WEATHER_API_HOST    "http://api.seniverse.com/v3/weather/now.json" // 天气接口

// IP 定位 — 支持三种方式（按优先级）：
//   1. 高德 IP 定位（需配置 AMAP_API_KEY）
//   2. ip-api.com（国际，国内可能不通）
//   3. 默认城市（Beijing）
#define ENABLE_AMAP_LOCATION    1       // 1=启用高德, 0=禁用
#define AMAP_API_KEY            "YOUR_AMAP_API_KEY_HERE"      // 高德 Web 服务 API Key（留空则用 ip-api.com）
#define AMAP_API_HOST           "https://restapi.amap.com/v3/ip"
#define IP_LOCATION_HOST        "http://ip-api.com/json"          // 备用 IP 定位（高德未配置时使用）
#define DEFAULT_CITY            "Beijing"       // 以上都失败时的默认城市

// ============================================================
// Preferences (NVS) 存储键名
// ============================================================

#define PREFS_NAMESPACE     "clock"         // 命名空间
#define PREFS_KEY_SSID      "wifi_ssid"     // WiFi SSID
#define PREFS_KEY_PASS      "wifi_pass"     // WiFi 密码
#define PREFS_KEY_PAIRED    "paired"        // 是否已配过网
#define PREFS_KEY_TEMP_UNIT "temp_unit"     // 温度单位 (0=℃, 1=℉)
#define PREFS_KEY_BRIGHTNESS "brightness"   // 亮度 (0-100)
#define PREFS_KEY_NIGHT_EN   "night_en"     // 夜间模式 (0/1)
#define PREFS_KEY_NIGHT_START "night_start" // 夜间开始小时
#define PREFS_KEY_NIGHT_END   "night_end"   // 夜间结束小时
#define PREFS_KEY_TOUCH_THR0  "tch_th0"     // 按键1阈值
#define PREFS_KEY_TOUCH_THR1  "tch_th1"     // 按键2阈值
#define PREFS_KEY_TOUCH_THR2  "tch_th2"     // 按键3阈值
#define PREFS_KEY_TOUCH_HYST  "tch_hyst"    // 触摸滞后
#define PREFS_KEY_REMOTE_URL  "rmt_url"     // 远程 Worker 地址
#define PREFS_KEY_REMOTE_PWD  "rmt_pwd"     // 远程访问密码
#define PREFS_KEY_BTN3_TYPE   "btn3_type"   // 按键3动画类型 (0=off, 1=builtin, 2=user)
#define PREFS_KEY_BTN3_ID     "btn3_id"     // 按键3动画ID

// ============================================================
// 省电配置
// ============================================================

#define ENABLE_CPU_FREQ_DOWN   1    // 1=CPU降频至80MHz, 0=保持240MHz
#define ENABLE_BT_DISABLE      1    // 1=关闭蓝牙, 0=保留
#define ENABLE_WIFI_PS         1    // 1=WiFi modem-sleep, 0=禁用

#endif // CONFIG_H
