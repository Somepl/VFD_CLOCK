# AGENTS.md — ESP32 数码管时钟

## Project overview

PlatformIO + Arduino project for an ESP32-WROOM-32UE (16MB Flash) 4-digit 7-segment tube desk clock with WiFi provisioning, NTP time sync, weather display, capacitive touch buttons, OTA updates, and a retro web UI.

Architecture: single-thread `loop() + millis()` non-blocking.

## Developer commands (PlatformIO)

```bash
pio run                    # build
pio run -t upload          # wired flash (hold IO0 → EN → release IO0 → release IO0)
pio run -t upload --upload-port 192.168.x.x   # OTA flash (device must be on WiFi with ArduinoOTA running)
pio run -t uploadfs        # flash LittleFS web files (data/ directory) — USB only, OTA unreliable for 10MB partition
pio device monitor -b 115200   # serial monitor
```

> **OTA 仅限已烧录过一次的设备**。首次烧录必须用 USB 有线方式。OTA 失败时（"No response from device"）说明设备上运行的是旧固件或无 OTA 支持，需先用有线烧录一次。

### 烧录方案（日常工作流）

| 目标 | 命令/方式 | 途径 | 说明 |
|------|-----------|------|------|
| 固件 | `pio run -t upload --upload-port <IP>` | OTA | 3MB 分区，OTA 稳定可靠 |
| 网页文件 | 浏览器打开 `http://<IP>/fs.html` | HTTP 上传 | 通过 `/api/fs/upload` 逐个上传 `data/` 目录中的文件 |
| 全量更新 | 先 OTA 固件，再网页上传 data 文件 | 全远程 | 无需插 USB |

> 网页上传步骤：打开设备 `fs.html` → 选择 `data/` 中的文件 → 点击上传。文件管理页也支持删除和列出已上传文件。
>
> 也可用 curl 直接上传（适合自动化/AI 操作）：
> ```bash
> curl.exe -F "file=@data/index.html" http://<IP>/api/fs/upload
> curl.exe -F "file=@data/creator.html" http://<IP>/api/fs/upload
> # 也可一次传多个文件
> ```
>
> 旧方案 `pio run -t uploadfs`（USB 有线烧写 LittleFS 分区）已废弃，改用网页 API 上传，更灵活且无需物理接触设备。

- 设备 mDNS 主机名 `clock`，通常可通过 `clock.local` 解析，但 Windows 可能不支持 mDNS；建议用路由器 DHCP 列表确认 IP。
- OTA 前确保串口监视器已关闭（串口独占）。

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
├── src/                    # 12 .cpp files, 4 display_* modules from refactor
│   ├── main.cpp            # setup() + loop() entrypoint
│   ├── display_driver.cpp  # 74HC595 hardware layer + PWM ISR + segment mapping
│   ├── display_config.cpp  # NVS config persistence (brightness, night, btn3)
│   ├── display_anim.cpp    # Animation frame data + state machines
│   ├── display_manager.cpp # Coordinator: RTC, mode switching, time/weather display
│   ├── wifi_manager.cpp    # WiFi provisioning + state machine
│   ├── ntp_sync.cpp        # NTP sync + software RTC fallback cache
│   ├── weather_client.cpp  # Weather API client (non-blocking HTTP)
│   ├── button_handler.cpp  # Capacitive touch button processing
│   ├── web_server.cpp      # HTTP API + static file server (704 lines, candidate for splitting)
│   ├── pattern_manager.cpp # LittleFS pattern/animation CRUD
│   └── remote_client.cpp   # MQTT + Worker HTTP remote control
├── data/                   # LittleFS web files (index.html, wifi.html, etc.)
└── YS18-3-for-yi-main(Old code)/   # old codebase (reference only)
```

> **Display Manager Refactor**: The original ~1200-line `display_manager.cpp` was split into 4 modules (driver, config, anim, coordinator) for maintainability. Each module has its own `.h`/`.cpp` pair.

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
- **Web files must be re-flashed** via web upload at `http://<IP>/fs.html` after any change to `data/`.
- **API keys in config.h** — do not commit secrets. Keys are for 心知天气 (weather) and 高德 (IP geolocation) APIs.
- **I2C pin fix**: SDA=22, SCL=21 (not the reverse — was a prior bug).
- **Touch threshold**: `TOUCH_PRESS_MARGIN=1` (acrylic overlay delta only 3-4 counts), `touchSetCycles(4000,2000)`, auto-calibrate on boot, IIR baseline tracking, noise spike rejection.
- **AP SSID** `Clock-Setup` (open, no password), auto-closes after 3 min idle.
- **mDNS hostname**: `clock` → accessible at `http://clock.local`.
- **Weather animations**: 6 types mapped by keyword in weather text (晴/云/雨/雪/雷/其他). "阴" also maps to Cloudy. Unmatched weather skips animation, shows temperature directly.
- **Temperature display**: tube1=± sign (blank for positive, `char_to_segments('-')` for negative), tube2-3=digits, tube4=°C symbol (`char_to_segments(0xB0)`). Magic numbers `0x39`/`0xFD`/`B11111101` etc. have been unified into `char_to_segments()`.
- **NTP interval**: 6 hours, UTC+8, server `pool.ntp.org`.
- **Software RTC fallback**: NTP sync caches Unix time + millis() base point. If DS3231 fails, `sw_rtc_get_hh_mm()` computes time from the cache, preventing `----` display.
- **Old code** in `YS18-3-for-yi-main(Old code)/` should not be modified — it's reference only.
- **Builtin animation overrides** stored in Preferences NVS with keys `ov0`-`ov9` (JSON strings), not LittleFS.
- **Button 3 animation config** stored in NVS: `btn3_type` (0=off, 1=builtin, 2=user) and `btn3_id` (animation index/ID).
- **NVS keys**: `btn3_type`, `btn3_id` in `config.h:121-122`.

