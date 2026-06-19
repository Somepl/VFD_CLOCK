#include "remote_client.h"
#include "display_manager.h"
#include "pattern_manager.h"
#include "wifi_manager.h"
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

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

static void loadConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    mqttBroker = prefs.getString(PREFS_KEY_REMOTE_URL, "");
    topicKey = prefs.getString(PREFS_KEY_REMOTE_PWD, "");
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
}

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

    Serial.printf("[MQTT] 收到命令: %s\n", cmd.c_str());

    if (cmd == "display_number") {
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
    } else if (cmd == "get_config") {
        publishStatus();
    } else if (cmd == "get_patterns") {
        publishPatterns();
    } else if (cmd == "save_pattern") {
        String name = data["name"].as<String>();
        JsonArray segs = data["data"].as<JsonArray>();
        int editId = data["id"].as<int>();
        if (name.length() == 0 || segs.size() != 4) {
            Serial.printf("[MQTT] save_pattern 参数错误\n");
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
                    publishPatterns();
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
        publishPatterns();
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
        publishPatterns();
    } else if (cmd == "get_animations") {
        publishAnimations();
    } else if (cmd == "save_animation") {
        String name = data["name"].as<String>();
        JsonArray frames = data["frames"].as<JsonArray>();
        int editId = data["id"].as<int>();
        if (name.length() == 0 || frames.size() == 0) {
            Serial.printf("[MQTT] save_animation 参数错误\n");
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
                    publishAnimations();
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
        publishAnimations();
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
        publishAnimations();
    } else if (cmd == "play_animation_frames") {
        JsonArray frames = data["frames"].as<JsonArray>();
        if (frames.size() > 0) {
            display_play_user_anim(frames);
        }
    } else {
        Serial.printf("[MQTT] 未知命令: %s\n", cmd.c_str());
    }

    publishStatus();
}

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
    if (state == REMOTE_DISABLED) return;
    if (!wifi_is_sta_connected()) return;

    if (!mqtt.connected()) {
        state = REMOTE_DISCONNECTED;
        unsigned long now = millis();
        if (now - lastConnectAttempt > RECONNECT_INTERVAL || lastConnectAttempt == 0) {
            lastConnectAttempt = now;
            connectToBroker();
        }
    } else {
        mqtt.loop();
        state = REMOTE_CONNECTED;
        unsigned long now = millis();
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
bool remote_is_enabled() { return state != REMOTE_DISABLED; }

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

void remote_disable() {
    remote_set_config("", "");
}


