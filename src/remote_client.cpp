#include "remote_client.h"
#include "display_manager.h"
#include "pattern_manager.h"
#include "wifi_manager.h"
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ============================================================
// MQTT 远程控制（原有）
// ============================================================

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static RemoteState state = REMOTE_DISABLED;
static String mqttBroker;
static String topicKey;
static String cmdTopic;
static String statusTopic;
static unsigned long lastReconnect = 0;
static unsigned long lastConnectAttempt = 0;
static unsigned long lastPublish = 0;
static const unsigned long RECONNECT_INTERVAL = 5000;
static const unsigned long PUBLISH_INTERVAL = 5000;

// ============================================================
// Worker HTTP 远程控制（新增外网代理）
// ============================================================

static String workerHttpUrl;         // https://clock.xxx.workers.dev
static bool workerEnabled = false;
static unsigned long lastTelemetry = 0;
static unsigned long lastPoll = 0;
static const unsigned long TELEMETRY_INTERVAL = 5000;  // 5s 上报状态
static const unsigned long POLL_INTERVAL = 3000;        // 3s 轮询命令

// ============================================================
// 工具函数
// ============================================================

static String urlDomain(const String& url) {
    int start = url.indexOf("://");
    if (start < 0) start = 0; else start += 3;
    int end = url.indexOf('/', start);
    if (end < 0) end = url.indexOf(':', start);
    if (end < 0) return url.substring(start);
    return url.substring(start, end);
}

static int urlPort(const String& url) {
    int start = url.indexOf("://");
    if (start < 0) start = 0; else start += 3;
    int colon = url.indexOf(':', start);
    if (colon < 0) return url.startsWith("https") ? 443 : 80;
    int slash = url.indexOf('/', colon);
    if (slash < 0) return url.substring(colon + 1).toInt();
    return url.substring(colon + 1, slash).toInt();
}

static String urlPath(const String& url) {
    int start = url.indexOf("://");
    if (start < 0) start = 0; else start += 3;
    int slash = url.indexOf('/', start);
    if (slash < 0) return "/";
    return url.substring(slash);
}

// ============================================================
// MQTT 配置加载
// ============================================================

static void loadConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    mqttBroker = prefs.getString(PREFS_KEY_REMOTE_URL, "");
    topicKey = prefs.getString(PREFS_KEY_REMOTE_PWD, "");
    workerHttpUrl = prefs.getString(PREFS_KEY_WORKER_URL, "");
    prefs.end();

    cmdTopic = "clock/";
    cmdTopic += topicKey;
    cmdTopic += "/cmd";
    statusTopic = "clock/";
    statusTopic += topicKey;
    statusTopic += "/status";

    if (mqttBroker.length() > 0 && topicKey.length() > 0) {
        state = REMOTE_DISCONNECTED;
    } else {
        state = REMOTE_DISABLED;
    }

    workerEnabled = workerHttpUrl.length() > 0;
    if (workerEnabled) {
        Serial.printf("[Worker] HTTP URL: %s\n", workerHttpUrl.c_str());
    }
}

// ============================================================
// MQTT 状态发布
// ============================================================

static void publishStatus() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<256> doc;
    doc["brightness"] = display_get_brightness();
    doc["nightEnabled"] = display_get_night_enabled();
    doc["nightStart"] = display_get_night_start();
    doc["nightEnd"] = display_get_night_end();
    doc["displayMode"] = (int)display_get_mode();
    doc["wifiState"] = wifi_state_str();
    doc["ip"] = wifi_get_ip();
    String out;
    serializeJson(doc, out);
    mqtt.publish(statusTopic.c_str(), out.c_str());
}

static void publishPatterns() {
    if (!mqtt.connected()) return;
    DynamicJsonDocument doc(2048);
    if (!pm_load_patterns(doc)) return;
    DynamicJsonDocument resp(2048);
    resp["_resp"] = "patterns";
    resp["items"] = doc.as<JsonArray>();
    String out;
    serializeJson(resp, out);
    mqtt.publish(statusTopic.c_str(), out.c_str());
}

