/**
 * ntp_sync.h — NTP 网络校时管理器
 *
 * 职责：
 *   - 在 WiFi 连接后自动从 NTP 服务器获取标准时间
 *   - 将 NTP 时间写入 DS3231 RTC（保持硬件时钟准确）
 *   - 上电后立即同步一次 + 每 6 小时同步一次
 *
 * 状态机（非阻塞）：
 *   IDLE ──(WiFi已连接 & 需要同步)──> SYNCING ──(成功)──> DONE ──> IDLE
 *                                          └──(超时10s)──> FAILED ──> IDLE
 */

#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <Arduino.h>
#include "config.h"

// ============================================================
// NTP 同步状态
// ============================================================

enum NTPSyncState {
    NTP_IDLE,       // 等待触发条件
    NTP_SYNCING,    // 正在同步中
    NTP_DONE,       // 本次同步成功
    NTP_FAILED      // 本次同步失败
};

// ============================================================
// 公开接口
// ============================================================

/** 初始化 NTP 客户端，在 setup() 中调用 */
void ntp_init();

/** 非阻塞状态机，每圈 loop() 调用 */
void ntp_update();

/** 强制触发一次同步（WiFi 连接成功后调用） */
void ntp_force_sync();

/** 获取上次成功同步的时间戳（millis()） */
unsigned long ntp_last_sync_time();

/** 是否至少完成过一次同步 */
bool ntp_has_synced();

/** 获取当前同步状态字符串（调试用） */
const char* ntp_state_str();

#endif // NTP_SYNC_H
