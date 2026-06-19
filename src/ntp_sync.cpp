/**
 * ntp_sync.cpp - NTP 网络校时管理器 实现
 *
 * 复用用户原有 setClock() 逻辑 *   - configTime() + getLocalTime() 获取 NTP 时间
 *   - rtc.adjust() 写入 DS3231
 *
 * 改造：从阻塞式改为非阻塞状态机
 */

#include "ntp_sync.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include <time.h>

// ============================================================
// 全局状态
// ============================================================

static NTPSyncState  ntpState = NTP_IDLE;
static unsigned long syncStartTime = 0;
static unsigned long lastSyncTime = 0;
static bool          syncedOnce = false;

// NTP 已配置标志（configTime 只需调用一次）
static bool ntpConfigured = false;

// ============================================================
// 状态字符串映射
// ============================================================

const char* ntp_state_str() {
    switch (ntpState) {
    case NTP_IDLE:    return "IDLE";
    case NTP_SYNCING: return "SYNCING";
    case NTP_DONE:    return "DONE";
    case NTP_FAILED:  return "FAILED";
    default:          return "UNKNOWN";
    }
}

// ============================================================
// 内部：检查是否需要同步
// ============================================================

static bool needs_sync() {
    if (!wifi_is_sta_connected()) {
        return false;  // WiFi 未连接，不同
    }
    if (!syncedOnce) {
        return true;   // 从未同步过，必须同步
    }
    // 距上次同步超过 6 小时
    return (millis() - lastSyncTime >= NTP_INTERVAL_MS);
}

// ============================================================
// 内部：将 NTP 时间写入 RTC
// ============================================================

static bool write_ntp_to_rtc() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 3000)) {  // 3秒超时
        return false;
    }

    // 验证时间合理性（年份必须 > 2024）
    if (timeinfo.tm_year + 1900 < 2024) {
        Serial.printf("[NTP] 时间不合理: %d\n", timeinfo.tm_year + 1900);
        return false;
    }

    // 通过 display_manager 校准 RTC（共用同一 RTC 实例）
    display_rtc_adjust(timeinfo.tm_year + 1900,
                       timeinfo.tm_mon + 1,
                       timeinfo.tm_mday,
                       timeinfo.tm_hour,
                       timeinfo.tm_min,
                       timeinfo.tm_sec);

    return true;
}

// ============================================================
// 公开接口实现
// ============================================================

void ntp_init() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    ntpConfigured = true;
    ntpState = NTP_IDLE;
    syncedOnce = false;

    Serial.printf("[NTP] 服务器: %s, 时区: UTC%d\n",
                  NTP_SERVER, GMT_OFFSET_SEC / 3600);
    Serial.println(F("[NTP] 初始化完成"));
}

void ntp_update() {
    switch (ntpState) {

    case NTP_IDLE:
        if (needs_sync()) {
            // 触发同步
            ntpState = NTP_SYNCING;
            syncStartTime = millis();
            // 重新配置 NTP 以发起同步请求
            configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
            Serial.println(F("[NTP] 开始同步"));
        }
        break;

    case NTP_SYNCING: {
        // 尝试获取时间（非阻塞：每次只等一小段时间）        // 首次调用 getLocalTime 会等待，用短超时避免卡住 loop
        struct tm dummy;
        bool got = getLocalTime(&dummy, 0);  // 0 = 不等待，立即返回

        if (got && (dummy.tm_year + 1900 >= 2024)) {
            // 时间有效，写入 RTC
            if (write_ntp_to_rtc()) {
                ntpState = NTP_DONE;
                lastSyncTime = millis();
                syncedOnce = true;
                Serial.println(F("[NTP] 同步成功"));
            } else {
                ntpState = NTP_FAILED;
                Serial.println(F("[NTP] RTC写入失败"));
            }
        } else if (millis() - syncStartTime >= NTP_TIMEOUT_MS) {
            // 超时
            ntpState = NTP_FAILED;
            Serial.println(F("[NTP] 同步超时"));
        }
        // 否则继续等待（下一次 loop 再检查）
        break;
    }

    case NTP_DONE:
    case NTP_FAILED:
        ntpState = NTP_IDLE;  // 重置，等待下次触发
        break;
    }
}

void ntp_force_sync() {
    if (ntpState == NTP_SYNCING) return;  // 已在同步中
    ntpState = NTP_IDLE;
    lastSyncTime = 0;  // 伪造需要同步
    Serial.println(F("[NTP] 强制同步请求"));
}

unsigned long ntp_last_sync_time() {
    return lastSyncTime;
}

bool ntp_has_synced() {
    return syncedOnce;
}