static void publishAnimations() {
    if (!mqtt.connected()) return;
    DynamicJsonDocument doc(4096);
    if (!pm_load_animations(doc)) return;
    DynamicJsonDocument resp(4096);
    resp["_resp"] = "animations";
    resp["items"] = doc.as<JsonArray>();
    String out;
    serializeJson(resp, out);
    mqtt.publish(statusTopic.c_str(), out.c_str());
}

// ============================================================
// 命令分发（MQTT & HTTP 共用）
// ============================================================

static void executeCommand(const String& cmd, JsonObject& data) {
    Serial.printf("[远程] 执行命令: %s\n", cmd.c_str());

    if (cmd == "flash_start") {
        display_start_flash();
    } else if (cmd == "flash_stop") {
        display_stop_flash();
    } else if (cmd == "display_number") {
        int num = data["number"] | -1;
        if (num >= 0 && num <= 9999) display_show_number((uint16_t)num);
    } else if (cmd == "recover_time") {
        display_set_mode(DISPLAY_TIME);
    } else if (cmd == "toggle_power") {
        display_toggle_power();
    } else if (cmd == "set_brightness") {
        uint8_t pct = constrain(data["brightness"].as<uint8_t>(), 0, 100);
        display_set_brightness(pct);
    } else if (cmd == "set_night_config") {
        bool en = data["enabled"] | display_get_night_enabled();
        uint8_t start = data["start"] | display_get_night_start();
        uint8_t end = data["end"] | display_get_night_end();
        display_set_night_config(en, start, end);
    } else if (cmd == "play_animation") {
        uint8_t type = constrain(data["type"].as<uint8_t>(), 0, 5);
        display_show_web_anim(type);
    } else if (cmd == "show_pattern") {
        JsonArray segs = data["segments"].as<JsonArray>();
        if (segs.size() == 4) {
            uint8_t d[4];
            for (int i = 0; i < 4; i++) d[i] = segs[i].as<uint8_t>();
            display_show_raw(d);
        }
    } else if (cmd == "save_pattern") {
        String name = data["name"].as<String>();
        JsonArray segs = data["data"].as<JsonArray>();
        int editId = data["id"].as<int>();
        if (name.length() == 0 || segs.size() != 4) {
            Serial.printf("[远程] save_pattern 参数错误\n");
            return;
        }
        DynamicJsonDocument pdoc(2048);
        pm_load_patterns(pdoc);
        JsonArray arr = pdoc.as<JsonArray>();
        if (editId > 0) {
            for (JsonVariant v : arr) {
                if ((int)v["id"] == editId) {
                    v["name"] = name;
                    v["data"].as<JsonArray>().clear();
                    for (JsonVariant sv : segs) v["data"].add(sv.as<uint8_t>());
                    pm_save_patterns(pdoc);
                    return;
                }
            }
        }
        uint8_t newId = 1;
        for (JsonVariant v : arr) {
            uint8_t id = v["id"];
            if (id >= newId) newId = id + 1;
        }
        JsonObject obj = arr.createNestedObject();
        obj["id"] = newId;
        obj["name"] = name;
        JsonArray d = obj.createNestedArray("data");
        for (JsonVariant sv : segs) d.add(sv.as<uint8_t>());
        pm_save_patterns(pdoc);
    } else if (cmd == "delete_pattern") {
        int targetId = data["id"].as<int>();
        DynamicJsonDocument pdoc(2048);
        pm_load_patterns(pdoc);
        JsonArray arr = pdoc.as<JsonArray>();
        int removeIdx = -1;
        for (int i = 0; i < (int)arr.size(); i++) {
            if ((int)arr[i]["id"] == targetId) { removeIdx = i; break; }
        }
        if (removeIdx >= 0) {
            arr.remove(removeIdx);
            pm_save_patterns(pdoc);
        }
    } else if (cmd == "save_animation") {
        String name = data["name"].as<String>();
        JsonArray frames = data["frames"].as<JsonArray>();
        int editId = data["id"].as<int>();
        if (name.length() == 0 || frames.size() == 0) {
            Serial.printf("[远程] save_animation 参数错误\n");
            return;
        }
        DynamicJsonDocument adoc(4096);
        pm_load_animations(adoc);
        JsonArray arr = adoc.as<JsonArray>();
        if (editId > 0) {
            for (JsonVariant v : arr) {
                if ((int)v["id"] == editId) {
                    v["name"] = name;
                    v["frames"] = frames;
                    pm_save_animations(adoc);
                    return;
                }
            }
        }
        uint8_t newId = 1;
        for (JsonVariant v : arr) {
            uint8_t id = v["id"];
            if (id >= newId) newId = id + 1;
        }
        JsonObject obj = arr.createNestedObject();
        obj["id"] = newId;
        obj["name"] = name;
        obj["frames"] = frames;
        pm_save_animations(adoc);
    } else if (cmd == "delete_animation") {
        int targetId = data["id"].as<int>();
        DynamicJsonDocument adoc(4096);
        pm_load_animations(adoc);
        JsonArray arr = adoc.as<JsonArray>();
        int removeIdx = -1;
        for (int i = 0; i < (int)arr.size(); i++) {
            if ((int)arr[i]["id"] == targetId) { removeIdx = i; break; }
        }
        if (removeIdx >= 0) {
            arr.remove(removeIdx);
            pm_save_animations(adoc);
        }
    } else if (cmd == "play_animation_frames") {
        JsonArray frames = data["frames"].as<JsonArray>();
        if (frames.size() > 0) {
            display_play_user_anim(frames);
        }
    } else {
        Serial.printf("[远程] 未知命令: %s\n", cmd.c_str());
    }
}

