/**
 * display_config.h — 数码管 NVS 配置持久化
 *
 * 职责：
 *   - 亮度、夜间模式、按键3动画配置的 NVS 读写
 *   - 配置变量全局访问
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <Arduino.h>

// ============================================================
// 配置变量（全局访问，由 display_config.cpp 定义）
// ============================================================

/** 当前亮度 0-100 */
extern uint8_t brightnessPct;

// 夜间自动关屏
extern bool nightEnabled;
extern uint8_t nightStart;
extern uint8_t nightEnd;

// 按键3动画配置
extern uint8_t btn3AnimType;
extern uint8_t btn3AnimId;

// ============================================================
// 持久化接口
// ============================================================

/** 从 NVS 加载全部配置 */
void load_display_config();

/** 保存亮度到 NVS */
void save_display_config_brightness();

/** 保存夜间模式配置到 NVS */
void save_display_config_night();

/** 保存按键3动画配置到 NVS */
void save_btn3_anim_config();

#endif // DISPLAY_CONFIG_H
