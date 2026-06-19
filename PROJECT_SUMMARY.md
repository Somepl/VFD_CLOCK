# ESP32 数码管时钟 — 项目总结文档

> 最后更新：2026-06-12

---

## 1. 项目概述

基于 **ESP32-WROOM-32UE (16MB Flash)** 的 4 位数码管桌面时钟。具备 WiFi 配网、NTP 自动校时、心知天气显示、3 个电容触摸按键、OTA 无线更新、白色像素复古风格 Web 管理界面。

- **框架**：PlatformIO + Arduino
- **架构**：单线程 `loop()` + `millis()` 非阻塞
- **显示**：4 位共阳极 7 段数码管（74HC595 驱动）
- **时钟源**：DS3231 RTC（I2C）+ NTP 互联网校时

---

## 2. 硬件引脚

| 功能 | 引脚 | 说明 |
|------|------|------|
| **74HC595 DATA** | GPIO14 | 数据线 (SER) |
| **74HC595 LATCH** | GPIO32 | 锁存线 (RCLK) |
| **74HC595 CLK** | GPIO33 | 时钟线 (SRCLK) |
| **DS3231 SDA** | GPIO22 | I2C 数据线 |
| **DS3231 SCL** | GPIO21 | I2C 时钟线 |
| **触摸按键1** | T0 (GPIO4) | WiFi 配网控制 |
| **触摸按键2** | T2 (GPIO2) | 天气显示 |
| **触摸按键3** | T3 (GPIO15) | 屏幕开关 |

---

## 3. 硬件 BOM（V0.02）

数据来源：`BOM_Board_V0.02_Schematic1_1_2026-06-12.xlsx`

### 核心芯片

| 位号 | 型号 | 封装 | 说明 |
|------|------|------|------|
| U7 | ESP32-WROOM-32UE (16MB) | SMD-38P | 主控 MCU + WiFi/蓝牙 |
| U6 | DS3231M+TRL | SOIC-16 | RTC 实时时钟（I²C） |
| U5 | CP2102-GMR | WQFN-28 | USB-UART 桥接芯片 |
| U15, U17, U19, U21 | SN74HC595PWR | TSSOP-16 | 8 位移位寄存器 ×4 |
| U16, U18, U20, U22 | ULN2803A | QFN-20 | 8 路达林顿阵列 ×4（位驱动） |

### 电源

| 位号 | 型号 | 封装 | 说明 |
|------|------|------|------|
| U1 | AMS1117-3.3 | SOT-223 | 3.3V LDO 稳压 |
| U3 | MT3608 | SOT-23-6 | 升压转换器（数码管供电） |
| U24 | TLV62569DBVR | SOT-23-5 | 降压转换器 |
| D1 | 1N5817WS | SOD-323 | 肖特基二极管 |
| F1 | ASMD1210-200 | F1210 | 自恢复保险丝 200mA |

### 电感

| 位号 | 值 | 封装 |
|------|-----|------|
| L1 | 22µH | L1206 |
| L2 | 2.2µH | IND-SMD_L3.5-W3.0 |

### 电容

| 位号 | 数量 | 值 | 封装 |
|------|------|-----|------|
| C1, C2, C23, C25, C29 | 5 | 22µF | C0603 |
| C3, C7, C9, C13-C20, C27, C28 | 13 | 100nF | C0603 |
| C8, C10, C11, C12 | 4 | 100nF | C0603 |
| C21 | 1 | 10µF | C0603 |
| C26 | 1 | 4.7µF | C0603 |

### 电阻

| 位号 | 数量 | 值 | 封装 | 说明 |
|------|------|-----|------|------|
| R1, R2, R6-R9 | 6 | 10kΩ | R0603 | 上下拉电阻 |
| R3 | 1 | 1kΩ | R0603 | |
| R4, R5 | 2 | 5.1kΩ | R0603 | I²C 上拉 |
| R23 | 1 | 100kΩ | 3224W | 可调电阻 |
| VR1 | 1 | 5kΩ | 3224W | 可调电阻（对比度/亮度） |
| RN9-RN16 | 8 | 10kΩ×4 | 0603×8P | 排阻（LED 段限流） |

