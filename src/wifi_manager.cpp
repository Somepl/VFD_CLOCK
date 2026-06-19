/**
 * wifi_manager.cpp - WiFi 连接管理器 实现
 *
 * 使用 WiFi.onEvent() 注册事件回调，在回调中更新状态 * AP 模式超时wifi_update() 中处理（非阻塞 millis() 计时） */

#include "wifi_manager.h"
#include "web_server.h"
#include "ntp_sync.h"
#include <Preferences.h>

// ============================================================
// 全局状态
// ============================================================

static WiFiState   wifiState = WIFI_DISCONNECTED;
static String      savedSSID;
static String      savedPassword;
static bool        credentialsLoaded = false;

// AP 超时计时
static unsigned long apStartTime = 0;
static bool          apTimedOut = false;

// STA 重连计时
static unsigned long staReconnectTime = 0;

// ============================================================
// 状态字符串映射（调试用）
// ============================================================

const char* wifi_state_str() {
    switch (wifiState) {
    case WIFI_DISCONNECTED: return "DISCONNECTED";
    case WIFI_CONNECTING:   return "CONNECTING";
    case WIFI_CONNECTED:    return "CONNECTED";
    case WIFI_AP_ACTIVE:    return "AP_ACTIVE";
    case WIFI_AP_CONNECTED: return "AP_CONNECTED";
    default:                return "UNKNOWN";
    }
}

// ============================================================
// 内部：加载保存 NVS 凭据
// ============================================================

static void load_credentials() {
    Preferences prefs;
    bool ok = prefs.begin(PREFS_NAMESPACE, true);  // 只读模式

    if (!ok) {
        Serial.println(F("[WiFi] 警告：NVS命名空间 'clock' 打开失败"));
        credentialsLoaded = false;
        return;
    }

    savedSSID = prefs.getString(PREFS_KEY_SSID, "");
    savedPassword = prefs.getString(PREFS_KEY_PASS, "");
    prefs.end();

    if (savedSSID.length() > 0) {
        credentialsLoaded = true;
        Serial.printf("[WiFi] 已加载凭据 SSID=%s, 长度=%d\n",
                      savedSSID.c_str(), savedSSID.length());
    } else {
        credentialsLoaded = false;
        Serial.println(F("[WiFi] 无已保存的WiFi凭据"));
    }
}

static void save_credentials(const char* ssid, const char* password) {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);  // 读写模式

    prefs.putString(PREFS_KEY_SSID, ssid);
    prefs.putString(PREFS_KEY_PASS, password);
    prefs.putBool(PREFS_KEY_PAIRED, true);

    savedSSID = String(ssid);
    savedPassword = String(password);
    credentialsLoaded = true;

    Serial.printf("[WiFi] 凭据已保存 SSID=%s\n", ssid);
    prefs.end();
}

// ============================================================
// 内部：连接 STA
// ============================================================

static void connect_sta() {
    if (!credentialsLoaded || savedSSID.length() == 0) {
        Serial.println(F("[WiFi] 无凭据，跳过STA连接"));
        wifiState = WIFI_DISCONNECTED;
        return;
    }

    Serial.printf("[WiFi] 正在连接 %s ...\n", savedSSID.c_str());
    wifiState = WIFI_CONNECTING;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_HOSTNAME);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
}

// ============================================================
// WiFi 事件回调（在 WiFi 线程上下文中运行，只做轻量操作）
// ============================================================

static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        wifiState = WIFI_CONNECTED;
        Serial.printf("[WiFi] STA 已连接！IP: %s\n", WiFi.localIP().toString().c_str());
#if ENABLE_WIFI_PS
        WiFi.setSleep(true);
        Serial.println(F("[WiFi] Modem-sleep 已启用"));
#endif
        // 启动 mDNS，可通过 clock.local 访问
        web_server_start_mdns();
        // 触发 NTP 校时
        ntp_force_sync();
        break;
    }

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        if (wifiState == WIFI_CONNECTED) {
            Serial.println(F("[WiFi] STA 连接断开，将自动重连"));
        }
        wifiState = WIFI_DISCONNECTED;
        staReconnectTime = millis();
        break;
    }

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
        wifiState = WIFI_AP_CONNECTED;
        apStartTime = millis();  // 重置超时计时
        Serial.println(F("[WiFi] 有客户端连入 AP"));
        break;
    }

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
        wifiState = WIFI_AP_ACTIVE;
        apStartTime = millis();  // 重新开始超时计时
        Serial.println(F("[WiFi] log"));
        break;
    }

    default:
        break;
    }
}

// ============================================================
// 公开接口实现
// ============================================================