// ============================================================
// HTTP 路径 → MQTT 命令 映射
// ============================================================

static String pathToCmd(const String& path) {
    if (path == "/api/display/number")         return "display_number";
    if (path == "/api/display/flash")          return "flash_start";
    if (path == "/api/display/flash-stop")     return "flash_stop";
    if (path == "/api/display/recover")        return "recover_time";
    if (path == "/api/display/animation")      return "play_animation";
    if (path == "/api/display/anim-play")      return "play_animation_frames";
    if (path == "/api/display/pattern")        return "show_pattern";
    if (path == "/api/display/recover")        return "recover_time";
    if (path == "/api/config" || path == "/api/update/config")   return "set_config";
    if (path == "/api/patterns")               return "manage_patterns";
    if (path == "/api/animations")             return "manage_animations";
    if (path == "/api/animations/builtin")     return "manage_builtins";
    if (path == "/api/wifi/connect")           return "wifi_connect";
    if (path == "/api/wifi/forget")            return "wifi_forget";
    if (path == "/api/restart")                return "restart";
    return "";
}

static String methodToCmd(const String& method, const String& path) {
    // POST/PUT = create/update, DELETE = delete, GET = read (handled by telemetry)
    if (path == "/api/patterns") {
        return (method == "DELETE") ? "delete_pattern" : "save_pattern";
    }
    if (path == "/api/animations") {
        return (method == "DELETE") ? "delete_animation" : "save_animation";
    }
    if (path == "/api/animations/builtin") {
        return (method == "DELETE") ? "restore_builtin" : "override_builtin";
    }
    return pathToCmd(path);
}

// ============================================================
// MQTT 回调
// ============================================================

static void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    String msg((char*)payload, length);
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        Serial.printf("[MQTT] JSON 解析失败: %s\n", msg.c_str());
        return;
    }

    const String cmd = doc["cmd"].as<String>();
    JsonObject data = doc["data"].as<JsonObject>();

    if (cmd.length() > 0) {
        executeCommand(cmd, data);
    }
    publishStatus();
}

// ============================================================
// HTTP Worker 遥测上报
// ============================================================

static bool httpPostJson(const String& host, int port, const String& path, const String& body, String& response) {
    WiFiClientSecure client;
    client.setInsecure(); // 不验证证书（简化）
    if (!client.connect(host.c_str(), port)) {
        return false;
    }
    client.println("POST " + path + " HTTP/1.1");
    client.println("Host: " + host);
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(body.length()));
    client.println();
    client.print(body);

    unsigned long timeout = millis() + 3000;
    while (!client.available() && millis() < timeout) {
        delay(10);
    }
    if (!client.available()) {
        client.stop();
        return false;
    }
    // 跳过 HTTP 头
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    response = client.readString();
    client.stop();
    return true;
}