### 其他

| 位号 | 型号 | 封装 | 说明 |
|------|------|------|------|
| USB3 | TYPE-C 16PIN 2MD(073) | USB-C-SMD | USB-C 接口（烧录+供电） |
| B2 | CR1220-2 | BAT-SMD | RTC 备用电池座 |
| FLASH1, RST1 | 1TS001G | SMD-4P | 微动按键 ×2（固件烧录/复位） |
| Q1, Q2 | S8050 | SOT-23 | NPN 三极管 |
| Y1-Y4 | ys18-3 | ys18-3 | 4 位 7 段数码管模块 |

### 电路架构简析

```
USB-C → CP2102 → ESP32 ──→ 4× 74HC595 (段驱动) ──→ 4× ULN2803 (位驱动) ──→ 4× ys18-3 数码管
                   │
                   └──→ DS3231 (I²C)
                        
电源：USB 5V → MT3608 升压 → AMS1117-3.3 / TLV62569 降压
RTC 备用：CR1220 纽扣电池
```

---

## 4. 项目文件结构

```
DEMO/
├── platformio.ini              # PlatformIO 项目配置
├── esptool_cfg.py              # esptool 自定义配置
├── esptool.cfg                 # esptool 配置文件
├── PROJECT_SUMMARY.md          # 本文档
├── partitions/
│   └── default_16MB.csv        # 16MB Flash 分区表
├── include/
│   ├── config.h                # 集中管理所有引脚、阈值、常量
│   ├── display_manager.h       # 数码管显示管理器接口
│   ├── button_handler.h        # 触摸按键处理器接口
│   ├── wifi_manager.h          # WiFi 连接管理器接口
│   ├── web_server.h            # 网页服务器 + mDNS 接口
│   ├── weather_client.h        # 心知天气客户端接口
│   └── ntp_sync.h              # NTP 校时管理器接口
├── src/
│   ├── main.cpp                # setup() + loop() 主逻辑编排
│   ├── display_manager.cpp     # 显示状态机 + 天气动画
│   ├── button_handler.cpp      # 触摸按键状态机（长短按）
│   ├── wifi_manager.cpp        # STA/AP 状态机 + NVS 凭据
│   ├── web_server.cpp          # AsyncWebServer + API 路由
│   ├── weather_client.cpp      # 心知天气 + 高德IP定位
│   └── ntp_sync.cpp            # NTP 校时（非阻塞状态机）
└── data/                       # LittleFS 网页文件
    ├── index.html              # 首页（状态面板）
    ├── wifi.html               # WiFi 配网页面
    ├── number.html             # 数字输入页面
    ├── style.css               # 白灰像素复古样式
    └── script.js               # 前端共享工具函数
```

---

## 5. Flash 分区布局（16MB）

| 分区名 | 类型 | 偏移 | 大小 | 用途 |
|--------|------|------|------|------|
| nvs | data/nvs | 0x9000 | 20KB | WiFi 凭据 + 设置 |
| otadata | data/ota | 0xE000 | 8KB | OTA 引导信息 |
| app0 | app/ota_0 | 0x10000 | 3MB | 固件槽 A |
| app1 | app/ota_1 | 0x310000 | 3MB | 固件槽 B（OTA 用） |
| spiffs | data/spiffs | 0x610000 | ~10MB | LittleFS 网页文件 |

---

## 6. 按键行为

| 按键 | 短按 | 长按(2秒) |
|------|------|-----------|
| **按键1** (T0) | 开启/关闭 AP 配网模式 | 清除 WiFi 凭据 |
| **按键2** (T2) | 获取天气（非天气模式）/ 切换动画（天气模式中） | 切换 ℃/℉ |
| **按键3** (T3) | 关屏 / 亮屏 | — |

---

## 7. 显示模式

共 4 种模式，由 `display_manager.cpp` 状态机管理：

