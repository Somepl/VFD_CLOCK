/**
 * button_handler.h — 电容触摸按键处理器
 *
 * 替代旧有的 ISR + FreeRTOS 信号量方案
 * 改为在 loop() 中轮询 touchRead()，非阻塞，无中断
 *
 * 支持：短按 / 长按（2秒） 识别
 * 通过回调函数注册行为，各按键行为在 main.cpp 中绑定
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "config.h"

// ============================================================
// 按键 ID 枚举
// ============================================================

enum ButtonID {
    BUTTON_1 = 0,   // 配网（T0）
    BUTTON_2 = 1,   // 天气（T2）
    BUTTON_3 = 2,   // 开关屏（T3）
    BUTTON_COUNT = 3
};

// ============================================================
// 回调函数类型
// ============================================================

typedef void (*ButtonCallback)();

// ============================================================
// 公开接口
// ============================================================

/** 初始化触摸引脚，必须在 setup() 中调用 */
void button_init();

/** 非阻塞轮询，每圈 loop() 都调用 */
void button_update();

/** 注册短按回调 */
void button_on_short_press(ButtonID btn, ButtonCallback callback);

/** 注册长按回调（按下超过 2 秒触发） */
void button_on_long_press(ButtonID btn, ButtonCallback callback);

/** 读取指定按键的原始 touch 值 */
uint16_t button_get_raw(ButtonID btn);

/** 获取指定按键的当前基线值（未触摸时的平滑值） */
uint16_t button_get_baseline(ButtonID btn);

/** 获取指定按键的阈值 */
uint16_t button_get_threshold(ButtonID btn);

/** 设置指定按键的阈值并存入 NVS */
void button_set_threshold(ButtonID btn, uint16_t value);

/** 获取触摸滞后值 */
uint16_t button_get_hysteresis();

/** 设置触摸滞后值并存入 NVS */
void button_set_hysteresis(uint16_t value);

/**
 * 自动校准所有按键
 * 在 setup() 末尾调用，或用户通过网页触发
 * 采集若干次读数后设定 baseline 和 threshold
 */
void button_auto_calibrate();

#endif // BUTTON_HANDLER_H