void wifi_init() {
    Serial.println(F("[WiFi] log"));

    // 注册事件回调
    WiFi.onEvent(onWiFiEvent);

    // 初始化 WiFi 底层（使 tcpip 栈可用，避免 Web 服务器崩溃）
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(MDNS_HOSTNAME);

    // 加载 NVS 凭据
    load_credentials();

    // 有凭据就尝试连接
    if (credentialsLoaded) {
        connect_sta();
    } else {
        wifiState = WIFI_DISCONNECTED;
        Serial.println(F("[WiFi] 首次开机，未配网。长按按键1或使用网页配置"));
        // 不自动开 AP——用户需要按按键1来配网
    }

    Serial.println(F("[WiFi] 初始化完成"));
}

void wifi_update() {
    unsigned long now = millis();

    switch (wifiState) {

    case WIFI_DISCONNECTED:
        // 有凭据且过了重连间隔 -> 尝试重连
        if (credentialsLoaded && (now - staReconnectTime >= WIFI_RECONNECT_MS)) {
            staReconnectTime = now;
            connect_sta();
        }
        break;

    case WIFI_CONNECTING: {
        // 连接中，WiFi.begin() 是异步的，等待事件回调
        // 每5秒打印一次状态帮助调试
        static unsigned long lastStatusLog = 0;
        if (now - lastStatusLog >= 5000) {
            lastStatusLog = now;
            wl_status_t s = WiFi.status();
            const char* statusStr = (s == WL_IDLE_STATUS) ? "IDLE" :
                                    (s == WL_NO_SSID_AVAIL) ? "NO_SSID" :
                                    (s == WL_SCAN_COMPLETED) ? "SCAN_DONE" :
                                    (s == WL_CONNECTED) ? "CONNECTED" :
                                    (s == WL_CONNECT_FAILED) ? "FAILED" :
                                    (s == WL_CONNECTION_LOST) ? "LOST" :
                                    (s == WL_DISCONNECTED) ? "DISCONNECTED" : "";
            Serial.printf("[WiFi] 连接状态 %s (status=%d)\n", statusStr, s);
        }
        break;
    }

    case WIFI_CONNECTED:
        // 一切正常，无需操作
        break;

    case WIFI_AP_ACTIVE:
        // 检查 3 分钟超时
        if (now - apStartTime >= AP_TIMEOUT_MS) {
            Serial.println(F("[WiFi] log"));
            wifi_disable_ap();
        }
        break;

    case WIFI_AP_CONNECTED:
        // 有客户端连接，保持 AP
        // 超时在客户端断开后重新计时（见事件回调）
        break;
    }
}

void wifi_enable_ap() {
    Serial.println(F("[WiFi] 启动 AP 配网模式..."));

    // 先断开 STA 连接（避免信道冲突）
    WiFi.disconnect(true);

    // 切换到 AP+STA 双模式（保留 STA 连接能力）
    WiFi.mode(WIFI_AP_STA);

    // 配置 AP
    if (WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL)) {
        wifiState = WIFI_AP_ACTIVE;
        apStartTime = millis();
        apTimedOut = false;

        // 确保网页服务器已启动（配网页）
        web_server_init();

        Serial.println(F("[WiFi] AP 已启动："));
        Serial.printf("  SSID: %s\n", AP_SSID);
        Serial.printf("  IP:   %s\n", WiFi.softAPIP().toString().c_str());
        Serial.println(F("  3分钟后无客户端连接将自动关闭"));
    } else {
        Serial.println(F("[WiFi] AP 启动失败"));
        wifiState = WIFI_DISCONNECTED;
    }
}

void wifi_disable_ap() {
    Serial.println(F("[WiFi] 关闭 AP 模式..."));

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiState = WIFI_DISCONNECTED;

    // 如果有凭据，尝试连接
    if (credentialsLoaded) {
        staReconnectTime = millis();
        connect_sta();
    }
}

void wifi_save_and_connect(const char* ssid, const char* password) {
    Serial.printf("[WiFi] 配网完成: %s\n", ssid);

    // 保存到 NVS
    save_credentials(ssid, password);

    // 关闭 AP
    WiFi.softAPdisconnect(true);

    // 切换到 STA 并连接
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_HOSTNAME);
    WiFi.begin(ssid, password);
    wifiState = WIFI_CONNECTING;

    Serial.println(F("[WiFi] log"));
}

void wifi_clear_credentials() {
    Serial.println(F("[WiFi] 清除所有WiFi凭据"));

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.clear();  // 清空整个命名空间
    prefs.end();

    savedSSID = "";
    savedPassword = "";
    credentialsLoaded = false;

    WiFi.disconnect(true);
    wifiState = WIFI_DISCONNECTED;

    Serial.println(F("[WiFi] 凭据已清除，请重新配置"));
}

WiFiState wifi_get_state() {
    return wifiState;
}

bool wifi_is_sta_connected() {
    return (wifiState == WIFI_CONNECTED) && WiFi.isConnected();
}

String wifi_get_ip() {
    if (wifiState == WIFI_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (wifiState == WIFI_AP_ACTIVE || wifiState == WIFI_AP_CONNECTED) {
        return WiFi.softAPIP().toString();
    }
    return String("0.0.0.0");
}

String wifi_get_saved_ssid() {
    return savedSSID;
}


