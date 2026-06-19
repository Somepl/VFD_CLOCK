/**
 * web_server.cpp - 异步网页服务器 + mDNS 实现
 *
 * API 路由： *   GET  /api/status         -> 系统状态 JSON
 *   GET  /api/wifi/scan      -> 扫描附近 WiFi 网络
 *   POST /api/wifi/connect   -> 保存凭据并连接 *   POST /api/display/number -> 设置数码管显示数字 *   POST /api/display/recover -> 恢复时间显示
 */

#include "web_server.h"
#include "config.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "pattern_manager.h"
#include "button_handler.h"
#include "remote_client.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ============================================================
// 全局实例
// ============================================================

static AsyncWebServer server(80);
static bool serverRunning = false;
static bool mdnsRunning = false;

// ============================================================
// 内部辅助：构建 WiFi 扫描结果 JSON
// ============================================================

static String buildScanJson() {
    int n = WiFi.scanComplete();
    if (n < 0) {
        return "[]";  // 扫描未完成或失败
    }

    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.to<JsonArray>();

    for (int i = 0; i < n && i < 20; i++) {  // 最多20个结果
        JsonObject net = networks.createNestedObject();
        net["ssid"]      = WiFi.SSID(i);
        net["rssi"]      = WiFi.RSSI(i);
        net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    // 扫描完成后清理，以便下次扫描
    WiFi.scanDelete();

    String result;
    serializeJson(doc, result);
    return result;
}

// ============================================================
// 内部辅助：构建配置 JSON
// ============================================================

static String buildConfigJson() {
    StaticJsonDocument<512> doc;

    doc["brightness"]       = display_get_brightness();
    doc["nightEnabled"]     = display_get_night_enabled();
    doc["nightStart"]       = display_get_night_start();
    doc["nightEnd"]         = display_get_night_end();

    JsonArray touch = doc.createNestedArray("touchRaw");
    for (int i = 0; i < BUTTON_COUNT; i++) {
        touch.add(button_get_raw((ButtonID)i));
    }

    JsonArray base = doc.createNestedArray("touchBaselines");
    for (int i = 0; i < BUTTON_COUNT; i++) {
        base.add(button_get_baseline((ButtonID)i));
    }

    JsonArray thr = doc.createNestedArray("touchThresholds");
    for (int i = 0; i < BUTTON_COUNT; i++) {
        thr.add(button_get_threshold((ButtonID)i));
    }

    doc["touchHysteresis"]  = button_get_hysteresis();

    doc["remoteUrl"]        = remote_get_worker_url();
    doc["remotePassword"]   = remote_get_password();
    doc["remoteState"]      = remote_get_state_str();

    doc["rtcTemp"]          = display_get_rtc_temp();

    String result;
    serializeJson(doc, result);
    return result;
}

// ============================================================
// 内部辅助：解析配置 POST 请求体
// ============================================================

static void handleConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    String body;
    body.reserve(len);
    body.concat((char*)data, len);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON格式错误\"}");
        return;
    }

    if (doc.containsKey("brightness")) {
        uint8_t pct = constrain(doc["brightness"].as<uint8_t>(), 0, 100);
        display_set_brightness(pct);
    }

    {
        bool enabled = doc.containsKey("nightEnabled") ? doc["nightEnabled"].as<bool>() : display_get_night_enabled();
        uint8_t start = doc.containsKey("nightStart") ? doc["nightStart"].as<uint8_t>() : display_get_night_start();
        uint8_t end = doc.containsKey("nightEnd") ? doc["nightEnd"].as<uint8_t>() : display_get_night_end();
        display_set_night_config(enabled, start, end);
    }

    if (doc.containsKey("touchThresholds")) {
        JsonArray arr = doc["touchThresholds"].as<JsonArray>();
        for (size_t i = 0; i < arr.size() && i < BUTTON_COUNT; i++) {
            button_set_threshold((ButtonID)i, arr[i].as<uint16_t>());
        }
    }
    if (doc.containsKey("touchHysteresis")) {
        button_set_hysteresis(doc["touchHysteresis"].as<uint16_t>());
    }

    if (doc.containsKey("remoteUrl")) {
        remote_set_config(
            doc["remoteUrl"].as<String>(),
            doc["remotePassword"].as<String>()
        );
    }

    request->send(200, "application/json", buildConfigJson());
}