## Button mapping

| Button | Short press | Long press |
|--------|-------------|------------|
| btn1 (T0/GPIO4) | Toggle display on/off; if flashing → stop flash | (none) |
| btn2 (T2/GPIO2) | Weather fetch; if flashing → stop flash | Toggle temp unit ℃/℉ |
| btn3 (T15/GPIO15) | Play configured animation; if flashing → stop flash | Toggle AP hotspot |

> 任意按键短按或长按都会先检查闪烁状态，如果正在闪烁则退出闪烁，不执行原本的功能。

## Flash notification mode

通过 HTTP API 或 MQTT 触发数码管闪烁（500ms 交替显示/全灭），用于通知用户需要查看/确认。

- **触发**: `POST /api/display/flash` 或 MQTT `{"cmd":"flash_start"}`
- **停止**: `POST /api/display/flash-stop`、MQTT `{"cmd":"flash_stop"}`、或任意物理按键
- **闪烁覆盖层**：不改变当前显示模式，在 `display_anim_tick()` 最开头拦截，冻结所有动画 + PWM
- **MQTT 主题**: `clock/{password}/cmd`，走公网 broker（当前 `broker.emqx.io:1883`），内外网均可访问
- **本地调试脚本**: `flash.py start` / `flash.py stop`

## ASCII art alignment

All `<pre class="logo">` blocks across web pages (index.html line 43, creator.html line 80, patterns.html line 42, touch.html line 42) have trailing spaces on shorter lines to ensure right-edge alignment in monospace font. This prevents the "歪歪扭扭" appearance.

## Loop execution order (main.cpp)

Each `loop()` iteration runs: `ArduinoOTA.handle()` → `button_update()` → `wifi_update()` → `ntp_update()` → `weather_update()` → `display_anim_tick()` → `display_update()` (every 1s via `DISPLAY_REFRESH_MS`).  

翻页动画已改为非阻塞状态机（`display_anim_tick()` 每圈推进一帧），不再使用 `delay()` 阻塞主循环。天气 HTTP 请求也改为 `WiFiClient` 非阻塞轮询，`connect()` 短暂阻塞后响应数据在 `weather_update()` 中分多次读取。

闪烁通知模式下，`display_anim_tick()` 最开头拦截并处理闪烁切换，跳过所有动画帧推进和 PWM。

按键3短按调用 `display_play_btn3_anim()`，根据 NVS 存储的 `btn3_type` 和 `btn3_id` 播放对应动画。内置动画直接用 `display_show_web_anim()`，用户动画从 `/animations.json` 读取后调用 `display_play_user_anim()`。

WiFi 扫描使用 `WiFi.scanNetworks(true)` + `delay(100)` 轮询，避免同步扫描阻塞 `async_tcp` 任务导致看门狗重启。

## Web API (AsyncWebServer on port 80)

| Method | Path                     | Purpose              |
|--------|--------------------------|----------------------|
| GET    | `/api/status`            | System status JSON (wifiState, ip, staConnected, brightness, currentTime, flashActive) |
| GET    | `/api/config`            | Full config JSON (brightness, night, touch, remote, rtcTemp, btn3Anim*) |
| POST   | `/api/config`            | Save config fields (brightness, nightEnabled/Start/End, touchThresholds, touchHysteresis, remoteUrl, remotePassword, btn3AnimType/Id) |
| GET    | `/api/wifi/scan`         | Scan nearby networks (async, use polling with delay(100)) |
| POST   | `/api/wifi/connect`      | Save & connect WiFi  |
| GET    | `/api/wifi/saved`        | List saved WiFi credentials |
| POST   | `/api/wifi/forget`       | Delete saved WiFi credentials |
| POST   | `/api/display/number`    | Show number on tubes |
| POST   | `/api/display/pattern`   | Show raw segment pattern |
| POST   | `/api/display/animation` | Play built-in web animation (`type` 0-5) |
| POST   | `/api/display/anim-play` | Play user animation (frame array) |
| POST   | `/api/display/recover`   | Revert to time mode  |
| POST   | `/api/display/flash`     | Start flash notification (500ms blink, freeze current content) |
| POST   | `/api/display/flash-stop`| Stop flash, restore time display |
| GET    | `/api/patterns`          | List saved patterns |
| POST   | `/api/patterns`          | Save pattern |
| DELETE | `/api/patterns`          | Delete pattern |
| GET    | `/api/animations`        | List saved user animations |
| POST   | `/api/animations`        | Save user animation |
| DELETE | `/api/animations`        | Delete user animation |
| GET    | `/api/animations/builtin`| List 10 builtins with override status |
| POST   | `/api/animations/builtin`| Save override for builtin animation |
| DELETE | `/api/animations/builtin`| Restore builtin to default |
| POST   | `/api/fs/upload`         | Upload file to LittleFS |
| GET    | `/api/fs/list`           | List LittleFS files |
| POST   | `/api/fs/delete`         | Delete file from LittleFS |
| POST   | `/api/restart`           | Restart device |
