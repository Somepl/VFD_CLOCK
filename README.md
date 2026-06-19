# ESP32 4-Digit 7-Segment Desk Clock

基于 PlatformIO + Arduino 的 ESP32 桌面时钟，4 位数码管显示，支持 WiFi 配网、NTP 校时、天气显示、电容触摸按键、OTA 远程升级、复古 Web 控制面板。

![IMG_20240323_162852](https://github.com/Somepl/YS18-3-for-yi/assets/56122958/55d47e16-f70d-4323-a173-1d48dbec4436)

## Features

- **4 位数码管** — 74HC595 驱动，8 段（A-H）独立控制，共阳极
- **WiFi 配网** — 热点 `Clock-Setup` 配网，3 分钟无操作自动关闭
- **NTP 校时** — `pool.ntp.org` + 备用服务器，UTC+8，6 小时间隔，零阻塞
- **天气显示** — 心知天气 API + 高德 IP 定位，6 种天气动画（晴/云/雨/雪/雷/默认）
- **电容触摸** — 3 个触摸按键（T0/T2/T3），支持短按/长按，3mm 亚克力面板
- **DS3231 RTC** — 电池备份、温度读取、NTP 校准持久化
- **OTA 升级** — 3MB OTA 分区，远程固件更新
- **Web 控制面板** — AsyncWebServer，图案/动画编辑器，文件管理，远程控制
- **MQTT 遥控** — 支持通过 MQTT 控制显示内容

## Hardware

| Component | Description |
|-----------|-------------|
| MCU | ESP32-WROOM-32UE (16MB Flash) |
| Display | 4-digit 7-segment LED (common anode) |
| Driver | SN74HC595 (3-wire SPI) |
| RTC | DS3231 (I2C: SDA=22, SCL=21) |
| Touch | 3× capacitive touch (T0/GPIO4, T2/GPIO2, T3/GPIO15) |
| Power | MT3608 boost to 20V (anode) + AMS1117 1.5V (filament) |

## Quick Start

```bash
# 安装 PlatformIO，然后：
pio run                    # 编译
pio run -t upload          # USB 烧录（首次）
pio run -t upload --upload-port <IP>   # OTA 更新固件
```

网页文件通过浏览器上传：打开 `http://<IP>/fs.html` → 选择 `data/` 目录中的文件 → 上传。

## Project Structure

```
DEMO/
├── platformio.ini          # 项目配置
├── partitions/             # 16MB 分区表
├── include/                # 头文件
│   ├── config.h            # 引脚定义 & API 密钥
│   ├── display_manager.h   # 数码管显示管理器
│   ├── pattern_manager.h   # 图案/动画/覆写管理器
│   └── web_server.h        # HTTP 服务器
├── src/                    # 源代码（7 个模块）
│   ├── main.cpp            # setup() + loop()
│   ├── display_manager.cpp # 显示驱动 & 动画引擎
│   ├── wifi_manager.cpp    # WiFi 配网 & 状态机
│   ├── ntp_sync.cpp        # NTP 校时 & RTC 读写
│   ├── weather_client.cpp  # 天气 API 客户端
│   ├── button_handler.cpp  # 触摸按键处理
│   ├── web_server.cpp      # Web API & 静态文件
│   ├── pattern_manager.cpp # LittleFS 图案/动画存储
│   └── remote_client.cpp   # MQTT 远程控制
├── data/                   # Web 界面文件（LittleFS）
│   ├── index.html          # 主页（模拟时钟 + 状态面板）
│   ├── creator.html        # 图案 & 动画编辑器
│   ├── wifi.html           # WiFi 配网页面
│   ├── remote.html         # 远程控制页面
│   └── fs.html             # 文件管理页面
└── YS18-3-for-yi-main(Old code)/   # 旧版代码参考
```

## Web API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | System status (WiFi, time, mode, brightness) |
| GET | `/api/config` | Get config (brightness, night mode) |
| POST | `/api/config` | Save config |
| GET | `/api/wifi/scan` | Scan WiFi networks |
| POST | `/api/wifi/connect` | Connect to WiFi |
| GET | `/api/animations/builtin` | List 10 built-in animations with frames |
| POST | `/api/animations/builtin` | Save override for built-in animation |
| DELETE | `/api/animations/builtin` | Remove override |
| GET | `/api/patterns` | Saved patterns |
| POST | `/api/patterns` | Save pattern |
| POST | `/api/display/number` | Show 4-digit number |
| POST | `/api/display/animation` | Play built-in animation |
| POST | `/api/display/anim-play` | Play user animation frames |
| POST | `/api/fs/upload` | Upload file to LittleFS |
| GET | `/api/fs/list` | List uploaded files |
| DELETE | `/api/fs/delete` | Delete uploaded file |

## PCB & Hardware

![SCH_Schematic1_1_2023-06-15](https://github.com/Somepl/YS18-3-for-yi/assets/56122958/2334f733-9261-45cb-b098-40d2a5ed5828)
![PCB_PCB1_1_2023-12-28](https://github.com/Somepl/YS18-3-for-yi/assets/56122958/b1e7b340-5560-496d-8cb9-174b90f94e1c)
![DSC_5194-2](https://github.com/Somepl/YS18-3-for-yi/assets/56122958/0b7fa5f1-54be-4b72-b6ce-d54f7cf38dae)

## License

MIT
