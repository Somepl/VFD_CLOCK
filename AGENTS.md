# AGENTS.md — ESP32 数码管时钟

## Project overview

PlatformIO + Arduino project for an ESP32-WROOM-32UE (16MB Flash) 4-digit 7-segment tube desk clock with WiFi provisioning, NTP time sync, weather display, capacitive touch buttons, OTA updates, and a retro web UI.

Architecture: single-thread `loop() + millis()` non-blocking.

## Developer commands (PlatformIO)

```bash
pio run                    # build
pio run -t upload          # wired flash (hold IO0 → EN → release IO0 → release IO0)
pio run -t upload --upload-port 192.168.x.x   # OTA flash (device must be on WiFi with ArduinoOTA running)
pio run -t uploadfs        # flash LittleFS web files (data/ directory)
pio device monitor -b 115200   # serial monitor
```

> **OTA 仅限已烧录过一次的设备**。首次烧录必须用 USB 有线方式。OTA 失败时（"No response from device"）说明设备上运行的是旧固件或无 OTA 支持，需先用有线烧录一次。

- Build uses custom `esptool.cfg` (hardware lacks DTR/RTS auto-reset).
- Pre-build script `esptool_cfg.py` 仅在 `upload_protocol = esptool` 时注入 `--configfile`，`espota` 协议下跳过。
- Flash size: 16MB with custom partition table (`partitions/default_16MB.csv`).
- OTA slots: app0 (0x10000, 3MB) + app1 (0x310000, 3MB).
- LittleFS partition labeled `spiffs` (ESP32 core quirk — must match).

## Directory layout

```
DEMO/
├── platformio.ini          # project config (single env: esp32dev)
├── partitions/             # 16MB flash partition CSV
├── include/                # headers (config.h has all pins & API keys)
├── src/                    # 7 .cpp files, each a self-contained module
│   └── main.cpp            # setup() + loop() entrypoint
├── data/                   # LittleFS web files (index.html, wifi.html, etc.)
└── YS18-3-for-yi-main(Old code)/   # old codebase (reference only)
```

## Key hardware pins (config.h)

| Function       | Pin  |
|----------------|------|
| 74HC595 DATA   | 14   |
| 74HC595 LATCH  | 32   |
| 74HC595 CLK    | 33   |
| DS3231 SDA     | 22   |
| DS3231 SCL     | 21   |
| Touch btn 1    | T0/4 |
| Touch btn 2    | T2/2 |
| Touch btn 3    | T3/15|

## Critical rules

- **每次重大变更前必须自动 git 归档（commit）到当前版本**，经过用户确认后再动手改代码。
- **所有修改必须先问用户确认方案**，不得直接执行。
- **禁止使用 edit 工具修改带中文的文件** — edit 工具有 UTF-8 写入 bug，会破坏中文字符。改用 write 工具全量重写或用户自行编辑。

## Important quirks & constraints

- **No test setup** — this is an embedded project; no unit tests exist.
- **No lint/format/typecheck** config — none applicable.
- **Web files must be re-flashed** via `pio run -t uploadfs` after any change to `data/`.
- **API keys in config.h** — do not commit secrets. Keys are for 心知天气 (weather) and 高德 (IP geolocation) APIs.
- **I2C pin fix**: SDA=22, SCL=21 (not the reverse — was a prior bug).
- **Touch threshold** `TOUCH_THRESHOLD=40`: touch readings drop from ~80 to <20 on press.
- **AP SSID** `Clock-Setup` (open, no password), auto-closes after 3 min idle.
- **mDNS hostname**: `clock` → accessible at `http://clock.local`.
- **Weather animations**: 6 types mapped by keyword in weather text (晴/云/雨/雪/雷/其他).
- **NTP interval**: 6 hours, UTC+8, server `pool.ntp.org`.
- **Old code** in `YS18-3-for-yi-main(Old code)/` should not be modified — it's reference only.

## Loop execution order (main.cpp)

Each `loop()` iteration runs: `ArduinoOTA.handle()` → `button_update()` → `wifi_update()` → `ntp_update()` → `weather_update()` → `display_anim_tick()` → `display_update()` (every 1s via `DISPLAY_REFRESH_MS`).  

翻页动画已改为非阻塞状态机（`display_anim_tick()` 每圈推进一帧），不再使用 `delay()` 阻塞主循环。天气 HTTP 请求也改为 `WiFiClient` 非阻塞轮询，`connect()` 短暂阻塞后响应数据在 `weather_update()` 中分多次读取。

## Web API (AsyncWebServer on port 80)

| Method | Path                     | Purpose              |
|--------|--------------------------|----------------------|
| GET    | `/api/status`            | System status JSON   |
| GET    | `/api/wifi/scan`         | Scan nearby networks |
| POST   | `/api/wifi/connect`      | Save & connect WiFi  |
| POST   | `/api/display/number`    | Show number on tubes |
| POST   | `/api/display/recover`   | Revert to time mode  |
