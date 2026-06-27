/**
 * display_driver.h — 数码管硬件驱动层
 *
 * 职责：
 *   - 74HC595 3线SPI 驱动（ISR 安全，直接 GPIO 寄存器写入）
 *   - 硬件定时器 PWM（20kHz ISR → 200Hz 亮度控制）
 *   - 7段段码映射（共阳极）
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>

// ============================================================
// 七段数码管段码表
// ============================================================

/** 数字 0-9 段码（共阳极，0=亮 1=灭）*/
extern const uint8_t SEGMENTS[10];

/** 全灭（所有段 = 不亮）*/
extern const uint8_t SEGMENT_BLANK;

/** 字符 → 7段段码映射 */
uint8_t char_to_segments(unsigned char c);

// ============================================================
// PWM 共享缓冲区（ISR 与主循环共享）
// ============================================================

/** 当前 PWM 段码缓冲区（ISR 每 tick 读取）*/
extern volatile uint8_t pwmSegs[4];

/** 当前亮度 0-100（ISR 据此控制占空比）*/
extern volatile uint8_t pwmBrightness;

/** 最近一次写入的段码（用于 flash 恢复）*/
extern uint8_t lastSegments[4];

// ============================================================
// 写入接口
// ============================================================

/**
 * 写入段码（异步：仅更新缓冲区，ISR 负责驱动硬件）
 * @param segs 4 字节段码数组
 */
void segs_write(const uint8_t segs[4]);

/** 全部熄灭（仅修改 pwmSegs，不破坏 lastSegments）*/
void display_force_off();

/** 全部熄灭（修改 pwmSegs + lastSegments）*/
void write_all_off();

/** 写入 4 位数字值（自动映射段码），无动画 */
void write_digits_static(uint8_t d[4]);

// ============================================================
// 初始化
// ============================================================

/** 启动硬件定时器 PWM，必须在所有写入操作前调用 */
void display_driver_init();

#endif // DISPLAY_DRIVER_H