static bool httpGetJson(const String& host, int port, const String& path, String& response) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host.c_str(), port)) {
        return false;
    }
    client.println("GET " + path + " HTTP/1.1");
    client.println("Host: " + host);
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis() + 3000;
    while (!client.available() && millis() < timeout) {
        delay(10);
    }
    if (!client.available()) {
        client.stop();
        return false;
    }
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    response = client.readString();
    client.stop();
    return true;
}

static void sendTelemetry() {
    if (!workerEnabled) return;
    if (!wifi_is_sta_connected()) return;

    String host = urlDomain(workerHttpUrl);
    int port = urlPort(workerHttpUrl);
    String apiPath = urlPath(workerHttpUrl);
    if (apiPath.endsWith("/")) apiPath.remove(apiPath.length() - 1);

    // 构建遥测数据
    DynamicJsonDocument doc(8192);

    // status
    JsonObject s = doc.createNestedObject("status");
    s["wifiState"] = wifi_state_str();
    s["ip"] = wifi_get_ip();
    s["savedSSID"] = wifi_get_saved_ssid();
    s["staConnected"] = wifi_is_sta_connected();
    s["brightness"] = display_get_brightness();
    {
        uint8_t h, m;
        display_get_hh_mm(h, m);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        s["currentTime"] = buf;
    }

    // config
    JsonObject c = doc.createNestedObject("config");
    c["brightness"] = display_get_brightness();
    c["nightEnabled"] = display_get_night_enabled();
    c["nightStart"] = display_get_night_start();
    c["nightEnd"] = display_get_night_end();
    c["btn3AnimType"] = display_get_btn3_anim_type();
    c["btn3AnimId"] = display_get_btn3_anim_id();

    // patterns
    {
        DynamicJsonDocument pdoc(2048);
        if (pm_load_patterns(pdoc)) {
            doc["patterns"] = pdoc.as<JsonArray>();
        }
    }

    // animations
    {
        DynamicJsonDocument adoc(4096);
        if (pm_load_animations(adoc)) {
            doc["animations"] = adoc.as<JsonArray>();
        }
    }

    // builtins
    {
        JsonArray builtins = doc.createNestedArray("builtins");
        for (int i = 0; i < 10; i++) {
            JsonObject b = builtins.createNestedObject();
            b["id"] = (uint8_t)i;
            b["name"] = BUILTIN_NAMES[i];
            b["hasOverride"] = pm_has_builtin_override(i);
        }
    }

    String body;
    serializeJson(doc, body);

    String resp;
    if (httpPostJson(host, port, apiPath + "/api/telemetry", body, resp)) {
        Serial.println(F("[Worker] 遥测上报成功"));
    } else {
        Serial.println(F("[Worker] 遥测上报失败"));
    }
}

static void pollCommands() {
    if (!workerEnabled) return;
    if (!wifi_is_sta_connected()) return;

    String host = urlDomain(workerHttpUrl);
    int port = urlPort(workerHttpUrl);
    String apiPath = urlPath(workerHttpUrl);
    if (apiPath.endsWith("/")) apiPath.remove(apiPath.length() - 1);

    String resp;
    if (!httpGetJson(host, port, apiPath + "/api/poll", resp)) {
        return; // 轮询失败
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, resp);
    if (err) return;

    JsonObject cmd = doc["command"];
    if (cmd.isNull() || cmd["path"].isNull()) return;

    String path = cmd["path"].as<String>();
    JsonObject body = cmd["body"].as<JsonObject>();

    Serial.printf("[Worker] 收到命令: %s\n", path.c_str());

    String mqttCmd = methodToCmd("POST", path);
    if (mqttCmd.length() > 0) {
        executeCommand(mqttCmd, body);
    }
}

// ============================================================
// 公开接口
// ============================================================

void remote_init() {
    loadConfig();
    mqtt.setCallback(mqttCallback);
    if (state != REMOTE_DISABLED) {
        Serial.printf("[MQTT] Broker: %s, 主题: %s\n", mqttBroker.c_str(), cmdTopic.c_str());
    } else {
        Serial.println(F("[MQTT] 未配置，已禁用"));
    }
}

