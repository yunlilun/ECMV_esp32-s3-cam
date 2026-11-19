English
Overview
ECMV is a single-file firmware project for ESP32-S3 camera boards. It integrates camera monitoring, a web settings center, Wi-Fi AP/STA, BLE provisioning, USB Host detection, SD card and LittleFS storage, ST7789 TFT local preview, physical buttons, and a simple GPIO script engine.

Features
Live JPEG camera view in a browser

Web settings center for camera, sensor, network, storage, TFT, buttons, and scripts

Wi-Fi AP hotspot and STA router connection

BLE provisioning and BLE connect/disconnect trigger scripts

USB Host device attach/detach detection and trigger scripts

Browser snapshot download and browser recording download

Device-side MJPEG recording

SD card and LittleFS file management

ST7789 240x320 TFT local preview and status page

Two configurable physical buttons

In TFT-only mode, hold both buttons for 5 seconds to restore web and Wi-Fi

Hardware and Fixed Pins
Camera pins:

Signal	GPIO
XCLK	15
SIOD	4
SIOC	5
D0-D7	11, 9, 8, 10, 12, 18, 17, 16
VSYNC	6
HREF	7
PCLK	13
TFT ST7789 pins:

Signal	GPIO
SCLK	19
MOSI	20
CS	45
DC	47
RST	21
BL	38
Other pins:

Function	GPIO
Button 1	0
Button 2	46
USB OTG D+	20
USB OTG D-	19
SD_MMC 1-bit CLK	39
SD_MMC 1-bit CMD	38
SD_MMC 1-bit D0	40
Note: TFT and USB OTG share GPIO19/20. In the default Web + TFT mode, SD and USB Host are paused. To use SD or USB Host, switch to Web-only mode and restart.

Run Modes
Web + TFT: default mode. Enables Wi-Fi, web, and TFT. SD and USB Host are paused due to pin multiplexing.

Web only: disables TFT and enables SD card and USB Host related features. Restart required.

TFT only: disables Wi-Fi and web. Local TFT preview only. Restart required. Hold both buttons for 5 seconds to restore web and Wi-Fi.

Build Dependencies
Use Arduino IDE or PlatformIO for ESP32-S3. PSRAM must be enabled.

Main dependencies:

esp32-camera

WiFi and WebServer

LittleFS and SD_MMC

NimBLE-Arduino

Adafruit GFX

TJpg_Decoder

ESP-IDF USB Host components

Usage
After flashing, the device starts a Wi-Fi hotspot. Default SSID is ECMV, default password is ecmv123456.

Connect to the hotspot and open the device web page.

Configure resolution, JPEG quality, sensor controls, TFT, storage, and network in the settings center.

Use a BLE tool to write JSON to the RX characteristic of ECMV-BLE for provisioning:
{"ssid":"MyWiFi","pass":"12345678"}

The file page supports preview, download, and delete.

The OTG and script pages configure USB Host status, USB attach/detach scripts, BLE trigger scripts, and simple GPIO scripts.

Script Format
One command per line:

GPIO=2:1
DELAY=500
GPIO=2:0
Allowed script GPIOs: 1, 2, 3, 14, 21, 41, 42, 47, 48. Camera, SD, USB, PSRAM, and UART related pins are not allowed.

Notes
Many pins are multiplexed. Restart is required after changing run modes.

USB Host mode requires 5V VBUS on the interface.

This project does not send key presses to connected phones or computers, and does not execute host commands.

Supported sensor controls vary by sensor model. Unsupported items are skipped.

SD card and TFT pin assignments depend on the board. Check your schematic before use.

