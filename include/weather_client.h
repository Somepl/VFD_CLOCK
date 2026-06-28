/**
 * weather_client.h — 心知天气客户端
 *
 * 职责：
 *   - 通过 IP 自动定位城市
 *   - 获取当前天气（温度和天气文字）
 *   - 支持摄氏度/华氏度切换
 *
 * 状态机（非阻塞）：
 *   IDLE ──(按键2短按)──> FETCH_IP ──(成功)──> FETCH_WEATHER ──(成功)──> DONE
 *                              └──(失败)──> FAILED                     └──(失败)──> FAILED
 *   FAILED ──> IDLE（短暂显示 "----" 后恢复）
 *
 * 温度单位（按键2长按切换）：
 *   默认摄氏度(℃)，长按切换为华氏度(℉)，存储在 NVS 中
 */

#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <Arduino.h>
#include "config.h"

// ============================================================
// 天气获取状态
// ============================================================

enum WeatherFetchState {
    WEATHER_IDLE,           // 等待触发
    WEATHER_FETCH_IP,       // 正在获取 IP 定位
    WEATHER_FETCH_DATA,     // 正在获取天气数据
    WEATHER_DONE,           // 获取成功
    WEATHER_FAILED          // 获取失败
};

// ============================================================
// 温度单位
// ============================================================

enum TempUnit {
    TEMP_CELSIUS = 0,
    TEMP_FAHRENHEIT = 1
};

// ============================================================
// 公开接口
// ============================================================

/** 初始化天气客户端，在 setup() 中调用 */
void weather_init();

/** 非阻塞状态机，每圈 loop() 调用 */
void weather_update();

/** 触发一次天气获取（按键2短按） */
void weather_fetch();

/** 切换温度单位（按键2长按） */
void weather_toggle_unit();

/** 获取当前温度（按当前单位） */
int16_t weather_get_temperature();

/** 获取天气文字描述 */
const char* weather_get_text();

/** 获取天气获取状态 */
WeatherFetchState weather_get_state();

/** 设置天气 API Key（保存到 NVS） */
void weather_set_api_key(const String& key);

/** 设置高德 API Key（保存到 NVS） */
void weather_set_amap_key(const String& key);

#endif // WEATHER_CLIENT_H
