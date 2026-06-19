#include "weather_client.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ============================================================
// 全局状态
// ============================================================

static WeatherFetchState fetchState = WEATHER_IDLE;

// 温度单位
static TempUnit tempUnit = TEMP_CELSIUS;

// 缓存的天气数据
static String  cachedCity = DEFAULT_CITY;
static String  cachedWeatherText = "未知";
static int16_t cachedTempC = 0;
static bool    weatherAvailable = false;

// 错误标记
static bool fetchRequested = false;

// ============================================================
// 非阻塞 HTTP 子状态机
// ============================================================

#define HTTP_IDLE       0
#define HTTP_CONNECT    1
#define HTTP_RECV       2
#define HTTP_DONE       3
#define HTTP_ERROR     -1

static WiFiClient      httpTcpClient;
static WiFiClientSecure httpSslClient;
static bool httpUseSsl = false;
static int8_t httpStage = HTTP_IDLE;
static unsigned long httpTimeoutEnd = 0;
static String httpHost;
static int httpPort;
static String httpPath;
static String httpResponse;

static void http_start(const char* host, int port, const char* path, bool useSsl, int timeoutMs) {
    httpTcpClient.stop();
    httpSslClient.stop();
    httpUseSsl = useSsl;
    httpStage = HTTP_CONNECT;
    httpHost = host;
    httpPort = port;
    httpPath = path;
    httpResponse = String();
    httpTimeoutEnd = millis() + timeoutMs;
}

// Returns: -1=busy, 0=error/timeout, 1=done (response in httpResponse)
static int http_poll() {
    if (httpStage == HTTP_ERROR) return 0;
    if (httpStage == HTTP_DONE) return 1;

    unsigned long now = millis();
    WiFiClient* c = httpUseSsl ? (WiFiClient*)&httpSslClient : &httpTcpClient;

    if (httpStage == HTTP_CONNECT) {
        if (c->connected()) {
            httpStage = HTTP_RECV;
        } else if (c->available()) {
            httpStage = HTTP_RECV;
        } else {
            if (c->connect(httpHost.c_str(), httpPort)) {
                httpStage = HTTP_RECV;
            } else {
                if (now >= httpTimeoutEnd) {
                    c->stop();
                    httpStage = HTTP_ERROR;
                    return 0;
                }
                return -1;
            }
        }
        if (httpStage == HTTP_RECV) {
            c->print(String("GET ") + httpPath + " HTTP/1.1\r\n"
                     "Host: " + httpHost + "\r\n"
                     "User-Agent: ESP32-Clock/1.0\r\n"
                     "Connection: close\r\n\r\n");
            httpTimeoutEnd = now + 3000;
        }
        return -1;
    }

    if (httpStage == HTTP_RECV) {
        while (c->available()) {
            httpResponse += (char)c->read();
        }
        if (!c->connected()) {
            c->stop();
            httpStage = HTTP_DONE;
            return 1;
        }
        if (now >= httpTimeoutEnd) {
            c->stop();
            httpStage = HTTP_ERROR;
            return 0;
        }
    }

    return -1;
}

// ============================================================
// 内部辅助：摄氏度 -> 华氏度
// ============================================================

static int16_t celsius_to_fahrenheit(int16_t c) {
    return (int16_t)(c * 9.0 / 5.0 + 32.0);
}

// ============================================================
// 内部：提取 HTTP 响应体（跳过 HTTP 头部）
// ============================================================

static String extract_body(const String& raw) {
    int idx = raw.indexOf("\r\n\r\n");
    if (idx < 0) idx = raw.indexOf("\n\n");
    if (idx < 0) return raw;
    return raw.substring(idx + (raw[idx+2] == '\r' ? 4 : 2));
}

// ============================================================
// 内部：加载保存温度单位（NVS）
// ============================================================

static void load_temp_unit() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    tempUnit = (TempUnit)prefs.getUChar(PREFS_KEY_TEMP_UNIT, TEMP_CELSIUS);
    prefs.end();
}

static void save_temp_unit() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_KEY_TEMP_UNIT, (uint8_t)tempUnit);
    prefs.end();
}

// ============================================================
// 公开接口实现
// ============================================================

void weather_init() {
    Serial.println(F("[天气] log"));
    httpSslClient.setInsecure();
    load_temp_unit();
    fetchState = WEATHER_IDLE;
    fetchRequested = false;
    weatherAvailable = false;

    Serial.printf("[天气] API Key: %s\n", WEATHER_API_KEY);
    Serial.printf("[天气] 温度单位: %s\n", tempUnit == TEMP_CELSIUS ? "℃" : "℉");
}

