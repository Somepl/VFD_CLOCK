/**
 * wifi_manager.h — WiFi 连接管理器
 *
 * 职责：
 *   - STA 模式：连接家中 WiFi，断连自动重试
 *   - AP 模式：配网热点，3分钟无设备连接自动退出
 *   - 凭据存储在 Preferences (NVS) 中
 *   - 状态机回调通知（供 web_server / ntp_sync 使用）
 *
 * 状态迁移：
 *   DISCONNECTED ──(有凭据)──> CONNECTING ──(成功)──> CONNECTED
 *        ^                         │                     │
 *        │                         │(失败)               │(按键1短按)
 *        │                         v                     v
 *        └──────────────────── DISCONNECTED          AP_ACTIVE
 *                                                      │    │
 *                                          (客户端连入) │    │(3分钟超时)
 *                                                      v    v
 *                                                AP_CONNECTED  DISCONNECTED
 *                                                      │
 *                                    (网页提交凭据)      │
 *                                                      v
 *                                                 CONNECTING(新WiFi)
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// ============================================================
// WiFi 状态枚举
// ============================================================

enum WiFiState {
    WIFI_DISCONNECTED,   // 未连接，将尝试重连
    WIFI_CONNECTING,     // 正在连接 STA
    WIFI_CONNECTED,      // STA 已连接
    WIFI_AP_ACTIVE,      // AP 模式运行中，等待客户端
    WIFI_AP_CONNECTED    // 有客户端连入 AP
};

// ============================================================
// 公开接口
// ============================================================

/** 初始化：加载 NVS 凭据，尝试连接已知 WiFi */
void wifi_init();

/** 非阻塞状态机，每圈 loop() 调用 */
void wifi_update();

/** 开启 AP 配网模式（按键1短按） */
void wifi_enable_ap();

/** 关闭 AP 模式，恢复 STA 连接 */
void wifi_disable_ap();

/** 保存凭据并连接到指定 WiFi（网页配网提交） */
void wifi_save_and_connect(const char* ssid, const char* password);

/** 清除已保存的 WiFi 凭据（按键1长按） */
void wifi_clear_credentials();

/** 获取当前 WiFi 状态 */
WiFiState wifi_get_state();

/** 获取当前状态的可读字符串 */
const char* wifi_state_str();

/** 是否已连接 STA（可用作网络操作的前置条件） */
bool wifi_is_sta_connected();

/** 获取 STA 模式下的 IP 地址字符串（未连接返回 "0.0.0.0"） */
String wifi_get_ip();

/** 获取已保存的 SSID（空字符串 = 未配过网） */
String wifi_get_saved_ssid();

#endif // WIFI_MANAGER_H