| 模式 | 触发方式 | 自动恢复 |
|------|----------|----------|
| **TIME** | 默认 / 15秒超时后恢复 | — |
| **WEATHER** | 按键2 短按 | 15 秒后自动恢复 TIME |
| **NUMBER** | 网页输入数字 | 手动恢复（网页点"恢复时间"） |
| **OFF** | 按键3 短按 | 再按按键3 恢复 |

天气动画：获取天气后先播 3 秒动画（根据天气文字自动选择），再显示温度。天气显示中按按键2 可循环切换 6 种动画。

---

## 8. WiFi 状态机

```
DISCONNECTED ←→ CONNECTING → CONNECTED
     ↑                          ↓ (断连)
     └──────────────────────────┘
     
AP_ACTIVE ←→ AP_CONNECTED (客户端连入/断开)
     ↓ (3分钟超时)
DISCONNECTED
```

- 首次开机无凭据，不自动开启 AP
- 按键1 短按开启 AP 配网热点 `Clock-Setup`（无密码）
- AP 3 分钟无人连接自动关闭
- STA 断连后每 5 秒自动重试
- 凭据存储在 NVS（Preferences），断电不丢失

---

## 9. 网页功能（clock.local）

| 页面 | 路径 | 功能 |
|------|------|------|
| 首页 | `/` | 显示 WiFi 状态、IP、显示模式（3 秒轮询） |
| WiFi 配网 | `/wifi.html` | 扫描附近网络 + 输入密码连接 |
| 数字输入 | `/number.html` | 发送 0-9999 数字到数码管 |

### API 路由

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/api/status` | 系统状态 JSON |
| GET | `/api/wifi/scan` | 扫描附近 WiFi |
| POST | `/api/wifi/connect` | 保存凭据并连接 |
| POST | `/api/display/number` | 设置数码管显示数字 |
| POST | `/api/display/recover` | 恢复时间显示 |

---

## 10. 天气服务

**流程**：按键2 触发 → 高德 IP 定位获取城市 → 心知天气 API 获取天气 → 播放动画 → 显示温度

### 配置的 API

| 服务 | 接口 | API Key |
|------|------|---------|
| **高德 IP 定位** | `https://restapi.amap.com/v3/ip` | `YOUR_AMAP_API_KEY_HERE` |
| **心知天气** | `http://api.seniverse.com/v3/weather/now.json` | `YOUR_WEATHER_API_KEY_HERE` |

### 定位失败降级顺序
1. 高德 IP 定位（`ENABLE_AMAP_LOCATION=1`）
2. ip-api.com（国际备用）
3. `DEFAULT_CITY="Beijing"`（最终兜底）

---

## 11. NTP 校时

- 服务器：`pool.ntp.org`
- 时区：UTC+8（北京时间）
- 触发时机：WiFi 首次连上 / 每 6 小时
- 非阻塞设计：不卡主循环，超时 10 秒

---

## 12. 天气动画系统

### 动画数据

每种动画由帧表（`uint8_t[N][4]`）驱动，4 字节对应 4 位数码管，共阳极编码（0=亮，1=灭）。

### 6 种动画类型

| 类型 | 帧数 | 帧间隔 | 说明 |
|------|------|--------|------|
| ☀️ 晴 | 8 帧 | 130ms | 流光扫描（Knight Rider） |
| ☁️ 多云 | 6 帧 | 200ms | 柔和脉动 + 左右漂移 |
| 🌧️ 雨 | 8 帧 | 150ms | 雨滴级联下落 |
| ❄️ 雪 | 6 帧 | 250ms | 小数点雪花飘落 |
| ⛈️ 雷阵雨 | 8 帧 | 150ms | 雨滴 + 闪电全亮 |
| ❤️ 默认 | 6 帧 | 180ms | 爱心滚动 |

动画总时长：3000ms，播完后自动切换到温度显示。

### 天气文字→动画映射

```
含"晴" → ☀️   含"云"/"阴" → ☁️   含"雨" → 🌧️
含"雪"/"冰" → ❄️   含"雷"/"暴" → ⛈️   其他 → ❤️
```