// ============================================================
// 内部辅助：构建系统状态 JSON
// ============================================================

static String buildStatusJson() {
    StaticJsonDocument<768> doc;

    doc["wifiState"]       = wifi_state_str();
    doc["ip"]              = wifi_get_ip();
    doc["savedSSID"]       = wifi_get_saved_ssid();
    doc["staConnected"]    = wifi_is_sta_connected();

    DisplayMode dm = display_get_mode();
    doc["displayMode"] = (dm == DISPLAY_TIME) ? "TIME" :
                             (dm == DISPLAY_WEATHER) ? "WEATHER" :
                             (dm == DISPLAY_NUMBER) ? "NUMBER" :
                             (dm == DISPLAY_ANIMATION) ? "ANIM" :
                             (dm == DISPLAY_PATTERN) ? "PATTERN" :
                             (dm == DISPLAY_ANIM_PLAY) ? "ANIM_PLAY" : "OFF";

    doc["brightness"] = display_get_brightness();

    uint8_t hh, mm;
    display_get_hh_mm(hh, mm);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    doc["currentTime"] = buf;

    String result;
    serializeJson(doc, result);
    return result;
}

// ============================================================
// 公开接口实现
// ============================================================

void web_server_init() {
    if (serverRunning) return;

    Serial.println(F("[Web] log"));

    // --- 挂载 LittleFS ---
    if (!LittleFS.begin(true)) {
        Serial.println(F("[Web] 错误：LittleFS 挂载失败"));
        return;
    }
    Serial.println(F("[Web] LittleFS 已挂载"));

    // --- 初始化图案动画管理器 ---
    pm_init();

    // --- 注册 API 路由 ---

    // 状态查询
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", buildStatusJson());
    });

    // WiFi 扫描
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        WiFi.scanNetworks(true);  // true = 异步扫描
        delay(2000);
        String json = buildScanJson();
        request->send(200, "application/json", json);
    });

    // WiFi 连接（配网提交）
    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // 解析请求体
            String body;
            body.reserve(len);
            body.concat((char*)data, len);

            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                // JSON 解析失败
                request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON格式错误\"}");
                return;
            }

            const char* ssid = doc["ssid"] | "";
            const char* password = doc["password"] | "";

            if (strlen(ssid) == 0) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID不能为空\"}");
                return;
            }

            Serial.printf("[Web] 配网请求: SSID=%s\n", ssid);
            wifi_save_and_connect(ssid, password);

            request->send(200, "application/json", "{\"success\":true}");
        });

    // 设置数码管显示数字
    server.on("/api/display/number", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(len);
            body.concat((char*)data, len);

            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON格式错误\"}");
                return;
            }

            uint16_t number = doc["number"] | 0;
            if (number > 9999) number = 9999;

            Serial.printf("[Web] 设置数字: %d\n", number);
            display_show_number(number);

            request->send(200, "application/json", "{\"success\":true}");
        });

    // 恢复时间显示
    server.on("/api/display/recover", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println(F("[Web] 恢复时间显示"));
        display_set_mode(DISPLAY_TIME);
        request->send(200, "application/json", "{\"success\":true}");
    });

    // 触发动画展示
    server.on("/api/display/animation", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String body;
            body.reserve(len);
            body.concat((char*)data, len);
            StaticJsonDocument<64> doc;
            DeserializationError error = deserializeJson(doc, body);
            if (error || !doc.containsKey("type")) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"缺少type字段\"}");
                return;
            }
            uint8_t type = doc["type"].as<uint8_t>();
            display_show_web_anim(type);
            request->send(200, "application/json", "{\"success\":true}");
        });

    // ============================================================
    // 图案 CRUD
    // ============================================================

    // 列出所有图案
    server.on("/api/patterns", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<2048> doc;
        if (!pm_load_patterns(doc)) {
            request->send(500, "application/json", "{\"error\":\"读取失败\"}");
            return;
        }
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 创建图案
    server.on("/api/patterns", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<512> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("name") || !body.containsKey("data")) {
                request->send(400, "application/json", "{\"error\":\"name和data字段必填\"}");
                return;
            }

            StaticJsonDocument<2048> doc;
            pm_load_patterns(doc);
            JsonArray arr = doc.as<JsonArray>();

            uint8_t newId = 1;
            for (JsonVariant v : arr) {
                uint8_t id = v["id"];
                if (id >= newId) newId = id + 1;
            }

            JsonObject obj = arr.createNestedObject();
            obj["id"] = newId;
            obj["name"] = body["name"].as<String>();
            JsonArray d = obj.createNestedArray("data");
            JsonArray src = body["data"].as<JsonArray>();
            for (JsonVariant v : src) d.add(v.as<uint8_t>());

            if (!pm_save_patterns(doc)) {
                request->send(500, "application/json", "{\"error\":\"保存失败\"}");
                return;
            }
            StaticJsonDocument<64> res;
            res["id"] = newId;
            String out;
            serializeJson(res, out);
            request->send(200, "application/json", out);
        });

    // 更新图案
    server.on("/api/patterns", HTTP_PUT,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<512> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id")) {
                request->send(400, "application/json", "{\"error\":\"id必填\"}");
                return;
            }

            StaticJsonDocument<2048> doc;
            pm_load_patterns(doc);
            JsonArray arr = doc.as<JsonArray>();
            uint8_t targetId = body["id"];

            for (JsonVariant v : arr) {
                if ((uint8_t)v["id"] == targetId) {
                    if (body.containsKey("name")) v["name"] = body["name"].as<String>();
                    if (body.containsKey("data")) {
                        v["data"].as<JsonArray>().clear();
                        JsonArray src = body["data"].as<JsonArray>();
                        for (JsonVariant sv : src) v["data"].add(sv.as<uint8_t>());
                    }
                    pm_save_patterns(doc);
                    request->send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
            request->send(404, "application/json", "{\"error\":\"未找到\"}");
        });

    // 删除图案
    server.on("/api/patterns", HTTP_DELETE,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<128> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id")) {
                request->send(400, "application/json", "{\"error\":\"id必填\"}");
                return;
            }

            StaticJsonDocument<2048> doc;
            pm_load_patterns(doc);
            JsonArray arr = doc.as<JsonArray>();
            uint8_t targetId = body["id"];
            int removeIdx = -1;

            for (int i = 0; i < (int)arr.size(); i++) {
                if ((uint8_t)arr[i]["id"] == targetId) { removeIdx = i; break; }
            }
            if (removeIdx < 0) {
                request->send(404, "application/json", "{\"error\":\"未找到\"}");
                return;
            }
            arr.remove(removeIdx);
            pm_save_patterns(doc);
            request->send(200, "application/json", "{\"success\":true}");
        });

    // ============================================================
    // 内置动画管理（必须在 /api/animations 之前注册！ESPAsyncWebServer 前缀匹配）
    // ============================================================

    // GET /api/animations/builtin — 列出所有内置动画（含默认帧 + 覆写）
    server.on("/api/animations/builtin", HTTP_GET,
        [](AsyncWebServerRequest *request) {
            DynamicJsonDocument doc(8192);
            JsonArray arr = doc.to<JsonArray>();
            for (uint8_t i = 0; i < 10; i++) {
                JsonObject obj = arr.createNestedObject();
                obj["id"] = i;
                obj["name"] = BUILTIN_NAMES[i];

                // 检查是否有覆写
                DynamicJsonDocument ovDoc(2048);
                bool hasOverride = pm_get_builtin_override(i, ovDoc);
                obj["hasOverride"] = hasOverride;
                if (hasOverride) {
                    obj["override"] = ovDoc.as<JsonArray>();
                }

                // 默认帧数据
                JsonArray defFrames = obj["default"].to<JsonArray>();
                display_get_builtin_default_frames(i, defFrames);
            }
            String out;
            serializeJson(doc, out);
            request->send(200, "application/json", out);
        });

    // POST /api/animations/builtin — 保存覆写
    server.on("/api/animations/builtin", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<2048> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id") || !body.containsKey("frames")) {
                request->send(400, "application/json", "{\"error\":\"id和frames必填\"}");
                return;
            }
            uint8_t id = body["id"];
            if (id > 9) {
                request->send(400, "application/json", "{\"error\":\"id 0-9\"}");
                return;
            }
            JsonArray frames = body["frames"].as<JsonArray>();
            DynamicJsonDocument saveDoc(2048);
            saveDoc.set(frames);
            if (pm_set_builtin_override(id, saveDoc)) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(500, "application/json", "{\"error\":\"保存失败\"}");
            }
        });

    // DELETE /api/animations/builtin — 删除覆写
    server.on("/api/animations/builtin", HTTP_DELETE,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<128> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id")) {
                request->send(400, "application/json", "{\"error\":\"id必填\"}");
                return;
            }
            uint8_t id = body["id"];
            if (id > 9) {
                request->send(400, "application/json", "{\"error\":\"id 0-9\"}");
                return;
            }
            pm_delete_builtin_override(id);
            request->send(200, "application/json", "{\"success\":true}");
        });

    // ============================================================
    // 动画 CRUD
    // ============================================================

    // 列出所有动画
    server.on("/api/animations", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<4096> doc;
        if (!pm_load_animations(doc)) {
            request->send(500, "application/json", "{\"error\":\"读取失败\"}");
            return;
        }
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 创建动画
    server.on("/api/animations", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<1024> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("name") || !body.containsKey("frames")) {
                request->send(400, "application/json", "{\"error\":\"name和frames字段必填\"}");
                return;
            }

            StaticJsonDocument<4096> doc;
            pm_load_animations(doc);
            JsonArray arr = doc.as<JsonArray>();

            uint8_t newId = 1;
            for (JsonVariant v : arr) {
                uint8_t id = v["id"];
                if (id >= newId) newId = id + 1;
            }

            JsonObject obj = arr.createNestedObject();
            obj["id"] = newId;
            obj["name"] = body["name"].as<String>();
            obj["frames"] = body["frames"].as<JsonArray>();

            if (!pm_save_animations(doc)) {
                request->send(500, "application/json", "{\"error\":\"保存失败\"}");
                return;
            }
            StaticJsonDocument<64> res;
            res["id"] = newId;
            String out;
            serializeJson(res, out);
            request->send(200, "application/json", out);
        });

    // 更新动画
    server.on("/api/animations", HTTP_PUT,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<1024> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id")) {
                request->send(400, "application/json", "{\"error\":\"id必填\"}");
                return;
            }

            StaticJsonDocument<4096> doc;
            pm_load_animations(doc);
            JsonArray arr = doc.as<JsonArray>();
            uint8_t targetId = body["id"];

            for (JsonVariant v : arr) {
                if ((uint8_t)v["id"] == targetId) {
                    if (body.containsKey("name")) v["name"] = body["name"].as<String>();
                    if (body.containsKey("frames")) v["frames"] = body["frames"].as<JsonArray>();
                    pm_save_animations(doc);
                    request->send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
            request->send(404, "application/json", "{\"error\":\"未找到\"}");
        });

    // 删除动画
    server.on("/api/animations", HTTP_DELETE,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<128> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("id")) {
                request->send(400, "application/json", "{\"error\":\"id必填\"}");
                return;
            }

            StaticJsonDocument<4096> doc;
            pm_load_animations(doc);
            JsonArray arr = doc.as<JsonArray>();
            uint8_t targetId = body["id"];
            int removeIdx = -1;

            for (int i = 0; i < (int)arr.size(); i++) {
                if ((uint8_t)arr[i]["id"] == targetId) { removeIdx = i; break; }
            }
            if (removeIdx < 0) {
                request->send(404, "application/json", "{\"error\":\"未找到\"}");
                return;
            }
            arr.remove(removeIdx);
            pm_save_animations(doc);
            request->send(200, "application/json", "{\"success\":true}");
        });

    // ============================================================
    // 显示图案/动画
    // ============================================================

    // 显示单帧图案（POST /api/display/pattern）
    server.on("/api/display/pattern", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<128> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("data")) {
                request->send(400, "application/json", "{\"error\":\"data字段必填\"}");
                return;
            }
            JsonArray arr = body["data"].as<JsonArray>();
            if (arr.size() != 4) {
                request->send(400, "application/json", "{\"error\":\"data需要4个字节\"}");
                return;
            }
            uint8_t d[4];
            for (int i = 0; i < 4; i++) d[i] = arr[i];
            display_show_raw(d);
            request->send(200, "application/json", "{\"success\":true}");
        });

    // 播放动画（POST /api/display/anim-play）    // body: {"frames": [{"data":[4], "duration":ms}, ...]}
    server.on("/api/display/anim-play", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<2048> body;
            DeserializationError err = deserializeJson(body, data, len);
            if (err || !body.containsKey("frames")) {
                request->send(400, "application/json", "{\"error\":\"frames字段必填\"}");
                return;
            }
            display_play_user_anim(body["frames"].as<JsonArray>());
            request->send(200, "application/json", "{\"success\":true}");
        });

    // 获取配置
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", buildConfigJson());
    });

    // 保存配置
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleConfigPost(request, data, len);
        });

    // 重启设备
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println(F("[Web] log"));
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
        request->send(resp);
        delay(1000);
        ESP.restart();
    });

    // ============================================================
    // 文件管理 API
    // ============================================================

    // 列出文件
    server.on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<2048> doc;
        JsonArray files = doc.to<JsonArray>();
        File root = LittleFS.open("/");
        if (root) {
            File f = root.openNextFile();
            while (f) {
                JsonObject obj = files.createNestedObject();
                obj["name"] = String(f.name());
                obj["size"] = f.size();
                f = root.openNextFile();
            }
        }
        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // 删除文件
    server.on("/api/fs/delete", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<128> doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc.containsKey("name")) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"缺少name字段\"}");
                return;
            }
            const char* name = doc["name"];
            bool ok = LittleFS.remove(name);
            Serial.printf("[Web] 删除文件: %s -> %s\n", name, ok ? "OK" : "FAIL");
            request->send(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
        });

    // 上传文件（支持多文件 multipart）
    server.on("/api/fs/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(200, "application/json", "{\"success\":true}");
        },
        [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                String path = "/" + filename;
                // 去掉路径中的目录穿越
                path.replace("/../", "/");
                Serial.printf("[Web] 开始上传: %s\n", path.c_str());
                File* f = new File(LittleFS.open(path, FILE_WRITE));
                request->_tempObject = (void*)f;
            }
            File* f = (File*)request->_tempObject;
            if (f && *f && len) {
                f->write(data, len);
            }
            if (final && f) {
                f->close();
                delete f;
                request->_tempObject = nullptr;
                Serial.println(F("[Web] 上传完成"));
            }
        });

    // --- 静态文件服务（LittleFS）---
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // --- 启动服务器 ---
    server.begin();
    serverRunning = true;
    Serial.println(F("[Web] HTTP 服务器已启动 (端口80)"));
}

void web_server_stop() {
    if (serverRunning) {
        server.end();
        serverRunning = false;
        Serial.println(F("[Web] HTTP 服务器已停止"));
    }

    if (mdnsRunning) {
        MDNS.end();
        mdnsRunning = false;
        Serial.println(F("[Web] mDNS 已停止"));
    }
}

void web_server_start_mdns() {
    if (mdnsRunning) return;

    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsRunning = true;
        Serial.printf("[Web] mDNS 已启动: http://%s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println(F("[Web] mDNS 启动失败"));
    }
}


