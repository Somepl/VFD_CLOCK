/**
 * main.cpp - ESP32 数码管时钟 主程序
 * 架构：单线程 loop() + millis() 非阻塞 * 配网、天气、NTP校时、显示刷新全部在 loop() 中轮流执行 * ESPAsyncWebServer 在后台异步处理 HTTP 请求，不占用 loop() 时间
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "display_manager.h"
#include "button_handler.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ntp_sync.h"
#include "weather_client.h"
#include "remote_client.h"

// ============================================================
// 全局时间戳（用于非阻塞计时）
// ============================================================

unsigned long lastDisplayUpdate = 0;

// ============================================================
// 按键回调函数
// ============================================================

// --- 按键1：WiFi 配网 ---
void on_button1_short_press() {
    WiFiState state = wifi_get_state();
    if (state == WIFI_AP_ACTIVE || state == WIFI_AP_CONNECTED) {
        // AP 模式关闭 AP
        wifi_disable_ap();
    } else {
        // 其他状开AP 配网
        wifi_enable_ap();
    }
}

void on_button1_long_press() {
    // 长按 -> 清除 WiFi 凭据
    Serial.println(F("[主控] 按键1长按：清除WiFi凭据"));
    wifi_clear_credentials();
    // 清除后自动进入 AP 模式，方便重新配置
    wifi_enable_ap();
}

// --- 按键2：天气 ---
void on_button2_short_press() {
    if (display_get_mode() == DISPLAY_WEATHER) {
        // 已在天气显示模式，切换到下一个动画
        Serial.println(F("[主控] 按键2短按：切换动画"));
        display_cycle_weather_anim();
    } else {
        // 其他模式，重新获取天气
        Serial.println(F("[主控] 按键2短按：获取天气"));
        weather_fetch();
    }
}

void on_button2_long_press() {
    Serial.println(F("[主控] 按键2长按：切换温度单位"));
    weather_toggle_unit();
}

// --- 按键3：屏幕开关/ 夜间唤醒 ---
void on_button3_short_press() {
    display_toggle_power();
}

void on_button3_long_press() {
    // 按键3无长按功能
}

// ============================================================
// setup() - 初始化所有模块
// ============================================================

void setup() {
    Serial.begin(115200);
    // 立即熄灭数码管，防止 74HC595 上电默认 LOW 导致全亮
    display_force_off();
        delay(500);
    Serial.println();
    Serial.println(F("===================================="));
    Serial.println(F("  ESP32 数码管时钟启动中..."));
    Serial.println(F("===================================="));

    // --- Step 1: 省电基础设置 ---
#if ENABLE_CPU_FREQ_DOWN
    setCpuFrequencyMhz(80);
    Serial.printf("[系统] CPU频率: %d MHz\n", getCpuFrequencyMhz());
#endif
#if ENABLE_BT_DISABLE
    btStop();
    Serial.println(F("[系统] 蓝牙已关闭"));
#endif

    // --- Step 2: 初始化显示模块 ---
    display_init();

    // --- Step 3: 初始化按键模块 + 绑定回调 ---
    button_init();
    button_on_short_press(BUTTON_1, on_button1_short_press);
    button_on_long_press(BUTTON_1, on_button1_long_press);
    button_on_short_press(BUTTON_2, on_button2_short_press);
    button_on_long_press(BUTTON_2, on_button2_long_press);
    button_on_short_press(BUTTON_3, on_button3_short_press);
    button_on_long_press(BUTTON_3, on_button3_long_press);

    // --- Step 4: 初始化 WiFi ---
    wifi_init();

    // --- OTA 无线烧录（配网后可通过 WiFi 更新固件）---
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "固件" : "文件系统";
        Serial.printf("[OTA] 开始更新 %s ...\n", type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("[OTA] 更新完成，重启中..."));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] 进度: %u%%\r", (progress * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] 错误: %u\n", error);
    });
    ArduinoOTA.begin();

    // --- Step 5: 初始化网页服务器 ---
    // 服务器在此启动一次，之后无论 STA 还是 AP 模式都在端口 80 响应
    web_server_init();

    // --- Step 6: 初始化远程客户端 ---
    remote_init();

    // --- Step 7: 初始化 NTP 校时 ---
    ntp_init();

    // --- Step 7: 初始化天气客户端 ---
    weather_init();

    // --- 触摸自动校准（延迟等待系统稳定）---
    delay(TOUCH_AUTO_CAL_MS);
    button_auto_calibrate();

    Serial.println(F("初始化完成，进入主循环"));
}

// ============================================================
// loop() -> 主循环，所有模块在此轮流执行
// ============================================================

void loop() {
    unsigned long now = millis();

    // --- OTA 无线烧录（每圈都跑，有WiFi连接时可用）---
    ArduinoOTA.handle();

    // --- Telnet 远程日志 ---
    
    // --- 按键检测（每圈都跑）---
    button_update();

    // --- WiFi 状态机（每圈都跑）---
    wifi_update();

    // --- NTP 校时检查（每圈都跑）---
    ntp_update();

    // --- 天气状态机（每圈都跑）---
    weather_update();

    // --- 远程控制客户端（每圈都跑）---
    remote_update();

    // --- 数码管动画帧推进（每圈都跑，非阻塞）---
    display_anim_tick();

    // --- 数码管显示刷新（每秒一次）---
    if (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
        lastDisplayUpdate = now;
        display_update();
    }
}


