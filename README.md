# ECMV ESP32-S3 Camera Monitor

说明文档

---

## 简体中文

### 项目简介

ECMV 是一个面向 ESP32-S3 摄像头开发板的单文件固件项目，集成摄像头监控、Web 设置中心、Wi-Fi AP/STA、BLE 配网、USB Host 检测、SD 卡与 LittleFS 存储、ST7789 TFT 本地预览、实体按键操作和简易 GPIO 脚本引擎。

### 主要功能

- 浏览器实时查看摄像头 JPEG 画面
- Web 设置中心：画面参数、传感器控制、网络、存储、TFT、按键、脚本
- Wi-Fi AP 热点与 STA 路由器连接
- BLE 配网与 BLE 连接/断开触发脚本
- USB Host 设备插入/拔出检测与触发脚本
- 浏览器拍照下载、浏览器录像下载
- 设备端 MJPEG 录制
- SD 卡与 LittleFS 文件管理
- ST7789 240x320 TFT 本地预览与状态页
- 两个实体按键可配置短按动作
- 仅 TFT 模式下可长按两个按键 5 秒恢复网页与 Wi-Fi

### 硬件与固定引脚

摄像头引脚：

- XCLK: GPIO15
- SIOD: GPIO4
- SIOC: GPIO5
- D0-D7: GPIO11, 9, 8, 10, 12, 18, 17, 16
- VSYNC: GPIO6
- HREF: GPIO7
- PCLK: GPIO13

TFT ST7789 引脚：

- SCLK: GPIO19
- MOSI: GPIO20
- CS: GPIO45
- DC: GPIO47
- RST: GPIO21
- BL: GPIO38

其他引脚：

- 按键1: GPIO0
- 按键2: GPIO46
- USB OTG D+: GPIO20
- USB OTG D-: GPIO19
- SD_MMC 1-bit 预设: CLK GPIO39, CMD GPIO38, D0 GPIO40

注意：TFT 与 USB OTG 使用 GPIO19/20，存在引脚复用。默认“网页 + TFT”方案下 SD 与 USB Host 暂停。需要 SD 或 USB Host 时请切换到“仅网页”方案并重启。

### 运行方案

1. 网页 + TFT：默认方案，启用 Wi-Fi、网页和 TFT。SD 与 USB Host 因引脚复用暂停。
2. 仅网页：关闭 TFT，启用 SD 卡与 USB Host 相关功能。需要重启。
3. 仅 TFT：关闭 Wi-Fi 和网页，仅本地 TFT 预览。需要重启。同时长按两个按键 5 秒可恢复网页与 Wi-Fi。

### 构建依赖

建议使用 Arduino IDE 或 PlatformIO 构建 ESP32-S3 项目，并确保启用 PSRAM。

主要依赖库：

- esp32-camera
- WiFi 与 WebServer
- LittleFS 与 SD_MMC
- NimBLE-Arduino
- Adafruit GFX
- TJpg_Decoder
- ESP-IDF USB Host 相关组件

### 使用说明

1. 烧录固件后设备启动 Wi-Fi 热点，默认 SSID 为 `ECMV`，默认密码为 `ecmv123456`。
2. 连接热点后访问设备 Web 页面。
3. 在设置中心配置分辨率、JPEG 画质、传感器参数、TFT、存储位置和网络。
4. 可使用 BLE 工具向 ECMV-BLE 的 RX 特征写入 JSON 配网：
   `{"ssid":"MyWiFi","pass":"12345678"}`
5. 文件页可预览、下载和删除存储文件。
6. OTG 与脚本页可配置 USB Host 状态、USB 插拔脚本、BLE 触发脚本和简易 GPIO 脚本。

### 脚本格式

每行一条命令，支持：

```text
GPIO=2:1
DELAY=500
GPIO=2:0