void weather_update() {
    switch (fetchState) {

    case WEATHER_IDLE:
        if (fetchRequested) {
            fetchRequested = false;

            if (!wifi_is_sta_connected()) {
                Serial.println(F("[天气] WiFi未连接，无法获取天气"));
                fetchState = WEATHER_FAILED;
                break;
            }

            // 第一步：IP 定位
#if ENABLE_AMAP_LOCATION
            if (strlen(AMAP_API_KEY) > 0) {
                Serial.printf("[天气] 使用高德IP定位\n");
                String path = String("/v3/ip?key=") + AMAP_API_KEY;
                http_start("restapi.amap.com", 443, path.c_str(), true, 4000);
            } else
#endif
            {
                Serial.printf("[天气] 使用 ip-api.com 定位\n");
                http_start("ip-api.com", 80, "/json", false, 3000);
            }
            fetchState = WEATHER_FETCH_IP;
        }
        break;

    case WEATHER_FETCH_IP: {
        int ret = http_poll();
        if (ret == -1) break; // still in progress

        httpTcpClient.stop();
        httpSslClient.stop();

        if (ret == 1) {
            String body = extract_body(httpResponse);
            Serial.printf("[天气] IP定位响应: %s\n", body.c_str());

            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, body);

            if (!err) {
#if ENABLE_AMAP_LOCATION
                if (httpUseSsl) {
                    if (doc["status"] == "1") {
                        String city = doc["city"].as<String>();
                        String province = doc["province"].as<String>();
                        cachedCity = (city.length() > 0) ? city : province;
                    }
                } else
#endif
                {
                    cachedCity = doc["city"].as<String>();
                }

                if (cachedCity.length() > 0) {
                    Serial.printf("[天气] 定位成功: %s\n", cachedCity.c_str());
                } else {
                    Serial.println(F("[天气] IP定位返回城市为空"));
                    cachedCity = DEFAULT_CITY;
                }
            } else {
                Serial.printf("[天气] IP定位JSON解析失败: %s\n", err.c_str());
                cachedCity = DEFAULT_CITY;
            }
        } else {
            Serial.printf("[天气] IP定位失败，使用默认城市 %s\n", DEFAULT_CITY);
            cachedCity = DEFAULT_CITY;
        }

        // 第二步：获取天气数据
        {
            String path = "/v3/weather/now.json?key=";
            path += WEATHER_API_KEY;
            path += "&location=";
            path += cachedCity;
            path += "&language=zh-Hans&unit=c";
            http_start("api.seniverse.com", 80, path.c_str(), false, 3000);
        }
        fetchState = WEATHER_FETCH_DATA;
        break;
    }

    case WEATHER_FETCH_DATA: {
        int ret = http_poll();
        if (ret == -1) break;

        httpTcpClient.stop();
        httpSslClient.stop();

        if (ret == 1) {
            String body = extract_body(httpResponse);
            Serial.printf("[天气] API响应: %s\n", body.c_str());

            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, body);

            if (!err) {
                JsonVariant text = doc["results"][0]["now"]["text"];
                JsonVariant temp = doc["results"][0]["now"]["temperature"];

                if (text.is<const char*>()) {
                    cachedWeatherText = text.as<String>();
                } else {
                    Serial.println(F("[天气] 警告：天气文字字段异常"));
                    cachedWeatherText = "未知";
                }

                if (temp.is<int>() || temp.is<float>() || temp.is<const char*>()) {
                    cachedTempC = temp.as<int16_t>();
                    Serial.printf("[天气] 温度原始值 %s\n", temp.as<String>().c_str());
                } else {
                    Serial.printf("[天气] 警告：温度字段异常，使用缓存\n");
                }

                weatherAvailable = true;
                fetchState = WEATHER_DONE;
                Serial.printf("[天气] 获取成功: %s, %d℃\n",
                              cachedWeatherText.c_str(), cachedTempC);
            } else {
                Serial.printf("[天气] 天气JSON解析失败: %s\n", err.c_str());
                fetchState = WEATHER_FAILED;
            }
        } else {
            Serial.printf("[天气] 天气请求超时或失败\n");
            fetchState = WEATHER_FAILED;
        }
        break;
    }

    case WEATHER_DONE: {
        int16_t displayTemp = (tempUnit == TEMP_FAHRENHEIT) ?
                               celsius_to_fahrenheit(cachedTempC) :
                               cachedTempC;
        display_show_weather_with_anim(cachedWeatherText.c_str(), displayTemp);
        Serial.printf("[天气] 显示温度: %d\n", displayTemp);
        fetchState = WEATHER_IDLE;
        break;
    }

    case WEATHER_FAILED:
        if (weatherAvailable) {
        int16_t displayTemp = (tempUnit == TEMP_FAHRENHEIT) ?
                               celsius_to_fahrenheit(cachedTempC) :
                               cachedTempC;
            display_show_weather(displayTemp);
        } else {
            display_show_weather(-99);
        }
        Serial.println(F("[天气] 获取失败，使用缓存或显示错误"));
        fetchState = WEATHER_IDLE;
        break;
    }
}

void weather_fetch() {
    if (fetchState != WEATHER_IDLE) {
        Serial.println(F("[天气] 正在获取中，请稍候"));
        return;
    }
    fetchRequested = true;
    Serial.println(F("[天气] 触发获取"));
}

void weather_toggle_unit() {
    tempUnit = (tempUnit == TEMP_CELSIUS) ? TEMP_FAHRENHEIT : TEMP_CELSIUS;
    save_temp_unit();
    Serial.printf("[天气] 单位切换为 %s\n",
                  tempUnit == TEMP_CELSIUS ? "C" : "F");

    if (display_get_mode() == DISPLAY_WEATHER && weatherAvailable) {
        int16_t displayTemp = (tempUnit == TEMP_FAHRENHEIT) ?
                               celsius_to_fahrenheit(cachedTempC) :
                               cachedTempC;
        display_show_weather(displayTemp);
    }
}

int16_t weather_get_temperature() {
    if (!weatherAvailable) return 0;
    return (tempUnit == TEMP_FAHRENHEIT) ?
            celsius_to_fahrenheit(cachedTempC) :
            cachedTempC;
}

const char* weather_get_text() {
    return weatherAvailable ? cachedWeatherText.c_str() : "未知";
}

WeatherFetchState weather_get_state() {
    return fetchState;
}