---

## 13. 库依赖

| 库 | 版本 | 用途 |
|----|------|------|
| ESPAsyncWebServer | 2.0.0+ | 异步 Web 服务器 |
| ArduinoJson | 6.21.6 | JSON 解析（API 请求/响应） |
| ShiftRegister74HC595 | 1.3.1 | 74HC595 数码管驱动 |
| RTClib | 2.1.4 | DS3231 RTC 驱动 |
| AsyncTCP-esphome | 2.1.4 | AsyncWebServer TCP 层 |
| ArduinoOTA | 内置 | OTA 无线固件更新 |
| LittleFS | 内置 | 网页文件存储 |

---

## 14. 烧录方式

### 有线烧录（首次/OTA 失败时）

```
pio run -t upload
# 硬件：按住 IO0 → 按 EN → 松开 EN → 松开 IO0
```

波特率：921600

### OTA 无线烧录（配网后）

```
pio run -t upload --upload-port 192.168.x.x
```

设备需已连 WiFi，IP 从串口日志或路由器获取。

### 网页文件烧录

```
pio run -t uploadfs
```

---

## 15. 开发过程中的关键问题和修复

| # | 问题 | 原因 | 修复 |
|---|------|------|------|
| 1 | I2C 不工作 | SDA/SCL 引脚写反（SDA=21, SCL=22 正确配对被写成 SDA=22, SCL=21） | 改回 SDA=22, SCL=21 |
| 2 | LittleFS 分区找不到 | 分区名 "littlefs" 不匹配 ESP32 核心期望的 "spiffs" | 分区名改为 "spiffs" |
| 3 | AsyncTCP 缺失 | ESPAsyncWebServer 依赖未声明 | 添加 `esphome/AsyncTCP-esphome` |
| 4 | Web 服务器崩溃（tcpip Invalid mbox） | `server.begin()` 在 WiFi 栈就绪前调用 | 在 `wifi_init()` 中加 `WiFi.mode(WIFI_AP_STA)` |
| 5 | IP 定位服务不可用（HTTP -1） | ip.useragentinfo.com 国内不通 | 改用高德 IP 定位 API |
| 6 | 动画后数码管全黑 | `write_digits_with_animation()` 做了 `setAllHigh()` 但未渲染最终值 | 在动画末尾加 `sr.setAll(finalSegs)` |
| 7 | 按键2 无反应 | 引脚写成 T4，实际应为 T2 | 改为 T2 |
| 8 | 动画帧率低、效果差 | `display_update()` 每秒只跑 1 次，动画帧需要 130-250ms 更新 | 计划修复中 |
| 9 | 自动复位不工作 | 板子 DTR/RTS 电路不支持标准自动下载 | 增加 OTA 作为替代方案 |

---

## 16. 配置参数速查（config.h）

```cpp
// 显示
DISPLAY_REFRESH_MS    1000    // 时间刷新间隔 (ms)
DISPLAY_ANIM_DELAY    50      // 翻页动画每帧延时 (ms)
WEATHER_DISPLAY_MS    15000   // 天气显示时长 (ms)

// 触摸
TOUCH_THRESHOLD       40      // 触摸判定阈值
BTN_DEBOUNCE_MS       50      // 消抖时间 (ms)
BTN_LONG_PRESS_MS     2000    // 长按判定时间 (ms)

// WiFi
AP_SSID               "Clock-Setup"
AP_TIMEOUT_MS         180000  // AP 3 分钟超时 (ms)
WIFI_RECONNECT_MS     5000    // 断连重试间隔 (ms)

// 网络
MDNS_HOSTNAME          "clock"
NTP_SERVER             "pool.ntp.org"
NTP_INTERVAL_MS       21600000 // NTP 间隔 = 6 小时 (ms)
NTP_TIMEOUT_MS        10000    // NTP 超时 (ms)
GMT_OFFSET_SEC         (8*3600) // UTC+8
```