static void connectToBroker() {
    if (mqttBroker.length() == 0 || topicKey.length() == 0) {
        state = REMOTE_DISABLED;
        return;
    }

    String host = mqttBroker;
    int port = 1883;
    int colon = host.indexOf(':');
    if (colon >= 0) {
        port = host.substring(colon + 1).toInt();
        host = host.substring(0, colon);
    }

    Serial.printf("[MQTT] 连接 %s:%d ...\n", host.c_str(), port);
    mqtt.setServer(host.c_str(), port);

    String clientId = "clock-";
    uint64_t chipId = ESP.getEfuseMac();
    clientId += String((uint32_t)(chipId >> 32), HEX);
    clientId += String((uint32_t)chipId, HEX);

    if (mqtt.connect(clientId.c_str())) {
        mqtt.subscribe(cmdTopic.c_str());
        state = REMOTE_CONNECTED;
        Serial.printf("[MQTT] 已连接，订阅: %s\n", cmdTopic.c_str());
        publishStatus();
    } else {
        Serial.printf("[MQTT] 连接失败, rc=%d\n", mqtt.state());
        state = REMOTE_DISCONNECTED;
    }
}

void remote_update() {
    unsigned long now = millis();

    // --- Worker HTTP 遥测上报 ---
    if (workerEnabled && wifi_is_sta_connected()) {
        if (now - lastTelemetry >= TELEMETRY_INTERVAL || lastTelemetry == 0) {
            lastTelemetry = now;
            sendTelemetry();
        }
        if (now - lastPoll >= POLL_INTERVAL || lastPoll == 0) {
            lastPoll = now;
            pollCommands();
        }
    }

    // --- MQTT（保持原有逻辑）---
    if (state == REMOTE_DISABLED) return;
    if (!wifi_is_sta_connected()) return;

    if (!mqtt.connected()) {
        state = REMOTE_DISCONNECTED;
        if (now - lastConnectAttempt > RECONNECT_INTERVAL || lastConnectAttempt == 0) {
            lastConnectAttempt = now;
            connectToBroker();
        }
    } else {
        mqtt.loop();
        state = REMOTE_CONNECTED;
        if (now - lastPublish > PUBLISH_INTERVAL) {
            lastPublish = now;
            publishStatus();
        }
    }
}

RemoteState remote_get_state() { return state; }

String remote_get_state_str() {
    switch (state) {
    case REMOTE_DISABLED:     return "DISABLED";
    case REMOTE_CONNECTED:    return "CONNECTED";
    case REMOTE_DISCONNECTED: return "DISCONNECTED";
    default:                  return "UNKNOWN";
    }
}

String remote_get_worker_url() { return mqttBroker; }
String remote_get_password() { return topicKey; }
String remote_get_http_url() { return workerHttpUrl; }
bool remote_is_enabled() { return state != REMOTE_DISABLED || workerEnabled; }

void remote_set_config(const String& url, const String& password) {
    mqttBroker = url;
    topicKey = password;
    cmdTopic = "clock/";
    cmdTopic += password;
    cmdTopic += "/cmd";
    statusTopic = "clock/";
    statusTopic += password;
    statusTopic += "/status";

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_REMOTE_URL, url);
    prefs.putString(PREFS_KEY_REMOTE_PWD, password);
    prefs.end();

    if (url.length() > 0 && password.length() > 0) {
        if (mqtt.connected()) mqtt.disconnect();
        state = REMOTE_DISCONNECTED;
        lastConnectAttempt = 0;
    } else {
        state = REMOTE_DISABLED;
        if (mqtt.connected()) mqtt.disconnect();
    }
}

void remote_set_http_url(const String& url) {
    workerHttpUrl = url;
    workerEnabled = url.length() > 0;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_WORKER_URL, url);
    prefs.end();
    Serial.printf("[Worker] 设置 HTTP URL: %s\n", url.c_str());
}

void remote_disable() {
    remote_set_config("", "");
    remote_set_http_url("");
}
