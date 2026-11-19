#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <NimBLEDevice.h>
#include <usb/usb_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <TJpg_Decoder.h>

// 摄像头引脚
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

// OTG 默认引脚
#define OTG_DP_PIN 20
#define OTG_DM_PIN 19
#define TFT_SCLK_GPIO 19
#define TFT_MOSI_GPIO 20
#define TFT_CS_GPIO 45
#define TFT_DC_GPIO 47
#define TFT_RST_GPIO 21
#define TFT_BL_GPIO 38
#define BUTTON1_GPIO 0
#define BUTTON2_GPIO 46

class St7789Panel : public Adafruit_GFX {
public:
    St7789Panel() : Adafruit_GFX(240, 320) {}

    void setColorCorrection(bool enabled) {
        colorCorrection = enabled;
        setRotation(panelRotation);
    }

    bool begin() {
        pinMode(TFT_CS_GPIO, OUTPUT);
        pinMode(TFT_DC_GPIO, OUTPUT);
        pinMode(TFT_RST_GPIO, OUTPUT);
        pinMode(TFT_BL_GPIO, OUTPUT);
        digitalWrite(TFT_CS_GPIO, HIGH);
        digitalWrite(TFT_DC_GPIO, HIGH);
        digitalWrite(TFT_BL_GPIO, LOW);
        displaySPI.begin(TFT_SCLK_GPIO, -1, TFT_MOSI_GPIO, TFT_CS_GPIO);
        digitalWrite(TFT_RST_GPIO, HIGH);
        delay(10);
        digitalWrite(TFT_RST_GPIO, LOW);
        delay(20);
        digitalWrite(TFT_RST_GPIO, HIGH);
        delay(120);
        writeCommand(0x01);
        delay(150);
        writeCommand(0x11);
        delay(120);
        const uint8_t pixelFormat = 0x55;
        writeCommandData(0x3A, &pixelFormat, 1);
        const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        writeCommandData(0xB2, porch, sizeof(porch));
        const uint8_t gateControl = 0x35;
        writeCommandData(0xB7, &gateControl, 1);
        const uint8_t vcom = 0x19;
        writeCommandData(0xBB, &vcom, 1);
        const uint8_t lcmControl = 0x2C;
        writeCommandData(0xC0, &lcmControl, 1);
        const uint8_t vdvEnable[] = {0x01, 0xFF};
        writeCommandData(0xC2, vdvEnable, sizeof(vdvEnable));
        const uint8_t vrh = 0x12;
        writeCommandData(0xC3, &vrh, 1);
        const uint8_t vdv = 0x20;
        writeCommandData(0xC4, &vdv, 1);
        const uint8_t frameRate = 0x0F;
        writeCommandData(0xC6, &frameRate, 1);
        const uint8_t powerControl[] = {0xA4, 0xA1};
        writeCommandData(0xD0, powerControl, sizeof(powerControl));
        const uint8_t positiveGamma[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                         0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
        writeCommandData(0xE0, positiveGamma, sizeof(positiveGamma));
        const uint8_t negativeGamma[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                         0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
        writeCommandData(0xE1, negativeGamma, sizeof(negativeGamma));
        writeCommand(0x21);
        writeCommand(0x13);
        setRotation(1);
        writeCommand(0x29);
        delay(20);
        return true;
    }

    void setRotation(uint8_t rotation) override {
        Adafruit_GFX::setRotation(rotation);
        panelRotation = rotation & 3;
        static const uint8_t madctl[] = {0x00, 0x60, 0xC0, 0xA0};
        uint8_t control = madctl[panelRotation] | (colorCorrection ? 0x00 : 0x08);
        writeCommandData(0x36, &control, 1);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= width() || y >= height()) return;
        displaySPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        digitalWrite(TFT_CS_GPIO, LOW);
        setWindow(x, y, x, y);
        displaySPI.write16(color);
        digitalWrite(TFT_CS_GPIO, HIGH);
        displaySPI.endTransaction();
    }

    void fillScreen(uint16_t color) override {
        fillRect(0, 0, width(), height(), color);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        int32_t x0 = (int32_t)x < 0 ? 0 : (int32_t)x;
        int32_t y0 = (int32_t)y < 0 ? 0 : (int32_t)y;
        int32_t x1 = (int32_t)x + w > width() ? width() : (int32_t)x + w;
        int32_t y1 = (int32_t)y + h > height() ? height() : (int32_t)y + h;
        if (x1 <= x0 || y1 <= y0) return;
        displaySPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        digitalWrite(TFT_CS_GPIO, LOW);
        setWindow(x0, y0, x1 - 1, y1 - 1);
        for (int32_t i = 0; i < (x1 - x0) * (y1 - y0); ++i) displaySPI.write16(color);
        digitalWrite(TFT_CS_GPIO, HIGH);
        displaySPI.endTransaction();
    }

    void pushRgb565(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels) {
        int32_t x0 = (int32_t)x < 0 ? 0 : (int32_t)x;
        int32_t y0 = (int32_t)y < 0 ? 0 : (int32_t)y;
        int32_t x1 = (int32_t)x + w > width() ? width() : (int32_t)x + w;
        int32_t y1 = (int32_t)y + h > height() ? height() : (int32_t)y + h;
        if (x1 <= x0 || y1 <= y0) return;
        displaySPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        digitalWrite(TFT_CS_GPIO, LOW);
        setWindow(x0, y0, x1 - 1, y1 - 1);
        static uint8_t rowBytes[640];
        size_t visibleWidth = (size_t)(x1 - x0);
        for (int32_t row = y0; row < y1; ++row) {
            size_t offset = (size_t)(row - y) * w + (x0 - x);
            for (size_t col = 0; col < visibleWidth; ++col) {
                uint16_t pixel = pixels[offset + col];
                rowBytes[col * 2] = (uint8_t)(pixel >> 8);
                rowBytes[col * 2 + 1] = (uint8_t)pixel;
            }
            displaySPI.writeBytes(rowBytes, visibleWidth * 2);
        }
        digitalWrite(TFT_CS_GPIO, HIGH);
        displaySPI.endTransaction();
    }

private:
    SPIClass displaySPI{FSPI};
    bool colorCorrection = true;
    uint8_t panelRotation = 1;

    void writeCommand(uint8_t command) {
        writeCommandData(command, nullptr, 0);
    }

    void writeCommandData(uint8_t command, const uint8_t *data, size_t length) {
        displaySPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        digitalWrite(TFT_CS_GPIO, LOW);
        digitalWrite(TFT_DC_GPIO, LOW);
        displaySPI.transfer(command);
        if (length && data) {
            digitalWrite(TFT_DC_GPIO, HIGH);
            displaySPI.writeBytes(data, length);
        }
        digitalWrite(TFT_CS_GPIO, HIGH);
        displaySPI.endTransaction();
    }

    void setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        uint8_t data[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
        digitalWrite(TFT_DC_GPIO, LOW);
        displaySPI.transfer(0x2A);
        digitalWrite(TFT_DC_GPIO, HIGH);
        displaySPI.writeBytes(data, sizeof(data));
        data[0] = (uint8_t)(y0 >> 8);
        data[1] = (uint8_t)y0;
        data[2] = (uint8_t)(y1 >> 8);
        data[3] = (uint8_t)y1;
        digitalWrite(TFT_DC_GPIO, LOW);
        displaySPI.transfer(0x2B);
        digitalWrite(TFT_DC_GPIO, HIGH);
        displaySPI.writeBytes(data, sizeof(data));
        digitalWrite(TFT_DC_GPIO, LOW);
        displaySPI.transfer(0x2C);
        digitalWrite(TFT_DC_GPIO, HIGH);
    }
};

St7789Panel tft;

WebServer server(80);
Preferences prefs;
bool prefsReady = false;
camera_config_t camConfig;
sensor_t *sensor = nullptr;

// 运行时状态
String apSSID = "ECMV";
String apPass = "ecmv123456";
String staSSID = "";
String staPass = "";
int currentRes = FRAMESIZE_SVGA;
int currentQual = 12;
int currentBri = 0;
int currentCon = 0;
int currentSat = 0;
int currentWb = 0;
struct CameraExtendedControls {
    int8_t sharpness = 0;
    uint8_t denoise = 0;
    uint8_t specialEffect = 0;
    bool whiteBalance = true;
    bool awbGain = true;
    bool exposureControl = true;
    bool aec2 = false;
    int8_t aeLevel = 0;
    uint16_t aecValue = 300;
    bool gainControl = true;
    uint8_t agcGain = 0;
    uint8_t gainCeiling = 0;
    bool hmirror = false;
    bool vflip = false;
    bool dcw = true;
    bool bpc = false;
    bool wpc = true;
    bool rawGma = true;
    bool lenc = true;
    bool colorbar = false;
};
CameraExtendedControls cameraExtendedControls;
int currentTheme = 0;
int recSegmentSec = 30;
bool recLoop = true;
bool apEnabled = true;
static const int HW_PROFILE_DISPLAY = 0;
static const int HW_PROFILE_STORAGE_USB = 1;
int hardwareProfile = HW_PROFILE_DISPLAY;
int uiMode = 0;
bool networkEnabled = true;
bool webEnabled = true;
bool serverStarted = false;
int button1Action = 1;
int button2Action = 2;
bool restartPending = false;
uint32_t restartAt = 0;
bool tftReady = false;
bool tftBacklightOn = true;
bool tftEnabled = true;
bool tftFillScreen = true;
bool tftColorCorrection = true;
uint8_t tftPage = 0;
uint8_t tftRotation = 1;
uint16_t tftFrameIntervalMs = 750;
int16_t tftImageOffsetX = 0;
int16_t tftImageOffsetY = 0;
uint32_t tftLastFrameAt = 0;
uint32_t tftLastCaptureAt = 0;
uint32_t tftLastStatusAt = 0;
uint16_t tftLastFrameWidth = 0;
uint16_t tftLastFrameHeight = 0;
uint8_t tftLastScale = 0;
bool sdMounted = false;
bool useSD = false;
int recOutput = 1;
bool otgAsUsb = true;
bool bleActive = false;
bool recording = false;
File recFile;
File browserRecFile;
File snapUploadFile;
uint32_t recFrames = 0;
uint32_t recStartMs = 0;
String recPath = "";
String browserRecPath = "";
String snapUploadPath = "";
String mediaUploadKind = "";
bool browserRecActive = false;
bool browserRecError = false;
bool mediaUploadError = false;
size_t mediaUploadExpectedSize = 0;
size_t mediaUploadBytes = 0;

// BLE 服务
NimBLEServer *bleServer = nullptr;
NimBLECharacteristic *bleCharRx = nullptr;
NimBLECharacteristic *bleCharTx = nullptr;
static bool bleConnected = false;
String bleConnectScript = "";
String bleDisconnectScript = "";
String bleMacFilter = "";
String usbConnectScript = "";
String usbDisconnectScript = "";

struct BleTriggerEvent {
    bool connected;
    char address[18];
};

QueueHandle_t bleTriggerQueue = nullptr;

struct UsbTriggerEvent {
    bool connected;
    uint8_t address;
    uint16_t vendorId;
    uint16_t productId;
    uint8_t deviceClass;
    uint8_t deviceSubclass;
    uint8_t deviceProtocol;
};

struct UsbDeviceRecord {
    bool used;
    usb_device_handle_t handle;
    UsbTriggerEvent event;
};

static const size_t USB_DEVICE_LIMIT = 8;
QueueHandle_t usbTriggerQueue = nullptr;
usb_host_client_handle_t usbHostClient = nullptr;
UsbDeviceRecord usbDevices[USB_DEVICE_LIMIT] = {};
UsbTriggerEvent lastUsbEvent = {};
uint8_t usbDeviceCount = 0;
bool usbHostReady = false;
String usbHostError = "";
portMUX_TYPE usbStatusMux = portMUX_INITIALIZER_UNLOCKED;

struct CameraButtonState {
    uint8_t pin;
    bool stablePressed;
    bool candidatePressed;
    uint32_t candidateAt;
};

CameraButtonState cameraButtons[2] = {};
uint8_t suppressButtonReleaseMask = 0;
uint32_t lastDeviceRecordAt = 0;

bool tftJpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *pixels) {
    tft.pushRgb565(x + tftImageOffsetX, y + tftImageOffsetY, w, h, pixels);
    return true;
}

bool initTftDisplay() {
    if (!tft.begin()) return false;
    tftReady = true;
    tft.setColorCorrection(tftColorCorrection);
    tft.setRotation(tftRotation);
    tft.fillScreen(0x0000);
    tft.setTextWrap(false);
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(tftJpegOutput);
    tftBacklightOn = prefs.getBool("tftBL", true);
    tftEnabled = prefs.getBool("tftOn", true);
    tftPage = prefs.getInt("tftPage", 0) ? 1 : 0;
    tftBacklightOn = tftBacklightOn && tftEnabled;
    digitalWrite(TFT_BL_GPIO, tftBacklightOn ? HIGH : LOW);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(12, 12);
    tft.print("ECMV CAMERA");
    tft.setTextSize(1);
    tft.setCursor(12, 42);
    tft.print("ST7789 240x320");
    return true;
}

void queueBleTrigger(bool connected, NimBLEConnInfo &connInfo) {
    if (!bleTriggerQueue) return;
    BleTriggerEvent event = {};
    event.connected = connected;
    std::string address = connInfo.getAddress().toString();
    snprintf(event.address, sizeof(event.address), "%s", address.c_str());
    xQueueSend(bleTriggerQueue, &event, 0);
}

uint8_t getUsbInterfaceClass(const usb_config_desc_t *config, uint8_t *subclass, uint8_t *protocol) {
    if (!config) return USB_CLASS_PER_INTERFACE;
    int offset = 0;
    const usb_standard_desc_t *current = (const usb_standard_desc_t *)config;
    const usb_standard_desc_t *next;
    while ((next = usb_parse_next_descriptor(current, config->wTotalLength, &offset)) != nullptr) {
        current = next;
        if (next->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE &&
            next->bLength >= USB_INTF_DESC_SIZE) {
            const usb_intf_desc_t *interfaceDesc = (const usb_intf_desc_t *)next;
            *subclass = interfaceDesc->bInterfaceSubClass;
            *protocol = interfaceDesc->bInterfaceProtocol;
            return interfaceDesc->bInterfaceClass;
        }
    }
    return USB_CLASS_PER_INTERFACE;
}

void usbClientEventCallback(const usb_host_client_event_msg_t *message, void *) {
    if (!message || !usbHostClient) return;
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        size_t slot = USB_DEVICE_LIMIT;
        for (size_t i = 0; i < USB_DEVICE_LIMIT; i++) {
            if (!usbDevices[i].used) { slot = i; break; }
        }
        if (slot == USB_DEVICE_LIMIT) {
            Serial.println("[usb] device limit reached");
            return;
        }

        usb_device_handle_t handle = nullptr;
        esp_err_t err = usb_host_device_open(usbHostClient, message->new_dev.address, &handle);
        if (err != ESP_OK) {
            Serial.printf("[usb] open failed: %s\n", esp_err_to_name(err));
            return;
        }
        const usb_device_desc_t *descriptor = nullptr;
        err = usb_host_get_device_descriptor(handle, &descriptor);
        if (err != ESP_OK || !descriptor) {
            Serial.printf("[usb] descriptor read failed: %s\n", esp_err_to_name(err));
            usb_host_device_close(usbHostClient, handle);
            return;
        }

        UsbTriggerEvent event = {};
        event.connected = true;
        event.address = message->new_dev.address;
        event.vendorId = descriptor->idVendor;
        event.productId = descriptor->idProduct;
        event.deviceClass = descriptor->bDeviceClass;
        event.deviceSubclass = descriptor->bDeviceSubClass;
        event.deviceProtocol = descriptor->bDeviceProtocol;
        if (event.deviceClass == USB_CLASS_PER_INTERFACE) {
            const usb_config_desc_t *config = nullptr;
            if (usb_host_get_active_config_descriptor(handle, &config) == ESP_OK && config) {
                event.deviceClass = getUsbInterfaceClass(config, &event.deviceSubclass, &event.deviceProtocol);
            }
        }

        usbDevices[slot].used = true;
        usbDevices[slot].handle = handle;
        usbDevices[slot].event = event;
        portENTER_CRITICAL(&usbStatusMux);
        lastUsbEvent = event;
        if (usbDeviceCount < USB_DEVICE_LIMIT) usbDeviceCount++;
        portEXIT_CRITICAL(&usbStatusMux);
        if (usbTriggerQueue && xQueueSend(usbTriggerQueue, &event, 0) != pdTRUE) {
            Serial.println("[usb] trigger queue full; connect script skipped");
        }
        Serial.printf("[usb] connected VID=%04X PID=%04X class=%02X\n",
                      event.vendorId, event.productId, event.deviceClass);
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        for (size_t i = 0; i < USB_DEVICE_LIMIT; i++) {
            if (!usbDevices[i].used || usbDevices[i].handle != message->dev_gone.dev_hdl) continue;
            UsbTriggerEvent event = usbDevices[i].event;
            event.connected = false;
            portENTER_CRITICAL(&usbStatusMux);
            lastUsbEvent = event;
            if (usbDeviceCount > 0) usbDeviceCount--;
            portEXIT_CRITICAL(&usbStatusMux);
            usbDevices[i].used = false;
            if (usbTriggerQueue && xQueueSend(usbTriggerQueue, &event, 0) != pdTRUE) {
                Serial.println("[usb] trigger queue full; disconnect script skipped");
            }
            esp_err_t err = usb_host_device_close(usbHostClient, message->dev_gone.dev_hdl);
            if (err != ESP_OK) Serial.printf("[usb] close failed: %s\n", esp_err_to_name(err));
            Serial.printf("[usb] disconnected VID=%04X PID=%04X\n", event.vendorId, event.productId);
            break;
        }
    }
}

void usbHostTask(void *) {
    while (usbHostReady) {
        uint32_t eventFlags = 0;
        esp_err_t libErr = usb_host_lib_handle_events(pdMS_TO_TICKS(10), &eventFlags);
        if (libErr != ESP_OK && libErr != ESP_ERR_TIMEOUT) {
            Serial.printf("[usb] library event error: %s\n", esp_err_to_name(libErr));
        }
        if (usbHostClient) {
            esp_err_t clientErr = usb_host_client_handle_events(usbHostClient, pdMS_TO_TICKS(10));
            if (clientErr != ESP_OK && clientErr != ESP_ERR_TIMEOUT) {
                Serial.printf("[usb] client event error: %s\n", esp_err_to_name(clientErr));
            }
        }
    }
    vTaskDelete(nullptr);
}

bool initUsbHost() {
    usbTriggerQueue = xQueueCreate(USB_DEVICE_LIMIT, sizeof(UsbTriggerEvent));
    if (!usbTriggerQueue) {
        usbHostError = "USB 事件队列分配失败";
        Serial.println("[usb] trigger queue allocation failed");
        return false;
    }
    usb_host_config_t hostConfig = {};
    hostConfig.skip_phy_setup = false;
    hostConfig.root_port_unpowered = false;
    hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&hostConfig);
    if (err != ESP_OK) {
        usbHostError = String("USB Host 启动失败: ") + esp_err_to_name(err);
        Serial.printf("[usb] install failed: %s\n", esp_err_to_name(err));
        return false;
    }
    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = USB_DEVICE_LIMIT;
    clientConfig.async.client_event_callback = usbClientEventCallback;
    clientConfig.async.callback_arg = nullptr;
    err = usb_host_client_register(&clientConfig, &usbHostClient);
    if (err != ESP_OK) {
        usbHostError = String("USB 客户端注册失败: ") + esp_err_to_name(err);
        Serial.printf("[usb] client registration failed: %s\n", esp_err_to_name(err));
        usb_host_uninstall();
        return false;
    }
    usbHostReady = true;
    if (xTaskCreate(usbHostTask, "usb_host", 6144, nullptr, 5, nullptr) != pdPASS) {
        usbHostReady = false;
        usb_host_client_deregister(usbHostClient);
        usbHostClient = nullptr;
        usb_host_uninstall();
        usbHostError = "USB 事件任务创建失败";
        Serial.println("[usb] event task creation failed");
        return false;
    }
    usbHostError = "";
    Serial.println("[usb] host enumeration ready");
    return true;
}

String jsonEscape(const String &s);

class BleRxCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
        String v = c->getValue().c_str();
        v.trim();
        if (v.length() < 5) return;
        int sp = v.indexOf("\"ssid\":\"");
        if (sp < 0) return;
        int s = sp + 8, e = v.indexOf('"', s);
        if (e < 0) return;
        String ssid = v.substring(s, e);
        String pass = "";
        int pp = v.indexOf("\"pass\":\"");
        if (pp >= 0) { s = pp + 8; e = v.indexOf('"', s); if (e > s) pass = v.substring(s, e); }
        staSSID = ssid; staPass = pass;
        prefs.putString("staSSID", staSSID); prefs.putString("staPass", staPass);
        WiFi.begin(staSSID.c_str(), staPass.c_str());
        if (bleCharTx) {
            String rsp = "{\"status\":\"connecting\",\"ssid\":\"" + jsonEscape(ssid) + "\"}";
            bleCharTx->setValue(rsp.c_str());
            bleCharTx->notify();
        }
    }
};

class BleConnCallback : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &connInfo) override {
        bleConnected = true;
        queueBleTrigger(true, connInfo);
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &connInfo, int) override {
        bleConnected = false;
        queueBleTrigger(false, connInfo);
    }
};

static BleRxCallback bleRxCallback;
static BleConnCallback bleConnCallback;

// 简易脚本存储
#define SCRIPT_MAX 2048
char scriptBuf[SCRIPT_MAX] = "";

// 内嵌前端
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>ECMV</title>
<style>
:root{--bg:#000;--fg:#fff;--accent:#00e5ff;--card:#111;--border:#333;}
[data-theme="tech"]{--bg:#020d02;--fg:#c8ffc8;--accent:#00ff66;--card:#061206;--border:#1a4d1a;}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,sans-serif;overflow:hidden;height:100vh;width:100vw}
#monitor{position:relative;width:100%;height:100%;background:#000;display:flex;align-items:center;justify-content:center}
#stream{width:100%;height:100%;object-fit:contain}
#hud-tl{position:absolute;top:8px;left:8px;font-size:11px;background:rgba(0,0,0,.55);padding:6px 9px;border-radius:6px;line-height:1.5;pointer-events:none;z-index:5}
#hud-tl b{color:var(--accent)}
#btn-settings{position:absolute;top:8px;right:8px;z-index:10;background:rgba(0,0,0,.6);border:1px solid var(--border);color:var(--fg);width:40px;height:40px;border-radius:50%;font-size:20px;cursor:pointer}
#hud-br{position:absolute;bottom:14px;right:12px;z-index:10;display:flex;gap:10px}
#hud-br button{width:58px;height:58px;border-radius:50%;border:2px solid var(--accent);background:rgba(0,0,0,.65);color:var(--accent);font-size:11px;font-weight:700;cursor:pointer}
#hud-br button.rec-on{background:#ff3b30;border-color:#ff3b30;color:#fff}
#save-progress{display:none;position:absolute;left:12px;bottom:14px;z-index:9;width:min(250px,55vw);padding:8px 10px;background:rgba(0,0,0,.72);border:1px solid var(--border);border-radius:8px;font-size:11px}
#save-progress.active{display:block}
#save-progress-track{height:5px;margin-top:6px;background:#333;border-radius:4px;overflow:hidden}
#save-progress-fill{display:block;width:0;height:100%;background:var(--accent);transition:width .15s}
#save-progress.indeterminate #save-progress-fill{width:35%;animation:save-progress 1.1s ease-in-out infinite alternate}
@keyframes save-progress{from{transform:translateX(0)}to{transform:translateX(185%)}}
#panel{position:fixed;inset:0;background:var(--bg);z-index:100;display:none;flex-direction:column;overflow:hidden}
#panel.open{display:flex}
#panel header{display:flex;align-items:center;padding:10px 12px;border-bottom:1px solid var(--border);background:var(--card);gap:8px}
#panel header h2{font-size:16px;flex:1;color:var(--accent)}
#panel header button{background:transparent;border:1px solid var(--border);color:var(--fg);padding:7px 12px;border-radius:6px;font-size:13px;cursor:pointer}
#tabs{display:flex;overflow-x:auto;background:var(--card);border-bottom:1px solid var(--border);flex-shrink:0}
#tabs button{flex:1 0 auto;padding:11px 12px;background:transparent;border:none;color:var(--fg);opacity:.55;font-size:12px;cursor:pointer;white-space:nowrap;border-bottom:2px solid transparent}
#tabs button.active{opacity:1;border-bottom-color:var(--accent);color:var(--accent)}
#tabbody{flex:1;overflow-y:auto;padding:12px}
.tabpage{display:none}.tabpage.active{display:block}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:12px;margin-bottom:10px}
.card h4{margin:0 0 10px;font-size:13px;color:var(--accent);font-weight:600}
label{display:block;font-size:12px;opacity:.75;margin:8px 0 4px}
input,select,textarea{width:100%;padding:9px 10px;background:#000;border:1px solid var(--border);border-radius:6px;color:var(--fg);font-size:13px;outline:none}
textarea{min-height:100px;resize:vertical;font-family:monospace;font-size:12px}
input[type=range]{padding:0;height:32px;border:none;background:transparent}
.btnrow{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.btnrow button,.btn{flex:1;padding:10px;background:var(--accent);color:#000;border:none;border-radius:6px;font-weight:700;font-size:13px;cursor:pointer}
.btn-ghost{background:transparent!important;color:var(--fg)!important;border:1px solid var(--border)!important}
.btn-danger{background:#ff3b30!important;color:#fff!important}
.row2{display:flex;gap:8px}.row2>*{flex:1}
.fileitem{display:flex;align-items:center;justify-content:space-between;padding:9px 0;border-bottom:1px solid var(--border);font-size:13px;gap:8px}
.fileitem:last-child{border-bottom:none}
.fileitem .nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fileitem button{padding:6px 10px;font-size:11px;border-radius:5px;border:1px solid var(--border);background:transparent;color:var(--fg);cursor:pointer}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:10px;background:rgba(0,229,255,.15);color:var(--accent);margin-left:6px}
#toast{position:fixed;bottom:90px;left:50%;transform:translateX(-50%);background:#222;color:#fff;padding:10px 18px;border-radius:20px;font-size:13px;z-index:999;opacity:0;transition:.25s;pointer-events:none;border:1px solid #444}
#toast.show{opacity:1}
#fileview{position:fixed;inset:0;background:#000;z-index:200;display:none;flex-direction:column}
#fileview header{display:flex;justify-content:space-between;padding:10px 12px;background:#111;border-bottom:1px solid #333}
#fileview .body{flex:1;display:flex;align-items:center;justify-content:center;overflow:auto}
#fileview img,#fileview video{max-width:100%;max-height:100%}
</style>
</head>
<body>
<div id="monitor">
<img id="stream" src="/frame">
<div id="hud-tl">
<div><b>FPS</b> <span id="h-fps">--</span> | <b>T</b> <span id="h-temp">--</span>°C</div>
<div><b>RAM</b> <span id="h-ram">--</span> | <b>WiFi</b> <span id="h-wifi">--</span></div>
<div id="h-net" style="font-size:10px;opacity:.7"></div>
</div>
<div id="save-progress"><span id="save-progress-label"></span><div id="save-progress-track"><i id="save-progress-fill"></i></div></div>
<button id="btn-settings" onclick="openPanel()">⚙</button>
<div id="hud-br">
<button onclick="snap()">SNAP</button>
<button id="recBtn" onclick="toggleRec()">REC</button>
</div>
</div>

<div id="panel">
<header><h2>ECMV 设置中心</h2><button onclick="closePanel()">关闭</button></header>
<div id="tabs">
<button class="active" onclick="switchTab(0,this)">常规</button>
<button onclick="switchTab(1,this)">网络与连接</button>
<button onclick="switchTab(2,this)">摄像头与文件</button>
<button onclick="switchTab(3,this)">OTG 与脚本</button>
<button onclick="switchTab(4,this)">关于</button>
</div>
<div id="tabbody">

<div class="tabpage active" id="tp0">
<div class="card"><h4>硬件运行方案</h4>
<label>网页 / TFT 运行模式</label>
<select id="s-hwprofile">
<option value="0">网页 + TFT（默认，Wi-Fi 相机监控）</option>
<option value="1">仅网页（关闭 TFT，启用 SD / USB 引脚方案）</option>
<option value="2">仅 TFT（关闭 Wi-Fi 与网页）</option>
</select>
<p id="hw-profile-info" style="font-size:11px;opacity:.7;margin-top:6px">网页 + TFT 为默认；仅网页和仅 TFT 两种方案需要重启。仅 TFT 下运行时同时长按两个实体按键 5 秒，可恢复网页与 Wi-Fi。</p>
<div class="btnrow"><button onclick="saveHardwareProfile()">保存运行方案</button></div>
</div>
<div class="card"><h4>本机 TFT 屏幕</h4>
<label><input type="checkbox" id="s-tft-enabled">启用 2.0 英寸 ST7789 屏幕（240×320）</label>
<label>屏幕方向</label>
<select id="s-tft-rotation"><option value="0">竖屏 0°</option><option value="1">横屏 90°</option><option value="2">竖屏 180°</option><option value="3">横屏 270°</option></select>
<label>预览画面</label>
<select id="s-tft-fill"><option value="1">全屏裁切填充（边缘会裁切）</option><option value="0">完整适配（保留黑边）</option></select>
<label>红蓝颜色校正</label>
<select id="s-tft-color"><option value="1">开启红蓝反向校正（红蓝颠倒时选此项）</option><option value="0">关闭校正（标准面板）</option></select>
<label>本机预览刷新间隔（毫秒）</label>
<select id="s-tft-interval"><option value="250">250 ms（目标 4 FPS）</option><option value="500">500 ms（目标 2 FPS）</option><option value="750">750 ms</option><option value="1000">1000 ms</option><option value="1500">1500 ms</option></select>
<div class="btnrow"><button onclick="saveTftSettings()">保存屏幕设置</button><button class="btn-ghost" onclick="toggleTftBacklight()">切换背光</button></div>
<p id="tft-color-info" style="font-size:11px;opacity:.7;margin-top:6px">红蓝反向校正通过 ST7789 RGB/BGR 模式完成，不改相机画面或存储文件。更改后即时生效并持久化。</p>
<p style="font-size:11px;opacity:.7;margin-top:6px">扩展板信号已固定接线：SCLK 19、MOSI 20、CS 45、DC 47、RST 21、背光 38。屏幕配置不能改写物理引脚。</p>
</div>
<div class="card"><h4>画面与特效</h4>
<label>分辨率</label>
<select id="s-res">
<option value="6">QVGA 320x240</option>
<option value="8">CIF 400x296</option>
<option value="9">HVGA 480x320</option>
<option value="10">VGA 640x480</option>
<option value="11">SVGA 800x600</option>
<option value="12">XGA 1024x768</option>
<option value="13">HD 1280x720</option>
<option value="14">SXGA 1280x1024</option>
<option value="15">UXGA 1600x1200</option>
<option value="19">QXGA 2048x1536 (OV3660 3MP)</option>
</select>
<label>JPEG 画质 (10-63，越小越好)</label>
<input type="range" id="s-qual" min="10" max="63" value="12">
<label>UI 主题</label>
<select id="s-theme">
<option value="0">暗黑</option>
<option value="1">科技绿</option>
</select>
<div class="btnrow"><button onclick="applyCam()">应用画面参数</button></div>
</div>
<div class="card"><h4>拍摄</h4>
<div class="btnrow"><button onclick="snap()">立即拍摄并下载</button></div>
<p style="font-size:11px;opacity:.6;margin-top:8px">拍摄下载到浏览器，不占用设备存储。</p>
</div>
<div class="card"><h4>录像</h4>
<label>分段时长 (秒)</label>
<input type="number" id="s-recseg" min="5" max="600" value="30">
<label><input type="checkbox" id="s-recloop" checked> 循环覆盖</label>
<div class="btnrow"><button onclick="applyRec()">保存录像设置</button></div>
<p style="font-size:11px;opacity:.6;margin-top:8px">网页端 REC 为浏览器 MediaRecorder 录制；设备端“保存 MJPEG”在文件页操作。</p>
</div>
</div>

<div class="tabpage" id="tp1">
<div class="card"><h4>当前网络状态</h4>
<div id="net-status" style="font-size:13px;line-height:1.8"></div>
</div>
<div class="card"><h4>热点 (AP) 设置</h4>
<p style="font-size:12px;opacity:.7;margin-bottom:8px">设备启动后始终开启热点。可配置热点名称和密码。</p>
<label>热点名称</label><input type="text" id="s-apssid" placeholder="ECMV">
<label>热点密码 (至少 8 位)</label><input type="password" id="s-appass" placeholder="留空不修改">
<div class="btnrow"><button onclick="applyAP()">保存热点设置</button></div>
</div>
<div class="card"><h4>WiFi 密码 / STA 连接</h4>
<label>路由器 SSID</label><input type="text" id="s-stassid" placeholder="要连接的路由器名称">
<label>路由器密码</label><input type="password" id="s-stapass" placeholder="留空不修改">
<div class="btnrow"><button onclick="applySTA()">连接路由器</button></div>
</div>
<div class="card"><h4>BLE 蓝牙配网</h4>
<p style="font-size:12px;opacity:.75;margin-bottom:8px">状态：<span id="ble-stat">未启动</span></p>
<div class="btnrow">
<button onclick="bleStart()">启动 BLE 广播</button>
<button class="btn-ghost" onclick="bleStop()">停止 BLE</button>
</div>
<p style="font-size:11px;opacity:.6;margin-top:8px">使用 nRF Connect 等工具连接 ECMV-BLE，向 RX 特征写入 JSON：{"ssid":"MyWiFi","pass":"12345678"}</p>
</div>
<div class="card"><h4>BLE 连接触发脚本</h4>
<label>设备 MAC 地址筛选（留空表示任意设备）</label>
<input type="text" id="s-ble-mac" placeholder="AA:BB:CC:DD:EE:FF">
<label>设备连接时</label>
<textarea id="s-ble-connect" placeholder="GPIO=2:1&#10;DELAY=500&#10;GPIO=2:0"></textarea>
<label>设备断开时</label>
<textarea id="s-ble-disconnect" placeholder="GPIO=2:0"></textarea>
<div class="btnrow"><button onclick="saveBleAutomation()">保存蓝牙触发脚本</button></div>
<p style="font-size:11px;opacity:.6;margin-top:8px">触发脚本在主循环中执行；只支持 GPIO 和 DELAY 命令。筛选使用连接设备的 BLE 地址，随机地址可能无法长期匹配。</p>
</div>
</div>

<div class="tabpage" id="tp2">
<div class="card"><h4>摄像头传感器</h4>
<label>亮度 (-2 ~ 2)</label><input type="range" id="s-bri" min="-2" max="2" value="0">
<label>对比度 (-2 ~ 2)</label><input type="range" id="s-con" min="-2" max="2" value="0">
<label>饱和度 (-2 ~ 2)</label><input type="range" id="s-sat" min="-2" max="2" value="0">
<label>白平衡</label>
<select id="s-wb">
<option value="0">自动</option><option value="1">晴天</option><option value="2">阴天</option>
<option value="3">办公室</option><option value="4">室内</option>
</select>
<div class="btnrow"><button onclick="applySensor()">应用传感器参数</button></div>
<details style="margin-top:10px"><summary style="font-size:12px;cursor:pointer;color:var(--accent)">专业曝光、色彩与图像控制</summary>
<label>锐度 (-2 ~ 2)</label><input type="range" id="s-sharp" min="-2" max="2" value="0">
<label>降噪等级 (0 ~ 8)</label><input type="range" id="s-denoise" min="0" max="8" value="0">
<label>图像特效</label><select id="s-effect"><option value="0">无</option><option value="1">负片</option><option value="2">灰度</option><option value="3">红色偏色</option><option value="4">绿色偏色</option><option value="5">蓝色偏色</option><option value="6">复古</option></select>
<label>自动白平衡</label><select id="s-awb"><option value="1">开启</option><option value="0">关闭</option></select>
<label>自动白平衡增益</label><select id="s-awbgain"><option value="1">开启</option><option value="0">关闭</option></select>
<label>自动曝光</label><select id="s-aec"><option value="1">开启</option><option value="0">手动</option></select>
<label>AE 微调 (-2 ~ 2)</label><input type="range" id="s-aelevel" min="-2" max="2" value="0">
<label>曝光值 (0 ~ 1200；自动曝光关闭时生效)</label><input type="range" id="s-aecvalue" min="0" max="1200" step="10" value="300">
<label>自动增益</label><select id="s-agc"><option value="1">开启</option><option value="0">手动</option></select>
<label>手动增益 (0 ~ 30)</label><input type="range" id="s-agcgain" min="0" max="30" value="0">
<label>增益上限</label><select id="s-gainceiling"><option value="0">2×</option><option value="1">4×</option><option value="2">8×</option><option value="3">16×</option><option value="4">32×</option><option value="5">64×</option><option value="6">128×</option></select>
<label>图像方向</label><div class="row2"><select id="s-hmirror"><option value="0">水平镜像：关</option><option value="1">水平镜像：开</option></select><select id="s-vflip"><option value="0">垂直翻转：关</option><option value="1">垂直翻转：开</option></select></div>
<label>镜头与画质校正</label><div class="row2"><select id="s-dcw"><option value="1">DCW 开</option><option value="0">DCW 关</option></select><select id="s-bpc"><option value="0">BPC 关</option><option value="1">BPC 开</option></select></div>
<div class="row2"><select id="s-wpc"><option value="1">WPC 开</option><option value="0">WPC 关</option></select><select id="s-lenc"><option value="1">镜头校正开</option><option value="0">镜头校正关</option></select></div>
<div class="row2"><select id="s-rawgma"><option value="1">Gamma 开</option><option value="0">Gamma 关</option></select><select id="s-aec2"><option value="0">AEC2 关</option><option value="1">AEC2 开</option></select></div>
<label>测试色条</label><select id="s-colorbar"><option value="0">关闭</option><option value="1">开启</option></select>
<div class="btnrow"><button onclick="applySensor()">应用并保存全部传感器控制</button></div>
<p id="sensor-capability" style="font-size:11px;opacity:.65;margin-top:8px">部分功能取决于传感器型号/驱动；不支持的项目会被跳过。</p>
</details>
</div>
<div class="card"><h4>机身实体按键</h4>
<p style="font-size:11px;opacity:.7">按键1：GPIO0（BOOT）；按键2：GPIO46。两键在所有运行方案下均可用。仅 TFT 模式下同时长按两键 5 秒，可直接恢复网页 + Wi-Fi。空闲应为 HIGH，按下应为 LOW。</p>
<p id="button-status" style="font-size:11px;opacity:.7">读取按键电平中...</p>
<label>按键 1（短按）</label>
<select id="s-btn1-action">
<option value="0">不执行</option><option value="1">拍照并保存到当前存储</option>
<option value="2">开始 / 停止设备端 MJPEG 录像</option>
<option value="3">切换 TFT 页面</option><option value="4">打开 / 关闭 TFT 背光</option>
</select>
<label>按键 2（短按）</label>
<select id="s-btn2-action">
<option value="0">不执行</option><option value="1">拍照并保存到当前存储</option>
<option value="2">开始 / 停止设备端 MJPEG 录像</option>
<option value="3">切换 TFT 页面</option><option value="4">打开 / 关闭 TFT 背光</option>
</select>
<div class="btnrow"><button onclick="saveButtonActions()">保存按键设置</button></div>
</div>
<div class="card"><h4>存储文件</h4>
<label>REC / SNAP 保存位置</label>
<select id="s-storage">
<option value="1">网页下载</option>
<option value="2">只保存到 SD 卡</option>
<option value="3">SD 卡 + 网页下载</option>
</select>
<p id="sd-info" style="font-size:11px;opacity:.65;margin-top:6px">GOOUUU v1.5 预设：CLK 39 / CMD 38 / D0 40（一位模式）</p>
<p style="font-size:11px;opacity:.65;margin-top:6px">网页录像会优先使用浏览器支持的 MP4；不支持时自动保存为 WebM。仅网页下载模式不会占用 SD 卡。</p>
<div class="btnrow"><button onclick="saveStorage()">保存 REC / SNAP 位置</button></div>
<p style="font-size:11px;opacity:.65;margin-top:6px">SD 卡选项也会将设备端 MJPEG 文件保存到 SD；“网页下载”模式下设备端 MJPEG 保存在内置 Flash。</p>
<div id="filelist" style="max-height:300px;overflow-y:auto;margin-bottom:10px">加载中...</div>
<div class="btnrow">
<button class="btn-ghost" onclick="loadFiles()">刷新</button>
<button class="btn-danger" onclick="deleteAll()">删除全部</button>
</div>
</div>
<div class="card"><h4>设备端 MJPEG 录制</h4>
<p style="font-size:12px;opacity:.75;margin-bottom:8px">将摄像头 JPEG 帧序列保存为 .mjpeg 文件，可在文件页下载。</p>
<div class="btnrow">
<button id="devRecBtn" onclick="toggleDevRec()">开始保存 MJPEG</button>
</div>
<p id="devRecStat" style="font-size:11px;margin-top:8px;opacity:.7"></p>
</div>
</div>

<div class="tabpage" id="tp3">
<div class="card"><h4>USB OTG 引脚</h4>
<label>GPIO19 (D-) / GPIO20 (D+) 用途</label>
<select id="s-otg">
<option value="1">USB Host 设备检测</option>
<option value="0">普通 GPIO (保留)</option>
</select>
<div class="btnrow"><button onclick="applyOTG()">保存 OTG 设置</button></div>
<p style="font-size:11px;opacity:.6;margin-top:8px">Host 模式读取 USB 描述符并运行板上 GPIO 脚本；需要该接口确有 5V VBUS。不会向连接的手机/电脑发送按键或执行主机命令。</p>
</div>
<div class="card"><h4>USB Host 状态</h4><div id="usb-status" style="font-size:12px;line-height:1.8">读取中...</div></div>
<div class="card"><h4>USB 插入/拔出脚本</h4>
<label>检测到 USB 设备时（板上脚本）</label>
<textarea id="s-usb-connect" placeholder="GPIO=2:1&#10;DELAY=500&#10;GPIO=2:0"></textarea>
<label>USB 设备拔出时</label>
<textarea id="s-usb-disconnect" placeholder="GPIO=2:0"></textarea>
<div class="btnrow"><button onclick="saveUsbAutomation()">保存 USB 触发脚本</button></div>
<p style="font-size:11px;opacity:.6;margin-top:8px">只支持 GPIO=引脚:0/1 和 DELAY=毫秒，不会操作 USB 设备本身。</p>
</div>
<div class="card"><h4>简易脚本引擎</h4>
<label>脚本内容（每行：GPIO=引脚:电平 或 DELAY=毫秒）</label>
<textarea id="s-script" placeholder="GPIO=2:1&#10;DELAY=500&#10;GPIO=2:0"></textarea>
<div class="btnrow">
<button onclick="saveScript()">保存脚本</button>
<button class="btn-ghost" onclick="runScript()">执行脚本</button>
</div>
<p style="font-size:11px;opacity:.6;margin-top:8px">脚本允许 GPIO：1,2,3,14,21,41,42,47,48；摄像头、SD、USB、PSRAM 与串口引脚不可用。</p>
</div>
<div class="card"><h4>系统信息</h4>
<div id="sys-info" style="font-size:12px;line-height:1.9"></div>
</div>
</div>

<div class="tabpage" id="tp4">
<div class="card"><h4>ECMV</h4>
<p style="font-size:13px;line-height:1.8">ESP32 Camera Monitor & Video</p>
<p style="font-size:12px;opacity:.7;line-height:1.8;margin-top:8px">单文件固件，集成摄像头监控、Web 文件管理、WiFi AP+STA、BLE 配网、OTG 配置与 MJPEG 保存。</p>
</div>
<div class="card"><h4>版本与芯片</h4>
<div id="about-info" style="font-size:12px;line-height:1.9"></div>
<p style="font-size:12px;margin-top:8px"><a href="https://github.com/yunlilun/ECMV_esp32-s3-cam" target="_blank" rel="noopener">GitHub 源码仓库</a></p>
</div>
</div>

</div>
</div>

<div id="toast"></div>
<div id="fileview">
<header><span id="fv-name"></span><button onclick="closeFileView()">关闭</button></header>
<div class="body" id="fv-body"></div>
</div>

<script>
var $=function(id){return document.getElementById(id)};
var stream=$('stream');
var canvas=document.createElement('canvas');
var ctx=canvas.getContext('2d');
var isRec=false,recStarting=false,recorder=null,chunks=[],drawTimer=null;
var recStartedAt=0;
var recOutput=1,storageLoaded=false,sdMountedAvailable=false,saveProgressTimer=null;
var hardwareProfile=0;
var devRecTimer=null;
var frameTimer=null;
var frameDelay=100;

var scheduleFrame=function(delay){
 clearTimeout(frameTimer);
 frameTimer=setTimeout(function(){
  if(stream.isConnected)stream.src='/frame?t='+Date.now();
 },document.hidden?1000:delay);
}
stream.onload=function(){scheduleFrame(frameDelay)};
stream.onerror=function(){scheduleFrame(500)};

var toast=function(m){var t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},1800)}
var api=function(p){return fetch(p).then(function(r){return r.json()})}
var apiPost=function(p,b){return fetch(p,{method:'POST',body:JSON.stringify(b),headers:{'Content-Type':'application/json'}}).then(function(r){return r.json().then(function(d){if(!r.ok&&!d.msg)d.msg='HTTP '+r.status;return d})})}

var openPanel=function(){$('panel').classList.add('open');loadAll()}
var closePanel=function(){$('panel').classList.remove('open')}
var switchTab=function(i,el){
 var ps=document.querySelectorAll('.tabpage');for(var k=0;k<ps.length;k++)ps[k].classList.remove('active');
 var bs=$('tabs').children;for(var k=0;k<bs.length;k++)bs[k].classList.remove('active');
 $('tp'+i).classList.add('active');el.classList.add('active');
 if(i===1){loadNetStatus();loadBleAutomation()};if(i===2){loadFiles();loadSensor();loadStorage()};if(i===3)loadOTG();if(i===4)loadAbout();
}

var loadAll=function(){
 api('/api/status').then(function(d){
  $('s-res').value=d.res;$('s-qual').value=d.qual;$('s-theme').value=d.theme;
  $('s-bri').value=d.bri;$('s-con').value=d.con;$('s-sat').value=d.sat;$('s-wb').value=d.wb;
  $('s-recseg').value=d.recSeg;$('s-recloop').checked=d.recLoop;
  $('s-apssid').value=d.apSSID;
  $('s-otg').value=d.otgAsUsb?1:0;
  document.documentElement.setAttribute('data-theme',d.theme===1?'tech':'');
  loadNetStatus();loadScript();loadBoardConfig();
 });
}
var loadBoardConfig=function(){
 api('/api/board-config').then(function(d){
  hardwareProfile=Number(d.profile)||0;
  uiMode=Number(d.mode)||0;
  $('s-hwprofile').value=String(d.mode);
  $('s-btn1-action').value=String(d.button1Action);
  $('s-btn2-action').value=String(d.button2Action);
  $('s-tft-enabled').checked=!!d.tftEnabled;
  $('s-tft-rotation').value=String(d.tftRotation);
  $('s-tft-fill').value=d.tftFillScreen?'1':'0';
  $('s-tft-color').value=d.tftColorCorrection?'1':'0';
  $('s-tft-interval').value=String(d.tftInterval);
  $('button-status').textContent='GPIO0：'+(d.button1Level?'HIGH':'LOW')+'；GPIO46：'+(d.button2Level?'HIGH':'LOW');
  $('hw-profile-info').textContent=!d.nvsReady
   ?'NVS 未初始化，设置无法持久化；请查看启动串口日志。'
   :d.mode===0
   ?'当前：网页 + TFT。为避免 GPIO 复用，SD 与 USB Host 暂停。'
   :d.mode===1
    ?'当前：仅网页。TFT 关闭，SD / USB Host 可用（需接入对应硬件）。'
    :'当前：仅 TFT。Wi-Fi 与网页已关闭；同时长按两个实体按键 5 秒可直接恢复网页 + Wi-Fi。';
 }).catch(function(){toast('读取硬件方案失败')});
}
var saveHardwareProfile=function(){
 apiPost('/api/board-config',{mode:+$('s-hwprofile').value})
 .then(function(d){
  if(!d.ok){toast(d.msg||'保存硬件方案失败');return}
  if(d.restart){
   $('hw-profile-info').textContent='方案已持久化，设备即将重启；Wi-Fi 网页和相机服务会在启动后恢复。';
   toast('方案已保存，设备正在重启');
  }else{
   hardwareProfile=+$('s-hwprofile').value;
   $('hw-profile-info').textContent=d.msg||'方案已保存';
   toast(d.msg||'方案已保存');
  }
 }).catch(function(){toast('保存硬件方案请求失败')});
}
var saveButtonActions=function(){
 apiPost('/api/button-config',{button1Action:+$('s-btn1-action').value,button2Action:+$('s-btn2-action').value})
 .then(function(d){if(!d.ok){toast(d.msg||'保存按键设置失败');return}toast(d.msg||'按键设置已保存')})
 .catch(function(){toast('保存按键设置请求失败')});
}
var saveTftSettings=function(){
 apiPost('/api/tft-config',{enabled:$('s-tft-enabled').checked,rotation:+$('s-tft-rotation').value,fill:+$('s-tft-fill').value,colorCorrection:+$('s-tft-color').value,interval:+$('s-tft-interval').value})
 .then(function(d){
  if(!d.ok){toast(d.msg||'屏幕设置失败');return}
  $('s-tft-enabled').checked=!!d.enabled;
  toast(d.msg||'屏幕设置已保存');
 }).catch(function(){toast('屏幕设置请求失败')});
}
var toggleTftBacklight=function(){
 apiPost('/api/tft-backlight',{on:1}).then(function(d){
  if(!d.ok){toast(d.msg||'背光切换失败');return}
  toast(d.msg||'背光已切换');
 }).catch(function(){toast('背光控制请求失败')});
}

var applyCam=function(){
 apiPost('/api/cam',{res:+$('s-res').value,qual:+$('s-qual').value,theme:+$('s-theme').value,bri:+$('s-bri').value,con:+$('s-con').value,sat:+$('s-sat').value,wb:+$('s-wb').value})
 .then(function(d){toast(d.msg||'已应用');document.documentElement.setAttribute('data-theme',+$('s-theme').value===1?'tech':'');stream.src='/frame?t='+Date.now()})
}
var applyRec=function(){apiPost('/api/rec',{seg:+$('s-recseg').value,loop:$('s-recloop').checked}).then(function(){toast('录像设置已保存')})}
var applyAP=function(){
 var pass=$('s-appass').value;
 if(pass.length>0&&pass.length<8){toast('热点密码至少 8 位');return}
 apiPost('/api/ap',{enabled:1,ssid:$('s-apssid').value,pass:pass}).then(function(d){toast(d.msg||'已保存，热点变更需重启');$('s-appass').value=''})
}
var applySTA=function(){
 var ssid=$('s-stassid').value.trim();
 if(!ssid){toast('请输入路由器 SSID');return}
 apiPost('/api/sta',{ssid:ssid,pass:$('s-stapass').value}).then(function(d){toast(d.msg||'连接指令已发送');$('s-stapass').value='';setTimeout(loadNetStatus,3000)})
}
var applySensor=function(){
 var data={
  bri:+$('s-bri').value,con:+$('s-con').value,sat:+$('s-sat').value,wb:+$('s-wb').value,
  sharp:+$('s-sharp').value,denoise:+$('s-denoise').value,effect:+$('s-effect').value,
  awb:+$('s-awb').value,awbGain:+$('s-awbgain').value,aec:+$('s-aec').value,
  aec2:+$('s-aec2').value,aeLevel:+$('s-aelevel').value,aecValue:+$('s-aecvalue').value,
  agc:+$('s-agc').value,agcGain:+$('s-agcgain').value,gainCeiling:+$('s-gainceiling').value,
  hmirror:+$('s-hmirror').value,vflip:+$('s-vflip').value,dcw:+$('s-dcw').value,
  bpc:+$('s-bpc').value,wpc:+$('s-wpc').value,rawGma:+$('s-rawgma').value,
  lenc:+$('s-lenc').value,colorbar:+$('s-colorbar').value
 };
 apiPost('/api/sensor',data).then(function(d){
  if(!d.ok){toast(d.msg||'传感器参数应用失败');return}
  $('sensor-capability').textContent=d.unsupported&&d.unsupported.length
   ?'当前传感器/驱动不支持：'+d.unsupported.join('、')
   :'传感器驱动支持以上设置。参数已保存到设备。';
  toast(d.msg||'传感器参数已应用');stream.src='/frame?t='+Date.now();
 }).catch(function(){toast('传感器设置请求失败')});
}
var applyOTG=function(){apiPost('/api/otg',{usb:+$('s-otg').value}).then(function(){toast('OTG 设置已保存，重启后生效')})}

var loadNetStatus=function(){
 api('/api/net').then(function(d){
  var h='<div><b>AP</b> '+(d.apOn?'已开启':'已关闭')+' '+(d.apSSID||'')+'</div>';
  h+='<div><b>STA</b> '+(d.staConnected?'已连接 '+d.staSSID+' | IP '+d.staIP+' | 信号 '+d.rssi+' dBm':'未连接')+'</div>';
  h+='<div><b>AP IP</b> '+d.apIP+'</div>';
  $('net-status').innerHTML=h;$('ble-stat').textContent=d.ble?'BLE 广播中':'未启动';
 });
}
var bleStart=function(){apiPost('/api/ble',{on:1}).then(function(d){toast(d.msg);loadNetStatus()})}
var bleStop=function(){apiPost('/api/ble',{on:0}).then(function(d){toast(d.msg);loadNetStatus()})}
var loadBleAutomation=function(){
 api('/api/ble-script').then(function(d){
  $('s-ble-mac').value=d.mac||'';
  $('s-ble-connect').value=d.connect||'';
  $('s-ble-disconnect').value=d.disconnect||'';
 });
}
var saveBleAutomation=function(){
 apiPost('/api/ble-script',{mac:$('s-ble-mac').value.trim(),connect:$('s-ble-connect').value,disconnect:$('s-ble-disconnect').value})
 .then(function(d){toast(d.msg||'蓝牙触发脚本已保存')});
}

var loadFiles=function(){
 api('/api/files').then(function(d){
  var el=$('filelist');
  if(!d.files||!d.files.length){el.innerHTML='<p style="opacity:.5;font-size:12px">暂无文件</p>';return}
  var h='';d.files.forEach(function(f){
   h+='<div class="fileitem"><span class="nm">'+f.name+' <span style="opacity:.5">'+(f.size/1024).toFixed(1)+'KB</span></span>';
   h+='<button onclick="viewFile(\''+f.name+'\')">预览</button>';
   h+='<button onclick="downloadFile(\''+f.name+'\')">下载</button>';
   h+='<button onclick="deleteFile(\''+f.name+'\')">删除</button></div>';
  });
  el.innerHTML=h;
 });
}
var loadStorage=function(){
 api('/api/storage').then(function(d){
  recOutput=Number(d.recOutput)||1;storageLoaded=true;sdMountedAvailable=!!d.sdMounted;
  if(d.profile!==undefined)hardwareProfile=Number(d.profile)||0;
  $('s-storage').value=recOutput;
  $('s-storage').options[1].disabled=!sdMountedAvailable;
  $('s-storage').options[2].disabled=!sdMountedAvailable;
  $('sd-info').textContent=d.sdMounted
   ?'板载 microSD 已挂载；GOOUUU v1.5 预设：CLK 39 / CMD 38 / D0 40（一位模式）'
   :hardwareProfile===0?'屏幕方案占用复用引脚，SD 已停用；REC 文件可保存到 LittleFS。':'未检测到 SD 卡；SD 保存选项不可用。';
 }).catch(function(){toast('读取存储设置失败')});
}
var saveStorage=function(){
 apiPost('/api/storage',{recOutput:+$('s-storage').value}).then(function(d){
  if(!d.ok){toast(d.msg||'保存位置失败');return}
  recOutput=+$('s-storage').value;storageLoaded=true;
  toast(d.msg||'REC / SNAP 保存位置已保存');loadFiles();
 }).catch(function(){toast('保存位置请求失败')});
}
var viewFile=function(n){$('fv-name').textContent=n;$('fileview').style.display='flex';var b=$('fv-body');
 if(n.toLowerCase().endsWith('.jpg')||n.toLowerCase().endsWith('.jpeg'))b.innerHTML='<img src="/download?f='+encodeURIComponent(n)+'">';
 else if(n.toLowerCase().endsWith('.mjpeg'))b.innerHTML='<p style="font-size:13px;line-height:1.6">设备端 MJPEG 文件是连续 JPEG 帧序列，不是 WebM/MP4 容器，浏览器不能直接播放。可下载后用支持 MJPEG 的播放器处理。</p><a style="color:#0ef" href="/download?f='+encodeURIComponent(n)+'">下载 '+n+'</a>';
 else if(/\.(webm|mp4|avi)$/i.test(n))b.innerHTML='<video controls playsinline src="/media?f='+encodeURIComponent(n)+'"></video>';
 else b.innerHTML='<a style="color:#0ef" href="/download?f='+encodeURIComponent(n)+'">下载 '+n+'</a>';
}
var closeFileView=function(){$('fileview').style.display='none';$('fv-body').innerHTML=''}
var downloadFile=function(n){location.href='/download?f='+encodeURIComponent(n)}
var deleteFile=function(n){if(!confirm('删除 '+n+'?'))return;apiPost('/api/delete',{name:n}).then(function(){loadFiles()})}
var deleteAll=function(){if(!confirm('删除全部文件？'))return;apiPost('/api/deleteall',{}).then(function(){loadFiles()})}

var readEbmlVint=function(view,offset,isSize){
 if(offset>=view.byteLength)return null;
 var first=view.getUint8(offset),mask=0x80,length=1;
 while(length<=8&&(first&mask)===0){mask>>=1;length++}
 if(length>8||offset+length>view.byteLength)return null;
 var value=isSize?(first&(mask-1)):first;
 for(var i=1;i<length;i++)value=value*256+view.getUint8(offset+i);
 var unknown=false;
 if(isSize){
  unknown=(first&(mask-1))===mask-1;
  for(var i=1;i<length&&unknown;i++)if(view.getUint8(offset+i)!==255)unknown=false;
 }
 return {value:value,length:length,unknown:unknown};
}
var readEbmlElement=function(view,offset,limit){
 var id=readEbmlVint(view,offset,false);
 if(!id)return null;
 var size=readEbmlVint(view,offset+id.length,true);
 if(!size)return null;
 var dataStart=offset+id.length+size.length;
 var dataEnd=size.unknown?limit:dataStart+size.value;
 if(dataEnd>limit||dataEnd<dataStart)return null;
 return {id:id.value,dataStart:dataStart,dataEnd:dataEnd,next:dataEnd};
}
var patchWebmDuration=function(blob,durationMs){
 if(!blob.arrayBuffer)return Promise.resolve({blob:blob,patched:false});
 return blob.arrayBuffer().then(function(buffer){
  var view=new DataView(buffer),segment=null,offset=0;
  while(offset<view.byteLength){
   var element=readEbmlElement(view,offset,view.byteLength);
   if(!element)break;
   if(element.id===0x18538067){segment=element;break}
   if(element.next<=offset)break;
   offset=element.next;
  }
  if(!segment)return {blob:blob,patched:false};
  var info=null;
  offset=segment.dataStart;
  while(offset<segment.dataEnd){
   var child=readEbmlElement(view,offset,segment.dataEnd);
   if(!child)break;
   if(child.id===0x1549A966){info=child;break}
   if(child.next<=offset)break;
   offset=child.next;
  }
  if(!info)return {blob:blob,patched:false};
  var duration=null,timecodeScale=1000000;
  offset=info.dataStart;
  while(offset<info.dataEnd){
   var child=readEbmlElement(view,offset,info.dataEnd);
   if(!child)break;
   if(child.id===0x2AD7B1){
    timecodeScale=0;
    for(var i=child.dataStart;i<child.dataEnd;i++)timecodeScale=timecodeScale*256+view.getUint8(i);
   }else if(child.id===0x4489)duration=child;
   if(child.next<=offset)break;
   offset=child.next;
  }
  if(!duration||!timecodeScale)return {blob:blob,patched:false};
  var timecodeDuration=Math.max(1,durationMs)*1000000/timecodeScale;
  if(duration.dataEnd-duration.dataStart===4)view.setFloat32(duration.dataStart,timecodeDuration,false);
  else if(duration.dataEnd-duration.dataStart===8)view.setFloat64(duration.dataStart,timecodeDuration,false);
  else return {blob:blob,patched:false};
  return {blob:new Blob([buffer],{type:blob.type}),patched:true};
 });
}

var toggleDevRec=function(){
 apiPost('/api/devrec',{}).then(function(d){
  toast(d.msg);$('devRecStat').textContent=d.rec?'设备正在保存 MJPEG...':'已停止';
  if(d.rec){$('devRecBtn').textContent='停止保存 MJPEG'}else{$('devRecBtn').textContent='开始保存 MJPEG';loadFiles()}
 });
}

var loadScript=function(){api('/api/script').then(function(d){$('s-script').value=d.script||''})}
var saveScript=function(){apiPost('/api/script/save',{script:$('s-script').value}).then(function(){toast('脚本已保存')})}
var runScript=function(){apiPost('/api/script/run',{}).then(function(d){toast(d.msg||'脚本已执行')})}

var loadOTG=function(){api('/api/otg/get').then(function(d){$('s-otg').value=d.usb?1:0});loadUsbStatus();loadUsbAutomation()}
var loadUsbStatus=function(){
 api('/api/usb-status').then(function(d){
  var h='<div><b>Host</b> '+(d.ready?'已启动':'未启动/当前模式关闭')+'</div>';
  if(d.error)h+='<div><b>错误</b> '+d.error+'</div>';
  h+='<div><b>已连接设备数</b> '+d.count+'</div>';
  if(d.last.address){
   h+='<div><b>最近事件</b> '+(d.last.connected?'插入':'拔出')+' (地址 '+d.last.address+')</div>';
   h+='<div><b>VID:PID</b> '+d.last.vid+':'+d.last.pid+'</div>';
   h+='<div><b>Class/Subclass/Protocol</b> '+d.last.class+'/'+d.last.subclass+'/'+d.last.protocol+'</div>';
  }else h+='<div>尚未检测到设备</div>';
  $('usb-status').innerHTML=h;
 });
}
var loadUsbAutomation=function(){
 api('/api/usb-script').then(function(d){
  $('s-usb-connect').value=d.connect||'';
  $('s-usb-disconnect').value=d.disconnect||'';
 });
}
var saveUsbAutomation=function(){
 apiPost('/api/usb-script',{connect:$('s-usb-connect').value,disconnect:$('s-usb-disconnect').value})
 .then(function(d){toast(d.msg||'USB 触发脚本已保存')});
}
var loadSensor=function(){
 api('/api/sensor/get').then(function(d){
  $('s-bri').value=d.bri;$('s-con').value=d.con;$('s-sat').value=d.sat;$('s-wb').value=d.wb;
  $('s-sharp').value=d.sharp;$('s-denoise').value=d.denoise;$('s-effect').value=d.effect;
  $('s-awb').value=d.awb?1:0;$('s-awbgain').value=d.awbGain?1:0;
  $('s-aec').value=d.aec?1:0;$('s-aec2').value=d.aec2?1:0;
  $('s-aelevel').value=d.aeLevel;$('s-aecvalue').value=d.aecValue;
  $('s-agc').value=d.agc?1:0;$('s-agcgain').value=d.agcGain;$('s-gainceiling').value=d.gainCeiling;
  $('s-hmirror').value=d.hmirror?1:0;$('s-vflip').value=d.vflip?1:0;
  $('s-dcw').value=d.dcw?1:0;$('s-bpc').value=d.bpc?1:0;$('s-wpc').value=d.wpc?1:0;
  $('s-rawgma').value=d.rawGma?1:0;$('s-lenc').value=d.lenc?1:0;$('s-colorbar').value=d.colorbar?1:0;
  $('sensor-capability').textContent=d.unsupported&&d.unsupported.length
   ?'当前传感器/驱动不支持：'+d.unsupported.join('、')
   :'当前传感器驱动支持以上设置。';
 }).catch(function(){toast('读取传感器设置失败')});
}
var loadAbout=function(){api('/api/about').then(function(d){
 $('about-info').innerHTML='固件 ECMV 1.0<br>芯片 '+d.chip+'<br>CPU '+d.cpu+' MHz<br>Flash '+d.flash+'<br>PSRAM '+(d.psram||'无')+'<br>空闲堆 '+d.heap+' bytes<br>运行 '+d.uptime+' 秒'
})}

var showSaveProgress=function(label,percent,indeterminate){
 var box=$('save-progress');box.classList.add('active');
 box.classList.toggle('indeterminate',!!indeterminate);
 $('save-progress-label').textContent=label;
 if(!indeterminate)$('save-progress-fill').style.width=Math.max(0,Math.min(100,percent||0))+'%';
}
var hideSaveProgress=function(){
 $('save-progress').classList.remove('active','indeterminate');
 $('save-progress-fill').style.width='0';
 if(saveProgressTimer){clearTimeout(saveProgressTimer);saveProgressTimer=null}
}
var finishSaveProgress=function(label){
 showSaveProgress(label,100,false);
 if(saveProgressTimer)clearTimeout(saveProgressTimer);
 saveProgressTimer=setTimeout(hideSaveProgress,2500);
}
var downloadBlob=function(blob,name){
 var url=URL.createObjectURL(blob),a=document.createElement('a');
 a.href=url;a.download=name;a.click();
 setTimeout(function(){URL.revokeObjectURL(url)},60000);
}
var uploadBlobToSd=function(blob,kind,onProgress){
 return new Promise(function(resolve,reject){
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/api/media-upload?kind='+encodeURIComponent(kind)+'&size='+blob.size,true);
  xhr.upload.onprogress=function(e){if(e.lengthComputable&&onProgress)onProgress(e.loaded/e.total)};
  xhr.onload=function(){
   var result;
   try{result=JSON.parse(xhr.responseText)}catch(e){reject(new Error('SD 返回了无法识别的响应'));return}
   if(xhr.status<200||xhr.status>=300||!result.ok){reject(new Error(result.msg||'SD 保存失败'));return}
   resolve(result);
  };
  xhr.onerror=function(){reject(new Error('SD 上传连接失败'))};
  var form=new FormData();
  form.append('file',blob,kind==='snap'?'snapshot.jpg':'recording');
  xhr.send(form);
 });
}
var outputNeedsSd=function(){return recOutput===2||recOutput===3}
var outputNeedsDownload=function(){return recOutput===1||recOutput===3}
var getRecorderMime=function(){
 if(typeof MediaRecorder==='undefined')return null;
 var types=['video/mp4;codecs=avc1.42E01E','video/mp4','video/webm;codecs=vp9','video/webm;codecs=vp8','video/webm'];
 if(typeof MediaRecorder.isTypeSupported!=='function')return '';
 for(var i=0;i<types.length;i++)if(MediaRecorder.isTypeSupported(types[i]))return types[i];
 return '';
}
var snap=function(){
 if(!storageLoaded){toast('正在读取存储设置，请稍后再试');loadStorage();return}
 if(outputNeedsSd()&&!sdMountedAvailable){toast('SD 卡未挂载');return}
 if(stream.readyState<2){toast('摄像头画面尚未就绪');return}
 canvas.width=stream.naturalWidth||640;canvas.height=stream.naturalHeight||480;
 ctx.drawImage(stream,0,0);showSaveProgress('正在生成快照...',null,true);
 canvas.toBlob(function(blob){
  if(!blob){hideSaveProgress();toast('快照生成失败');return}
  var name='ecmv_snap_'+Date.now()+'.jpg',needSd=outputNeedsSd(),needDownload=outputNeedsDownload();
  if(needDownload)downloadBlob(blob,name);
  var saveToSd=needSd?uploadBlobToSd(blob,'snap',function(r){showSaveProgress('SD 保存 '+Math.round(r*100)+'%',r*100,false)}):Promise.resolve();
  saveToSd.then(function(){
   finishSaveProgress(needSd&&needDownload?'SD 已保存；网页下载已开始':needSd?'快照已保存到 SD':'网页下载已开始');
   if(needSd)loadFiles();
  }).catch(function(e){
   finishSaveProgress(needDownload?'SD 保存失败；网页下载已开始':'SD 保存失败');
   toast(e.message);
  });
 },'image/jpeg',0.92);
}

var toggleRec=function(){
 var btn=$('recBtn');
 if(isRec){
  clearInterval(drawTimer);
  if(recorder&&recorder.state!=='inactive')recorder.stop();
  return;
 }
 if(recStarting)return;
 if(!storageLoaded){toast('正在读取存储设置，请稍后再试');loadStorage();return}
 if(outputNeedsSd()&&!sdMountedAvailable){toast('SD 卡未挂载');return}
 if(stream.readyState<2){toast('摄像头画面尚未就绪');return}
 if(typeof MediaRecorder==='undefined'){toast('此浏览器不支持录像');return}
 canvas.width=stream.naturalWidth||640;canvas.height=stream.naturalHeight||480;ctx.drawImage(stream,0,0);
 var capture=canvas.captureStream(25),requestedMime=getRecorderMime();
 try{recorder=requestedMime?new MediaRecorder(capture,{mimeType:requestedMime}):new MediaRecorder(capture)}
 catch(e){
  try{recorder=new MediaRecorder(capture);requestedMime=''}
  catch(e2){capture.getTracks().forEach(function(track){track.stop()});toast('无法启动录像：'+e2.message);return}
 }
 var actualMime=recorder.mimeType||requestedMime||'video/webm';
 var ext=/mp4/i.test(actualMime)?'mp4':'webm';
 var recordToSd=outputNeedsSd(),downloadRecording=outputNeedsDownload();
 recStarting=true;btn.textContent='WAIT';showSaveProgress('正在准备 '+ext.toUpperCase()+' 录像...',null,true);
 var prepare=recordToSd?apiPost('/api/browser-rec/start',{ext:ext}):Promise.resolve({ok:true});
 prepare.then(function(response){
  if(!response.ok)throw new Error(response.msg||'无法创建 SD 文件');
  chunks=[];
  recorder.ondataavailable=function(e){if(e.data.size>0)chunks.push(e.data)};
  recorder.onerror=function(e){toast('录像失败：'+(e.error&&e.error.message||'编码错误'))};
  recorder.onstop=function(){
   isRec=false;recStarting=false;btn.textContent='REC';btn.classList.remove('rec-on');
   capture.getTracks().forEach(function(track){track.stop()});
   if(!chunks.length){
    if(recordToSd)apiPost('/api/browser-rec/abort',{});
    finishSaveProgress('没有录到画面');toast('无画面数据');return;
   }
   var raw=new Blob(chunks,{type:actualMime}),elapsed=performance.now()-recStartedAt;
   showSaveProgress('正在封装 '+ext.toUpperCase()+'...',null,true);
   var packaged=/webm/i.test(actualMime)?patchWebmDuration(raw,elapsed):Promise.resolve({blob:raw,patched:true});
   packaged.then(function(result){
    var fileBlob=result.blob,name='ecmv_video_'+Date.now()+'.'+ext;
    if(downloadRecording)downloadBlob(fileBlob,name);
    var saveToSd=recordToSd?uploadBlobToSd(fileBlob,'rec',function(r){showSaveProgress('录像写入 SD '+Math.round(r*100)+'%',r*100,false)}).then(function(){
     return apiPost('/api/browser-rec/finish',{}).then(function(d){if(!d.ok)throw new Error(d.msg||'SD 文件收尾失败')});
    }):Promise.resolve();
    saveToSd.then(function(){
     finishSaveProgress(recordToSd&&downloadRecording?'SD 已保存；网页下载已开始':recordToSd?'录像已保存到 SD':'网页下载已开始');
     if(recordToSd)loadFiles();
     if(/webm/i.test(actualMime)&&!result.patched)toast('WebM 时长元数据未修复，但录像已保存');
    }).catch(function(e){
     if(recordToSd)apiPost('/api/browser-rec/abort',{});
     finishSaveProgress(downloadRecording?'SD 保存失败；网页下载已开始':'SD 保存失败');
     toast(e.message);
    });
   }).catch(function(e){
    if(recordToSd)apiPost('/api/browser-rec/abort',{});
    finishSaveProgress('录像封装失败');toast('录像封装失败：'+e.message);
   });
  };
  drawTimer=setInterval(function(){if(stream.readyState>=2)ctx.drawImage(stream,0,0,canvas.width,canvas.height)},40);
  recStartedAt=performance.now();
  try{recorder.start(1000)}catch(e){
   clearInterval(drawTimer);recStarting=false;btn.textContent='REC';
   capture.getTracks().forEach(function(track){track.stop()});
   if(recordToSd)apiPost('/api/browser-rec/abort',{});
   finishSaveProgress('录像未启动');toast('无法启动录像：'+e.message);return;
  }
  recStarting=false;isRec=true;
  btn.textContent='STOP';btn.classList.add('rec-on');
  showSaveProgress('录制中 · '+ext.toUpperCase(),null,true);
 }).catch(function(e){
  recStarting=false;btn.textContent='REC';capture.getTracks().forEach(function(track){track.stop()});
  finishSaveProgress('录像未启动');toast(e.message);
 });
}

loadStorage();
setInterval(function(){
 fetch('/api/status').then(function(r){return r.json()}).then(function(d){
  $('h-fps').textContent=d.fps||'--';$('h-temp').textContent=d.temp||'--';
  $('h-ram').textContent=(d.heap/1024).toFixed(0)+'K';
  $('h-wifi').textContent=d.staConnected?'STA':'AP';
  $('h-net').textContent=d.staConnected?d.staIP:d.apIP;
  frameDelay=d.res>=15?500:(d.res>=13?250:100);
  if($('tp3').classList.contains('active'))loadUsbStatus();
 }).catch(function(){});
},3000);
</script>
</body>
</html>
)rawliteral";

// 工具函数
String jsonEscape(const String &s) {
    String o;
    o.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

void sendJson(int code, const String &body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json; charset=utf-8", body);
}

String getStatusJson() {
    uint32_t heap = ESP.getFreeHeap();
    uint32_t tempC = temperatureRead();
    String j = "{";
    j += "\"res\":" + String(currentRes);
    j += ",\"qual\":" + String(currentQual);
    j += ",\"theme\":" + String(currentTheme);
    j += ",\"bri\":" + String(currentBri);
    j += ",\"con\":" + String(currentCon);
    j += ",\"sat\":" + String(currentSat);
    j += ",\"wb\":" + String(currentWb);
    j += ",\"recSeg\":" + String(recSegmentSec);
    j += ",\"recLoop\":" + String(recLoop ? "true" : "false");
    j += ",\"apSSID\":\"" + jsonEscape(apSSID) + "\"";
    j += ",\"apEnabled\":" + String(apEnabled ? "true" : "false");
    j += ",\"otgAsUsb\":" + String(otgAsUsb ? "true" : "false");
    j += ",\"hardwareProfile\":" + String(hardwareProfile);
    j += ",\"heap\":" + String(heap);
    j += ",\"temp\":" + String(tempC);
    j += ",\"staConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    j += ",\"staIP\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
    j += ",\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
    j += ",\"recording\":" + String(recording ? "true" : "false");
    j += ",\"frames\":" + String(recFrames);
    j += ",\"fps\":" + String(recording && (millis() - recStartMs) > 0 ? (recFrames * 1000ULL / (millis() - recStartMs)) : 0);
    j += "}";
    return j;
}

bool applyCameraSettings();

// 摄像头初始化
bool initCamera() {
    camConfig.ledc_channel = LEDC_CHANNEL_0;
    camConfig.ledc_timer = LEDC_TIMER_0;
    camConfig.pin_d0 = Y2_GPIO_NUM;
    camConfig.pin_d1 = Y3_GPIO_NUM;
    camConfig.pin_d2 = Y4_GPIO_NUM;
    camConfig.pin_d3 = Y5_GPIO_NUM;
    camConfig.pin_d4 = Y6_GPIO_NUM;
    camConfig.pin_d5 = Y7_GPIO_NUM;
    camConfig.pin_d6 = Y8_GPIO_NUM;
    camConfig.pin_d7 = Y9_GPIO_NUM;
    camConfig.pin_xclk = XCLK_GPIO_NUM;
    camConfig.pin_pclk = PCLK_GPIO_NUM;
    camConfig.pin_vsync = VSYNC_GPIO_NUM;
    camConfig.pin_href = HREF_GPIO_NUM;
    camConfig.pin_sscb_sda = SIOD_GPIO_NUM;
    camConfig.pin_sscb_scl = SIOC_GPIO_NUM;
    camConfig.pin_pwdn = PWDN_GPIO_NUM;
    camConfig.pin_reset = RESET_GPIO_NUM;
    bool highResolution = currentRes >= FRAMESIZE_UXGA;
    camConfig.xclk_freq_hz = highResolution ? 8000000 : 20000000;
    camConfig.pixel_format = PIXFORMAT_JPEG;
    camConfig.frame_size = (framesize_t)currentRes;
    camConfig.jpeg_quality = currentQual;
    camConfig.fb_count = highResolution ? 1 : 2;
    camConfig.fb_location = CAMERA_FB_IN_PSRAM;
    camConfig.grab_mode = highResolution ? CAMERA_GRAB_WHEN_EMPTY : CAMERA_GRAB_LATEST;
    esp_err_t err = esp_camera_init(&camConfig);
    if (err != ESP_OK) return false;
    sensor = esp_camera_sensor_get();
    if (!sensor) return false;
    if (!applyCameraSettings()) Serial.println("[camera] one or more persisted controls were rejected");
    return true;
}

bool isSupportedFrameSize(int size) {
    switch (size) {
        case FRAMESIZE_QVGA:
        case FRAMESIZE_CIF:
        case FRAMESIZE_HVGA:
        case FRAMESIZE_VGA:
        case FRAMESIZE_SVGA:
        case FRAMESIZE_XGA:
        case FRAMESIZE_HD:
        case FRAMESIZE_SXGA:
        case FRAMESIZE_UXGA:
        case FRAMESIZE_QXGA:
            return true;
        default:
            return false;
    }
}

bool applyCameraSettings() {
    if (!sensor) return false;
    auto setInt = [&](auto setter, int value) {
        return !setter || setter(sensor, value) == 0;
    };
    bool ok = true;
    ok = setInt(sensor->set_brightness, currentBri) && ok;
    ok = setInt(sensor->set_contrast, currentCon) && ok;
    ok = setInt(sensor->set_saturation, currentSat) && ok;
    ok = setInt(sensor->set_wb_mode, currentWb) && ok;
    ok = setInt(sensor->set_quality, currentQual) && ok;
    ok = setInt(sensor->set_sharpness, cameraExtendedControls.sharpness) && ok;
    ok = setInt(sensor->set_denoise, cameraExtendedControls.denoise) && ok;
    ok = setInt(sensor->set_special_effect, cameraExtendedControls.specialEffect) && ok;
    ok = setInt(sensor->set_whitebal, cameraExtendedControls.whiteBalance) && ok;
    ok = setInt(sensor->set_awb_gain, cameraExtendedControls.awbGain) && ok;
    ok = setInt(sensor->set_exposure_ctrl, cameraExtendedControls.exposureControl) && ok;
    ok = setInt(sensor->set_aec2, cameraExtendedControls.aec2) && ok;
    ok = setInt(sensor->set_ae_level, cameraExtendedControls.aeLevel) && ok;
    ok = setInt(sensor->set_aec_value, cameraExtendedControls.aecValue) && ok;
    ok = setInt(sensor->set_gain_ctrl, cameraExtendedControls.gainControl) && ok;
    ok = setInt(sensor->set_agc_gain, cameraExtendedControls.agcGain) && ok;
    if (sensor->set_gainceiling) {
        ok = sensor->set_gainceiling(sensor, (gainceiling_t)cameraExtendedControls.gainCeiling) == 0 && ok;
    }
    ok = setInt(sensor->set_hmirror, cameraExtendedControls.hmirror) && ok;
    ok = setInt(sensor->set_vflip, cameraExtendedControls.vflip) && ok;
    ok = setInt(sensor->set_dcw, cameraExtendedControls.dcw) && ok;
    ok = setInt(sensor->set_bpc, cameraExtendedControls.bpc) && ok;
    ok = setInt(sensor->set_wpc, cameraExtendedControls.wpc) && ok;
    ok = setInt(sensor->set_raw_gma, cameraExtendedControls.rawGma) && ok;
    ok = setInt(sensor->set_lenc, cameraExtendedControls.lenc) && ok;
    ok = setInt(sensor->set_colorbar, cameraExtendedControls.colorbar) && ok;
    return ok;
}

String sensorUnsupportedJson() {
    if (!sensor) return "[\"camera\"]";
    struct ControlSupport { const char *name; bool supported; };
    const ControlSupport controls[] = {
        {"brightness", sensor->set_brightness != nullptr},
        {"contrast", sensor->set_contrast != nullptr},
        {"saturation", sensor->set_saturation != nullptr},
        {"white balance", sensor->set_wb_mode != nullptr && sensor->set_whitebal != nullptr},
        {"sharpness", sensor->set_sharpness != nullptr},
        {"denoise", sensor->set_denoise != nullptr},
        {"special effect", sensor->set_special_effect != nullptr},
        {"AWB gain", sensor->set_awb_gain != nullptr},
        {"exposure", sensor->set_exposure_ctrl != nullptr && sensor->set_aec_value != nullptr},
        {"AE level", sensor->set_ae_level != nullptr},
        {"AEC2", sensor->set_aec2 != nullptr},
        {"gain control", sensor->set_gain_ctrl != nullptr && sensor->set_agc_gain != nullptr},
        {"gain ceiling", sensor->set_gainceiling != nullptr},
        {"horizontal mirror", sensor->set_hmirror != nullptr},
        {"vertical flip", sensor->set_vflip != nullptr},
        {"DCW", sensor->set_dcw != nullptr},
        {"BPC", sensor->set_bpc != nullptr},
        {"WPC", sensor->set_wpc != nullptr},
        {"gamma", sensor->set_raw_gma != nullptr},
        {"lens correction", sensor->set_lenc != nullptr},
        {"color bar", sensor->set_colorbar != nullptr}
    };
    String result = "[";
    bool first = true;
    for (const ControlSupport &control : controls) {
        if (control.supported) continue;
        if (!first) result += ",";
        first = false;
        result += "\"" + String(control.name) + "\"";
    }
    result += "]";
    return result;
}

fs::FS &mediaStorage() {
    if (useSD && sdMounted) return SD_MMC;
    return LittleFS;
}

bool initSDCard() {
    if (!SD_MMC.setPins(39, 38, 40)) {
        Serial.println("[sd] setPins failed");
        return false;
    }
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("[sd] mount failed");
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[sd] no card detected");
        SD_MMC.end();
        return false;
    }
    Serial.println("[sd] mounted (1-bit, CLK=39 CMD=38 D0=40)");
    return true;
}

void recordFrame(camera_fb_t *fb) {
    if (!recording || !recFile) return;
    size_t written = recFile.write(fb->buf, fb->len);
    if (written != fb->len) {
        Serial.println("[recording] frame write failed; stopping device recording");
        recFile.close();
        recording = false;
        recPath = "";
        return;
    }
    recFrames++;
    if (recSegmentSec > 0 && (millis() - recStartMs) >= (uint32_t)recSegmentSec * 1000UL) {
        recFile.close();
        recording = false;
        recPath = "";
    }
}

void processDeviceRecording() {
    if (!recording || !recFile) return;
    uint32_t now = millis();
    if ((uint32_t)(now - lastDeviceRecordAt) < 100) return;
    lastDeviceRecordAt = now;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;
    recordFrame(fb);
    esp_camera_fb_return(fb);
}

void drawTftStatusBar() {
    tft.fillRect(0, 0, tft.width(), 20, 0x0000);
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setCursor(4, 6);
    tft.print(recording ? "REC " : "CAM ");
    tft.print(currentRes == FRAMESIZE_QXGA ? "3MP " : String(currentRes) + " ");
    tft.print(WiFi.softAPIP().toString());
    tft.fillRect(0, tft.height() - 18, tft.width(), 18, 0x0000);
    tft.setCursor(4, tft.height() - 13);
    tft.print(recording ? "REC " : "READY ");
    tft.print(recFrames);
    tft.print("  RAM ");
    tft.print(ESP.getFreeHeap() / 1024);
    tft.print("K");
}

void renderTftStatusPage() {
    tft.fillScreen(0x0000);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("ECMV CAMERA");
    tft.setTextSize(1);
    tft.setCursor(10, 48);
    tft.print("AP: ");
    tft.print(WiFi.softAPIP().toString());
    tft.setCursor(10, 66);
    tft.print("Resolution: ");
    tft.print(currentRes == FRAMESIZE_QXGA ? "QXGA 3MP" : String(currentRes));
    tft.setCursor(10, 84);
    tft.print("Camera: ");
    tft.print(sensor ? "READY" : "ERROR");
    tft.setCursor(10, 102);
    tft.print("Recording: ");
    tft.print(recording ? "YES" : "NO");
    tft.setCursor(10, 120);
    tft.print("Frames: ");
    tft.print(recFrames);
    tft.setCursor(10, 138);
    tft.print("Storage: ");
    tft.print(useSD && sdMounted ? "SD" : "LittleFS");
    tft.setCursor(10, 156);
    tft.print("Heap: ");
    tft.print(ESP.getFreeHeap() / 1024);
    tft.print(" KB");
    tft.setCursor(10, 190);
    tft.print("BTN1 photo | BTN2 action");
    tft.setCursor(10, 208);
    tft.print("Buttons follow web settings");
}

void updateTftPreview() {
    if (!tftReady || !tftEnabled || !tftBacklightOn) return;
    uint32_t now = millis();
    bool renderDue = (uint32_t)(now - tftLastFrameAt) >= tftFrameIntervalMs;
    if (tftPage != 0) {
        if (renderDue && (uint32_t)(now - tftLastStatusAt) >= 1000) {
            tftLastFrameAt = now;
            tftLastStatusAt = now;
            renderTftStatusPage();
        }
        return;
    }
    if (!renderDue) return;
    tftLastFrameAt = now;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        tft.fillRect(0, 0, tft.width(), 20, 0x0000);
        tft.setTextColor(0xF800, 0x0000);
        tft.setCursor(4, 6);
        tft.print("CAMERA FRAME ERROR");
        return;
    }
    uint8_t scale = 1;
    if (tftFillScreen) {
        for (uint8_t candidate = 1; candidate <= 8; candidate *= 2) {
            uint16_t candidateW = (fb->width + candidate - 1) / candidate;
            uint16_t candidateH = (fb->height + candidate - 1) / candidate;
            if (candidateW < tft.width() || candidateH < tft.height()) break;
            scale = candidate;
        }
    } else {
        while (scale < 8 &&
               ((fb->width + scale - 1) / scale > tft.width() ||
                (fb->height + scale - 1) / scale > tft.height() - 38)) {
            scale *= 2;
        }
    }
    uint16_t renderW = (fb->width + scale - 1) / scale;
    uint16_t renderH = (fb->height + scale - 1) / scale;
    tftImageOffsetX = ((int16_t)tft.width() - renderW) / 2;
    tftImageOffsetY = tftFillScreen
        ? ((int16_t)tft.height() - renderH) / 2
        : 20 + (((int16_t)tft.height() - 38) - renderH) / 2;
    if (tftFillScreen && (renderW < tft.width() || renderH < tft.height())) {
        tft.fillScreen(0x0000);
    }
    TJpgDec.setJpgScale(scale);
    JRESULT result = TJpgDec.drawJpg(0, 0, fb->buf, fb->len);
    if (result == JDR_OK) {
        tftLastFrameWidth = fb->width;
        tftLastFrameHeight = fb->height;
        tftLastScale = scale;
        if (!tftFillScreen) drawTftStatusBar();
    } else {
        tft.fillRect(0, 0, tft.width(), 20, 0x0000);
        tft.setTextColor(0xF800, 0x0000);
        tft.setCursor(4, 6);
        tft.print("JPEG DECODE ERROR");
    }
    esp_camera_fb_return(fb);
}

void saveButtonSnapshot() {
    if (!sensor) {
        Serial.println("[button] photo skipped: camera is not ready");
        return;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[button] photo failed: no camera frame");
        return;
    }
    String path = "/button_" + String(millis()) + ".jpg";
    File file = mediaStorage().open(path, "w");
    if (!file) {
        Serial.println("[button] photo failed: cannot create file");
    } else {
        size_t written = file.write(fb->buf, fb->len);
        file.close();
        if (written != fb->len) {
            mediaStorage().remove(path);
            Serial.println("[button] photo failed: incomplete write");
        } else {
            Serial.printf("[button] photo saved: %s (%u bytes)\n", path.c_str(), (unsigned)written);
        }
    }
    esp_camera_fb_return(fb);
}

void toggleDeviceRecording() {
    if (!recording) {
        recPath = "/rec_" + String(millis()) + ".mjpeg";
        recFile = mediaStorage().open(recPath, "w");
        if (!recFile) {
            recPath = "";
            Serial.println("[button] recording start failed: cannot create file");
            return;
        }
        recording = true;
        recFrames = 0;
        recStartMs = millis();
        Serial.printf("[button] recording started: %s\n", recPath.c_str());
    } else {
        recording = false;
        if (recFile) recFile.close();
        Serial.printf("[button] recording stopped: %u frames\n", (unsigned)recFrames);
        recPath = "";
    }
}

void performButtonAction(int action, uint8_t buttonIndex) {
    switch (action) {
        case 1:
            saveButtonSnapshot();
            break;
        case 2:
            toggleDeviceRecording();
            break;
        case 3:
            tftPage = tftPage ? 0 : 1;
            prefs.putInt("tftPage", tftPage);
            tftLastStatusAt = 0;
            break;
        case 4:
            if (tftReady) {
                tftBacklightOn = !tftBacklightOn;
                digitalWrite(TFT_BL_GPIO, tftBacklightOn && tftEnabled ? HIGH : LOW);
                prefs.putBool("tftBL", tftBacklightOn);
            }
            break;
        default:
            break;
    }
    Serial.printf("[button] button %u action %d\n", (unsigned)buttonIndex + 1, action);
}

void initializeCameraButtons() {
    pinMode(BUTTON1_GPIO, INPUT_PULLUP);
    pinMode(BUTTON2_GPIO, INPUT_PULLUP);
    cameraButtons[0] = {BUTTON1_GPIO, digitalRead(BUTTON1_GPIO) == LOW,
                        digitalRead(BUTTON1_GPIO) == LOW, millis()};
    cameraButtons[1] = {BUTTON2_GPIO, digitalRead(BUTTON2_GPIO) == LOW,
                        digitalRead(BUTTON2_GPIO) == LOW, millis()};
}

void processCameraButtons() {
    for (uint8_t i = 0; i < 2; ++i) {
        CameraButtonState &button = cameraButtons[i];
        bool pressed = digitalRead(button.pin) == LOW;
        if (pressed != button.candidatePressed) {
            button.candidatePressed = pressed;
            button.candidateAt = millis();
        }
        if (pressed == button.stablePressed ||
            (uint32_t)(millis() - button.candidateAt) < 35) continue;
        button.stablePressed = pressed;
        if (!pressed) {
            uint8_t bit = (uint8_t)(1U << i);
            if (suppressButtonReleaseMask & bit) suppressButtonReleaseMask &= (uint8_t)~bit;
            else performButtonAction(i == 0 ? button1Action : button2Action, i);
        }
    }
}

void processRecoveryButtonCombo() {
    static uint32_t pressedAt = 0;
    static bool restored = false;
    bool bothPressed = digitalRead(BUTTON1_GPIO) == LOW && digitalRead(BUTTON2_GPIO) == LOW;
    if (!bothPressed) {
        pressedAt = 0;
        restored = false;
        return;
    }
    if (!pressedAt) {
        pressedAt = millis();
        return;
    }
    if (restored || uiMode != 2 || (uint32_t)(millis() - pressedAt) < 5000) return;
    restored = true;
    suppressButtonReleaseMask = 0x03;

    bool saved = prefsReady &&
                 prefs.putInt("uiMode", 0) == sizeof(int) &&
                 prefs.putInt("hwProfile", HW_PROFILE_DISPLAY) == sizeof(int) &&
                 prefs.putBool("tftOn", true) == sizeof(bool) &&
                 prefs.getInt("uiMode", -1) == 0;
    if (!saved) Serial.println("[recovery] NVS save failed; enabling web access for this boot");

    uiMode = 0;
    hardwareProfile = HW_PROFILE_DISPLAY;
    tftEnabled = true;
    networkEnabled = true;
    webEnabled = true;
    if (!tftReady) {
        tftReady = initTftDisplay();
        if (!tftReady) Serial.println("[recovery] TFT initialization failed");
    }
    WiFi.mode(WIFI_AP_STA);
    bool apStarted = WiFi.softAP(apSSID.c_str(), apPass.c_str());
    Serial.printf("[recovery] AP %s: %s\n", apStarted ? "started" : "failed",
                  WiFi.softAPIP().toString().c_str());
    if (staSSID.length()) WiFi.begin(staSSID.c_str(), staPass.c_str());
    if (!serverStarted) {
        server.begin();
        serverStarted = true;
    }
    Serial.println("[recovery] web and Wi-Fi restored");
}

void handleFrame() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { sendJson(503, "{\"msg\":\"camera frame unavailable\"}"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg", "");
    server.sendContent((const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// 文件列表
String listFilesJson() {
    File root = mediaStorage().open("/");
    if (!root) return "{\"files\":[]}";
    String j = "{\"files\":[";
    bool first = true;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            if (!first) j += ",";
            first = false;
            j += "{\"name\":\"" + jsonEscape(String(f.name())) + "\",\"size\":" + String(f.size()) + "}";
        }
        f = root.openNextFile();
    }
    root.close();
    j += "]}";
    return j;
}

// 文件下载
void handleDownload() {
    if (!server.hasArg("f")) { server.send(400, "text/plain", "missing f"); return; }
    String name = server.arg("f");
    if (name.indexOf("..") >= 0) { server.send(403, "text/plain", "forbidden"); return; }
    String path = name.startsWith("/") ? name : "/" + name;
    File f = mediaStorage().open(path, "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    String ct = "application/octet-stream";
    String lowerPath = path;
    lowerPath.toLowerCase();
    if (lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg")) ct = "image/jpeg";
    else if (lowerPath.endsWith(".mjpeg")) ct = "video/x-motion-jpeg";
    else if (lowerPath.endsWith(".avi")) ct = "video/x-msvideo";
    else if (lowerPath.endsWith(".webm")) ct = "video/webm";
    else if (lowerPath.endsWith(".mp4")) ct = "video/mp4";
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.streamFile(f, ct);
    f.close();
}

void handleMediaPreview() {
    if (!server.hasArg("f")) { server.send(400, "text/plain", "missing f"); return; }
    String name = server.arg("f");
    if (name.indexOf("..") >= 0) { server.send(403, "text/plain", "forbidden"); return; }
    String path = name.startsWith("/") ? name : "/" + name;
    String lowerPath = path;
    lowerPath.toLowerCase();
    String ct;
    if (lowerPath.endsWith(".webm")) ct = "video/webm";
    else if (lowerPath.endsWith(".mp4")) ct = "video/mp4";
    else if (lowerPath.endsWith(".avi")) ct = "video/x-msvideo";
    else { server.send(415, "text/plain", "unsupported media type"); return; }
    File f = mediaStorage().open(path, "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.sendHeader("Content-Disposition", "inline; filename=\"" + name + "\"");
    server.streamFile(f, ct);
    f.close();
}

void handleMediaUploadChunk() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        mediaUploadKind = server.arg("kind");
        mediaUploadError = false;
        long expectedSize = server.arg("size").toInt();
        mediaUploadExpectedSize = expectedSize > 0 ? (size_t)expectedSize : 0;
        mediaUploadBytes = 0;
        snapUploadPath = "";
        if (!sdMounted || mediaUploadExpectedSize == 0) {
            mediaUploadError = true;
        } else if (mediaUploadKind == "snap") {
            snapUploadPath = "/snap_" + String(millis()) + ".jpg";
            snapUploadFile = SD_MMC.open(snapUploadPath, "w");
            if (!snapUploadFile) mediaUploadError = true;
        } else if (mediaUploadKind == "rec") {
            if (!browserRecActive || !browserRecFile) mediaUploadError = true;
        } else {
            mediaUploadError = true;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File *target = mediaUploadKind == "snap" ? &snapUploadFile :
                       mediaUploadKind == "rec" ? &browserRecFile : nullptr;
        if (!mediaUploadError && target && *target) {
            size_t written = target->write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) mediaUploadError = true;
            else mediaUploadBytes += written;
        } else if (!mediaUploadError) {
            mediaUploadError = true;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (mediaUploadBytes != mediaUploadExpectedSize) mediaUploadError = true;
        if (mediaUploadKind == "snap" && snapUploadFile) {
            snapUploadFile.flush();
            if (snapUploadFile.size() == 0) mediaUploadError = true;
            snapUploadFile.close();
        } else if (mediaUploadKind == "rec" && browserRecFile) {
            browserRecFile.flush();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        mediaUploadError = true;
        if (mediaUploadKind == "snap" && snapUploadFile) snapUploadFile.close();
        if (mediaUploadKind == "rec") browserRecError = true;
    }
}

void handleMediaUploadComplete() {
    if (mediaUploadError) {
        if (mediaUploadKind == "snap") {
            if (snapUploadFile) snapUploadFile.close();
            if (snapUploadPath.length()) SD_MMC.remove(snapUploadPath);
        } else if (mediaUploadKind == "rec") {
            browserRecError = true;
        }
        sendJson(500, "{\"ok\":false,\"msg\":\"SD 写入失败，检查卡片空间与连接\"}");
        return;
    }
    if (mediaUploadKind == "snap") {
        sendJson(200, "{\"ok\":true,\"path\":\"" + jsonEscape(snapUploadPath) + "\"}");
    } else if (mediaUploadKind == "rec") {
        sendJson(200, "{\"ok\":true}");
    } else {
        sendJson(400, "{\"ok\":false,\"msg\":\"未知上传类型\"}");
    }
}

// 视频流
void handleStream() {
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();
    while (client.connected()) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { delay(10); continue; }
        client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.print("\r\n");
        esp_camera_fb_return(fb);
        if (!client.connected()) break;
    }
}

// API 路由
void handleApiStatus() { sendJson(200, getStatusJson()); }

void handleApiNet() {
    String j = "{";
    j += "\"apOn\":" + String(apEnabled ? "true" : "false");
    j += ",\"apSSID\":\"" + jsonEscape(apSSID) + "\"";
    j += ",\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
    bool sc = WiFi.status() == WL_CONNECTED;
    j += ",\"staConnected\":" + String(sc ? "true" : "false");
    j += ",\"staSSID\":\"" + (sc ? jsonEscape(WiFi.SSID()) : String("")) + "\"";
    j += ",\"staIP\":\"" + (sc ? WiFi.localIP().toString() : String("")) + "\"";
    j += ",\"rssi\":" + String(sc ? WiFi.RSSI() : 0);
    j += ",\"ble\":" + String(bleActive ? "true" : "false");
    j += "}";
    sendJson(200, j);
}

void handleApiFiles() { sendJson(200, listFilesJson()); }

void handleApiStorageGet() {
    String j = "{\"sdMounted\":";
    j += sdMounted ? "true" : "false";
    j += ",\"useSD\":";
    j += useSD ? "true" : "false";
    j += ",\"recOutput\":" + String(recOutput);
    j += ",\"profile\":" + String(hardwareProfile);
    j += ",\"sdPins\":\"CLK=39,CMD=38,D0=40\"}";
    sendJson(200, j);
}

void handleApiBoardConfigGet() {
    String j = "{\"mode\":" + String(uiMode);
    j += ",\"profile\":" + String(hardwareProfile);
    j += ",\"button1Action\":" + String(button1Action);
    j += ",\"button2Action\":" + String(button2Action);
    j += ",\"tftReady\":" + String(tftReady ? "true" : "false");
    j += ",\"tftEnabled\":" + String(tftEnabled ? "true" : "false");
    j += ",\"tftFillScreen\":" + String(tftFillScreen ? "true" : "false");
    j += ",\"tftColorCorrection\":" + String(tftColorCorrection ? "true" : "false");
    j += ",\"tftBacklightOn\":" + String(tftBacklightOn ? "true" : "false");
    j += ",\"tftRotation\":" + String(tftRotation);
    j += ",\"tftInterval\":" + String(tftFrameIntervalMs);
    j += ",\"networkEnabled\":" + String(networkEnabled ? "true" : "false");
    j += ",\"webEnabled\":" + String(webEnabled ? "true" : "false");
    j += ",\"nvsReady\":" + String(prefsReady ? "true" : "false");
    j += ",\"button1Level\":" + String(digitalRead(BUTTON1_GPIO));
    j += ",\"button2Level\":" + String(digitalRead(BUTTON2_GPIO));
    j += ",\"sdMounted\":" + String(sdMounted ? "true" : "false");
    j += ",\"usbHostReady\":" + String(usbHostReady ? "true" : "false") + "}";
    sendJson(200, j);
}

void handleApiBoardConfigSet() {
    String body = server.arg("plain");
    int p = body.indexOf("\"mode\":");
    if (p < 0) { sendJson(400, "{\"ok\":false,\"msg\":\"缺少运行模式\"}"); return; }
    int mode = body.substring(p + 7).toInt();
    if (mode < 0 || mode > 2) {
        sendJson(400, "{\"ok\":false,\"msg\":\"无效的运行模式\"}");
        return;
    }
    if (!prefsReady) {
        sendJson(503, "{\"ok\":false,\"msg\":\"NVS 未初始化；请检查启动串口日志\"}");
        return;
    }
    int nextProfile = mode == 1 ? HW_PROFILE_STORAGE_USB : HW_PROFILE_DISPLAY;
    bool nextTftEnabled = mode != 1;
    size_t modeWritten = prefs.putInt("uiMode", mode);
    size_t profileWritten = prefs.putInt("hwProfile", nextProfile);
    size_t tftWritten = prefs.putBool("tftOn", nextTftEnabled);
    bool persisted = modeWritten == sizeof(int) &&
                     profileWritten == sizeof(int) &&
                     tftWritten == sizeof(bool) &&
                     prefs.getInt("uiMode", -1) == mode &&
                     prefs.getInt("hwProfile", -1) == nextProfile &&
                     prefs.getBool("tftOn", !nextTftEnabled) == nextTftEnabled;
    if (!persisted) {
        prefs.putInt("uiMode", uiMode);
        prefs.putInt("hwProfile", hardwareProfile);
        prefs.putBool("tftOn", tftEnabled);
        sendJson(500, "{\"ok\":false,\"msg\":\"NVS 写入或回读校验失败；已尝试恢复旧方案\"}");
        return;
    }
    bool restart = mode != uiMode || nextProfile != hardwareProfile || nextTftEnabled != tftEnabled;
    if (!restart) {
        sendJson(200, "{\"ok\":true,\"restart\":false,\"msg\":\"当前方案已保存并通过回读校验\"}");
        return;
    }
    restartPending = true;
    restartAt = millis() + 1200;
    sendJson(200, "{\"ok\":true,\"restart\":true,\"msg\":\"运行模式已保存并通过回读校验，设备即将重启\"}");
}

void handleApiTftConfigSet() {
    if (hardwareProfile != HW_PROFILE_DISPLAY) {
        sendJson(409, "{\"ok\":false,\"msg\":\"当前方案未启用 TFT；请先切换到屏幕方案\"}");
        return;
    }
    if (!prefsReady) {
        sendJson(503, "{\"ok\":false,\"msg\":\"NVS 未初始化；请检查启动串口日志\"}");
        return;
    }
    String body = server.arg("plain");
    int enabledPos = body.indexOf("\"enabled\":");
    int rotationPos = body.indexOf("\"rotation\":");
    int fillPos = body.indexOf("\"fill\":");
    int colorPos = body.indexOf("\"colorCorrection\":");
    int intervalPos = body.indexOf("\"interval\":");
    if (enabledPos < 0 || rotationPos < 0 || fillPos < 0 || colorPos < 0 || intervalPos < 0) {
        sendJson(400, "{\"ok\":false,\"msg\":\"屏幕配置字段不完整\"}");
        return;
    }
    bool nextEnabled = body.substring(enabledPos + 10).startsWith("true");
    int nextRotation = body.substring(rotationPos + 11).toInt();
    int nextFill = body.substring(fillPos + 7).toInt();
    int nextColorCorrection = body.substring(colorPos + 18).toInt();
    int nextInterval = body.substring(intervalPos + 11).toInt();
    if (nextRotation < 0 || nextRotation > 3 || (nextFill != 0 && nextFill != 1) ||
        (nextColorCorrection != 0 && nextColorCorrection != 1) ||
        (nextInterval != 250 && nextInterval != 500 && nextInterval != 750 &&
         nextInterval != 1000 && nextInterval != 1500)) {
        sendJson(400, "{\"ok\":false,\"msg\":\"屏幕方向或刷新间隔无效\"}");
        return;
    }

    bool oldEnabled = tftEnabled;
    bool oldFill = tftFillScreen;
    bool oldColorCorrection = tftColorCorrection;
    uint8_t oldRotation = tftRotation;
    uint16_t oldInterval = tftFrameIntervalMs;
    size_t enabledWritten = prefs.putBool("tftOn", nextEnabled);
    size_t rotationWritten = prefs.putInt("tftRot", nextRotation);
    size_t fillWritten = prefs.putBool("tftFill", nextFill != 0);
    size_t colorWritten = prefs.putBool("tftColor", nextColorCorrection != 0);
    size_t intervalWritten = prefs.putInt("tftInt", nextInterval);
    bool persisted = enabledWritten == sizeof(bool) &&
                     rotationWritten == sizeof(int) &&
                     fillWritten == sizeof(bool) &&
                     colorWritten == sizeof(bool) &&
                     intervalWritten == sizeof(int) &&
                     prefs.getBool("tftOn", !nextEnabled) == nextEnabled &&
                     prefs.getInt("tftRot", -1) == nextRotation &&
                     prefs.getBool("tftFill", !(nextFill != 0)) == (nextFill != 0) &&
                     prefs.getBool("tftColor", !(nextColorCorrection != 0)) == (nextColorCorrection != 0) &&
                     prefs.getInt("tftInt", -1) == nextInterval;
    if (!persisted) {
        prefs.putBool("tftOn", oldEnabled);
        prefs.putInt("tftRot", oldRotation);
        prefs.putBool("tftFill", oldFill);
        prefs.putBool("tftColor", oldColorCorrection);
        prefs.putInt("tftInt", oldInterval);
        sendJson(500, "{\"ok\":false,\"msg\":\"屏幕配置持久化失败\"}");
        return;
    }
    tftEnabled = nextEnabled;
    tftFillScreen = nextFill != 0;
    tftColorCorrection = nextColorCorrection != 0;
    tftRotation = nextRotation;
    tftFrameIntervalMs = nextInterval;
    if (tftEnabled && !tftReady && !initTftDisplay()) {
        tftEnabled = oldEnabled;
        tftFillScreen = oldFill;
        tftColorCorrection = oldColorCorrection;
        tftRotation = oldRotation;
        tftFrameIntervalMs = oldInterval;
        prefs.putBool("tftOn", oldEnabled);
        prefs.putInt("tftRot", oldRotation);
        prefs.putBool("tftFill", oldFill);
        prefs.putBool("tftColor", oldColorCorrection);
        prefs.putInt("tftInt", oldInterval);
        sendJson(500, "{\"ok\":false,\"msg\":\"TFT 初始化失败；请检查排线与屏幕供电\"}");
        return;
    }
    if (tftReady) {
        tft.setColorCorrection(tftColorCorrection);
        tft.setRotation(tftRotation);
        digitalWrite(TFT_BL_GPIO, tftEnabled && tftBacklightOn ? HIGH : LOW);
        tftLastFrameAt = 0;
        tftLastStatusAt = 0;
    }
    String response = "{\"ok\":true,\"enabled\":";
    response += tftEnabled ? "true" : "false";
    response += ",\"fill\":";
    response += tftFillScreen ? "true" : "false";
    response += ",\"colorCorrection\":";
    response += tftColorCorrection ? "true" : "false";
    response += ",\"msg\":\"屏幕配置已保存\"}";
    sendJson(200, response);
}

void handleApiTftBacklightToggle() {
    if (hardwareProfile != HW_PROFILE_DISPLAY || !tftReady) {
        sendJson(409, "{\"ok\":false,\"msg\":\"当前没有可用的 TFT 屏幕\"}");
        return;
    }
    tftBacklightOn = !tftBacklightOn;
    digitalWrite(TFT_BL_GPIO, tftEnabled && tftBacklightOn ? HIGH : LOW);
    if (prefs.putBool("tftBL", tftBacklightOn) == 0) {
        tftBacklightOn = !tftBacklightOn;
        digitalWrite(TFT_BL_GPIO, tftEnabled && tftBacklightOn ? HIGH : LOW);
        sendJson(500, "{\"ok\":false,\"msg\":\"背光设置持久化失败\"}");
        return;
    }
    String response = "{\"ok\":true,\"msg\":\"背光已";
    response += tftBacklightOn ? "开启\"}" : "关闭\"}";
    sendJson(200, response);
}

void handleApiButtonConfigSet() {
    String body = server.arg("plain");
    auto num = [&](const char *key, int current, bool &found) -> int {
        int p = body.indexOf(String("\"") + key + "\":");
        if (p < 0) { found = false; return current; }
        found = true;
        return body.substring(p + strlen(key) + 3).toInt();
    };
    bool hasButton1 = false, hasButton2 = false;
    int nextButton1 = num("button1Action", button1Action, hasButton1);
    int nextButton2 = num("button2Action", button2Action, hasButton2);
    if (!hasButton1 || !hasButton2 || nextButton1 < 0 || nextButton1 > 4 ||
        nextButton2 < 0 || nextButton2 > 4) {
        sendJson(400, "{\"ok\":false,\"msg\":\"按键动作配置无效\"}");
        return;
    }
    int oldButton1 = button1Action, oldButton2 = button2Action;
    if (prefs.putInt("btn1Act", nextButton1) == 0 || prefs.putInt("btn2Act", nextButton2) == 0) {
        prefs.putInt("btn1Act", oldButton1);
        prefs.putInt("btn2Act", oldButton2);
        sendJson(500, "{\"ok\":false,\"msg\":\"按键配置持久化失败\"}");
        return;
    }
    button1Action = nextButton1;
    button2Action = nextButton2;
    sendJson(200, "{\"ok\":true,\"msg\":\"按键动作已保存\"}");
}

void handleApiStorageSet() {
    String body = server.arg("plain");
    int p = body.indexOf("\"recOutput\":");
    if (p < 0) { sendJson(400, "{\"ok\":false,\"msg\":\"缺少 REC / SNAP 保存位置\"}"); return; }
    int requestedOutput = body.substring(p + 12).toInt();
    if (requestedOutput < 1 || requestedOutput > 3) {
        sendJson(400, "{\"ok\":false,\"msg\":\"无效的保存位置\"}");
        return;
    }
    bool requestedSD = requestedOutput > 1;
    if (requestedSD && hardwareProfile == HW_PROFILE_DISPLAY) {
        sendJson(409, "{\"ok\":false,\"msg\":\"当前TFT方案与SD引脚复用；切换到SD卡+USB Host方案并重启后再保存\"}");
        return;
    }
    if (requestedSD && !sdMounted) {
        sendJson(409, "{\"ok\":false,\"msg\":\"未检测到板载 SD 卡\"}");
        return;
    }
    int previousOutput = recOutput;
    bool previousStorage = useSD;
    recOutput = requestedOutput;
    useSD = requestedSD;
    if (prefs.putInt("recOut", recOutput) == 0 || prefs.putBool("useSD", useSD) == 0) {
        prefs.putInt("recOut", previousOutput);
        prefs.putBool("useSD", previousStorage);
        recOutput = previousOutput;
        useSD = previousStorage;
        sendJson(500, "{\"ok\":false,\"msg\":\"保存位置写入失败\"}");
        return;
    }
    sendJson(200, "{\"ok\":true,\"msg\":\"REC / SNAP 保存位置已保存\"}");
}

void handleApiDelete() {
    String body = server.arg("plain");
    int p = body.indexOf("\"name\":\"");
    if (p < 0) { sendJson(400, "{\"msg\":\"bad\"}"); return; }
    int s = p + 8; int e = body.indexOf('"', s);
    String name = body.substring(s, e);
    if (name.indexOf("..") >= 0) { sendJson(403, "{\"msg\":\"forbidden\"}"); return; }
    String path = name.startsWith("/") ? name : "/" + name;
    bool ok = mediaStorage().remove(path);
    sendJson(200, ok ? "{\"msg\":\"deleted\"}" : "{\"msg\":\"fail\"}");
}

void handleApiDeleteAll() {
    File root = mediaStorage().open("/");
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String p = f.name();
            if (!p.startsWith("/")) p = "/" + p;
            mediaStorage().remove(p);
        }
        f = root.openNextFile();
    }
    root.close();
    sendJson(200, "{\"msg\":\"all deleted\"}");
}

void handleApiCam() {
    String body = server.arg("plain");
    auto num = [&](const char *key, int def) -> int {
        int p = body.indexOf(String("\"") + key + "\":");
        if (p < 0) return def;
        return body.substring(p + strlen(key) + 3).toInt();
    };
    int previousRes = currentRes;
    int previousQual = currentQual;
    int previousTheme = currentTheme;
    int previousBri = currentBri;
    int previousCon = currentCon;
    int previousSat = currentSat;
    int previousWb = currentWb;
    currentRes = num("res", currentRes);
    if (!isSupportedFrameSize(currentRes)) {
        currentRes = previousRes;
        sendJson(400, "{\"msg\":\"不支持的分辨率\"}");
        return;
    }
    bool resolutionChanged = currentRes != previousRes;
    currentQual = num("qual", currentQual);
    currentTheme = num("theme", currentTheme);
    currentBri = num("bri", currentBri);
    currentCon = num("con", currentCon);
    currentSat = num("sat", currentSat);
    currentWb = num("wb", currentWb);
    bool cameraApplied;
    if (resolutionChanged) {
        esp_camera_deinit();
        sensor = nullptr;
        cameraApplied = initCamera();
    } else {
        cameraApplied = applyCameraSettings();
    }
    if (!cameraApplied) {
        currentRes = previousRes;
        currentQual = previousQual;
        currentTheme = previousTheme;
        currentBri = previousBri;
        currentCon = previousCon;
        currentSat = previousSat;
        currentWb = previousWb;
        if (resolutionChanged) {
            esp_camera_deinit();
            sensor = nullptr;
            if (!initCamera()) Serial.println("[camera] failed to restore previous settings");
        } else {
            applyCameraSettings();
        }
        sendJson(422, "{\"msg\":\"摄像头无法应用该分辨率\"}");
        return;
    }
    prefs.putInt("res", currentRes); prefs.putInt("qual", currentQual);
    prefs.putInt("theme", currentTheme); prefs.putInt("bri", currentBri);
    prefs.putInt("con", currentCon); prefs.putInt("sat", currentSat);
    prefs.putInt("wb", currentWb);
    sendJson(200, "{\"msg\":\"画面参数已应用\"}");
}

void handleApiRec() {
    String body = server.arg("plain");
    int p = body.indexOf("\"seg\":");
    if (p >= 0) recSegmentSec = body.substring(p + 6).toInt();
    recLoop = body.indexOf("\"loop\":true") >= 0;
    prefs.putInt("recSeg", recSegmentSec);
    prefs.putBool("recLoop", recLoop);
    sendJson(200, "{\"msg\":\"录像设置已保存\"}");
}

void handleApiAp() {
    String body = server.arg("plain");
    int p = body.indexOf("\"ssid\":\"");
    if (p >= 0) {
        int s = p + 8, e = body.indexOf('"', s);
        String v = body.substring(s, e);
        if (v.length()) apSSID = v;
    }
    p = body.indexOf("\"pass\":\"");
    if (p >= 0) {
        int s = p + 8, e = body.indexOf('"', s);
        String v = body.substring(s, e);
        if (v.length() >= 8) apPass = v;
    }
    apEnabled = true;
    prefs.putString("apSSID", apSSID); prefs.putString("apPass", apPass);
    prefs.putBool("apEnabled", apEnabled);
    sendJson(200, "{\"msg\":\"已保存，重启后热点配置完全生效\"}");
}

void handleApiSta() {
    String body = server.arg("plain");
    int p = body.indexOf("\"ssid\":\"");
    if (p < 0) { sendJson(400, "{\"msg\":\"no ssid\"}"); return; }
    int s = p + 8, e = body.indexOf('"', s);
    String ssid = body.substring(s, e);
    p = body.indexOf("\"pass\":\"");
    String pass = "";
    if (p >= 0) { s = p + 8; e = body.indexOf('"', s); pass = body.substring(s, e); }
    staSSID = ssid; staPass = pass;
    prefs.putString("staSSID", staSSID); prefs.putString("staPass", staPass);
    WiFi.begin(staSSID.c_str(), staPass.c_str());
    sendJson(200, "{\"msg\":\"正在连接 \" + jsonEscape(ssid) + \"\"}");
}

void handleApiSensor() {
    String body = server.arg("plain");
    auto num = [&](const char *key, int def) -> int {
        int p = body.indexOf(String("\"") + key + "\":");
        if (p < 0) return def;
        return body.substring(p + strlen(key) + 3).toInt();
    };
    CameraExtendedControls previousControls = cameraExtendedControls;
    int previousBri = currentBri;
    int previousCon = currentCon;
    int previousSat = currentSat;
    int previousWb = currentWb;
    currentBri = num("bri", currentBri);
    currentCon = num("con", currentCon);
    currentSat = num("sat", currentSat);
    currentWb = num("wb", currentWb);
    cameraExtendedControls.sharpness = num("sharp", cameraExtendedControls.sharpness);
    cameraExtendedControls.denoise = num("denoise", cameraExtendedControls.denoise);
    cameraExtendedControls.specialEffect = num("effect", cameraExtendedControls.specialEffect);
    cameraExtendedControls.whiteBalance = num("awb", cameraExtendedControls.whiteBalance) != 0;
    cameraExtendedControls.awbGain = num("awbGain", cameraExtendedControls.awbGain) != 0;
    cameraExtendedControls.exposureControl = num("aec", cameraExtendedControls.exposureControl) != 0;
    cameraExtendedControls.aec2 = num("aec2", cameraExtendedControls.aec2) != 0;
    cameraExtendedControls.aeLevel = num("aeLevel", cameraExtendedControls.aeLevel);
    cameraExtendedControls.aecValue = num("aecValue", cameraExtendedControls.aecValue);
    cameraExtendedControls.gainControl = num("agc", cameraExtendedControls.gainControl) != 0;
    cameraExtendedControls.agcGain = num("agcGain", cameraExtendedControls.agcGain);
    cameraExtendedControls.gainCeiling = num("gainCeiling", cameraExtendedControls.gainCeiling);
    cameraExtendedControls.hmirror = num("hmirror", cameraExtendedControls.hmirror) != 0;
    cameraExtendedControls.vflip = num("vflip", cameraExtendedControls.vflip) != 0;
    cameraExtendedControls.dcw = num("dcw", cameraExtendedControls.dcw) != 0;
    cameraExtendedControls.bpc = num("bpc", cameraExtendedControls.bpc) != 0;
    cameraExtendedControls.wpc = num("wpc", cameraExtendedControls.wpc) != 0;
    cameraExtendedControls.rawGma = num("rawGma", cameraExtendedControls.rawGma) != 0;
    cameraExtendedControls.lenc = num("lenc", cameraExtendedControls.lenc) != 0;
    cameraExtendedControls.colorbar = num("colorbar", cameraExtendedControls.colorbar) != 0;

    bool valid = currentBri >= -2 && currentBri <= 2 &&
                 currentCon >= -2 && currentCon <= 2 &&
                 currentSat >= -2 && currentSat <= 2 &&
                 currentWb >= 0 && currentWb <= 4 &&
                 cameraExtendedControls.sharpness >= -2 && cameraExtendedControls.sharpness <= 2 &&
                 cameraExtendedControls.denoise <= 8 &&
                 cameraExtendedControls.specialEffect <= 6 &&
                 cameraExtendedControls.aeLevel >= -2 && cameraExtendedControls.aeLevel <= 2 &&
                 cameraExtendedControls.aecValue <= 1200 &&
                 cameraExtendedControls.agcGain <= 30 &&
                 cameraExtendedControls.gainCeiling <= 6;
    if (!valid) {
        cameraExtendedControls = previousControls;
        currentBri = previousBri;
        currentCon = previousCon;
        currentSat = previousSat;
        currentWb = previousWb;
        sendJson(400, "{\"ok\":false,\"msg\":\"传感器参数超出有效范围\"}");
        return;
    }
    if (!applyCameraSettings()) {
        cameraExtendedControls = previousControls;
        currentBri = previousBri;
        currentCon = previousCon;
        currentSat = previousSat;
        currentWb = previousWb;
        applyCameraSettings();
        sendJson(503, "{\"ok\":false,\"msg\":\"摄像头驱动拒绝了部分参数，已恢复原设置\"}");
        return;
    }

    bool persisted = prefs.putBytes("camCtl", &cameraExtendedControls, sizeof(cameraExtendedControls)) == sizeof(cameraExtendedControls);
    persisted = (prefs.putInt("bri", currentBri) > 0) && persisted;
    persisted = (prefs.putInt("con", currentCon) > 0) && persisted;
    persisted = (prefs.putInt("sat", currentSat) > 0) && persisted;
    persisted = (prefs.putInt("wb", currentWb) > 0) && persisted;
    if (!persisted) {
        cameraExtendedControls = previousControls;
        currentBri = previousBri;
        currentCon = previousCon;
        currentSat = previousSat;
        currentWb = previousWb;
        prefs.putBytes("camCtl", &cameraExtendedControls, sizeof(cameraExtendedControls));
        prefs.putInt("bri", currentBri);
        prefs.putInt("con", currentCon);
        prefs.putInt("sat", currentSat);
        prefs.putInt("wb", currentWb);
        applyCameraSettings();
        sendJson(500, "{\"ok\":false,\"msg\":\"传感器设置持久化失败，已回滚运行参数\"}");
        return;
    }
    sendJson(200, "{\"ok\":true,\"msg\":\"传感器参数已应用并保存\",\"unsupported\":" + sensorUnsupportedJson() + "}");
}

void handleApiSensorGet() {
    String j = "{\"bri\":" + String(currentBri) + ",\"con\":" + String(currentCon) +
               ",\"sat\":" + String(currentSat) + ",\"wb\":" + String(currentWb) +
               ",\"sharp\":" + String(cameraExtendedControls.sharpness) +
               ",\"denoise\":" + String(cameraExtendedControls.denoise) +
               ",\"effect\":" + String(cameraExtendedControls.specialEffect) +
               ",\"awb\":" + String(cameraExtendedControls.whiteBalance ? "true" : "false") +
               ",\"awbGain\":" + String(cameraExtendedControls.awbGain ? "true" : "false") +
               ",\"aec\":" + String(cameraExtendedControls.exposureControl ? "true" : "false") +
               ",\"aec2\":" + String(cameraExtendedControls.aec2 ? "true" : "false") +
               ",\"aeLevel\":" + String(cameraExtendedControls.aeLevel) +
               ",\"aecValue\":" + String(cameraExtendedControls.aecValue) +
               ",\"agc\":" + String(cameraExtendedControls.gainControl ? "true" : "false") +
               ",\"agcGain\":" + String(cameraExtendedControls.agcGain) +
               ",\"gainCeiling\":" + String(cameraExtendedControls.gainCeiling) +
               ",\"hmirror\":" + String(cameraExtendedControls.hmirror ? "true" : "false") +
               ",\"vflip\":" + String(cameraExtendedControls.vflip ? "true" : "false") +
               ",\"dcw\":" + String(cameraExtendedControls.dcw ? "true" : "false") +
               ",\"bpc\":" + String(cameraExtendedControls.bpc ? "true" : "false") +
               ",\"wpc\":" + String(cameraExtendedControls.wpc ? "true" : "false") +
               ",\"rawGma\":" + String(cameraExtendedControls.rawGma ? "true" : "false") +
               ",\"lenc\":" + String(cameraExtendedControls.lenc ? "true" : "false") +
               ",\"colorbar\":" + String(cameraExtendedControls.colorbar ? "true" : "false") +
               ",\"sensorPid\":" + String(sensor ? sensor->id.PID : 0) +
               ",\"unsupported\":" + sensorUnsupportedJson() + "}";
    sendJson(200, j);
}

void handleApiOtg() {
    String body = server.arg("plain");
    int p = body.indexOf("\"usb\":");
    if (p >= 0) otgAsUsb = body.substring(p + 6).toInt() != 0;
    prefs.putBool("otgUsb", otgAsUsb);
    sendJson(200, "{\"msg\":\"ok\"}");
}

void handleApiOtgGet() { sendJson(200, String("{\"usb\":") + (otgAsUsb ? "true" : "false") + "}"); }

void handleApiBle() {
    String body = server.arg("plain");
    int p = body.indexOf("\"on\":");
    int v = p >= 0 ? body.substring(p + 5).toInt() : 0;
    if (v) {
        if (!bleActive) {
            NimBLEDevice::init("ECMV-BLE");
            bleServer = NimBLEDevice::createServer();
            bleServer->setCallbacks(&bleConnCallback, false);
            auto svc = bleServer->createService("FFF0");
            bleCharRx = svc->createCharacteristic("FFF1", NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
            bleCharRx->setCallbacks(&bleRxCallback);
            bleCharTx = svc->createCharacteristic("FFF2", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
            svc->start();
            NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
            adv->addServiceUUID("FFF0");
            adv->start();
            bleActive = true;
        }
        sendJson(200, "{\"msg\":\"BLE 广播已启动\"}");
    } else {
        if (bleActive) { NimBLEDevice::deinit(true); bleActive = false; bleServer = nullptr; bleCharRx = nullptr; }
        sendJson(200, "{\"msg\":\"BLE 已停止\"}");
    }
}

void handleApiScriptGet() {
    sendJson(200, String("{\"script\":\"") + jsonEscape(String(scriptBuf)) + "\"}");
}

bool extractJsonString(const String &body, const char *key, String &value, size_t maxLength) {
    String marker = String("\"") + key + "\":\"";
    int start = body.indexOf(marker);
    if (start < 0) return false;
    start += marker.length();
    value = "";
    for (int i = start; i < (int)body.length(); i++) {
        char c = body[i];
        if (c == '"') return true;
        if (c == '\\') {
            if (++i >= (int)body.length()) return false;
            char escaped = body[i];
            if (escaped == 'n') c = '\n';
            else if (escaped == 'r') c = '\r';
            else if (escaped == 't') c = '\t';
            else if (escaped == '"' || escaped == '\\' || escaped == '/') c = escaped;
            else return false;
        }
        if (value.length() >= maxLength) return false;
        value += c;
    }
    return false;
}

bool validBleMac(const String &mac) {
    if (mac.length() == 0) return true;
    if (mac.length() != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = mac[i];
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (c != ':') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

void handleApiBleScriptGet() {
    String j = "{\"mac\":\"" + jsonEscape(bleMacFilter) +
               "\",\"connect\":\"" + jsonEscape(bleConnectScript) +
               "\",\"disconnect\":\"" + jsonEscape(bleDisconnectScript) + "\"}";
    sendJson(200, j);
}

void handleApiBleScriptSave() {
    String body = server.arg("plain");
    String mac, connectScript, disconnectScript;
    if (!extractJsonString(body, "mac", mac, 17) ||
        !extractJsonString(body, "connect", connectScript, 1024) ||
        !extractJsonString(body, "disconnect", disconnectScript, 1024)) {
        sendJson(400, "{\"msg\":\"脚本格式错误或内容超过 1024 字节\"}");
        return;
    }
    mac.trim();
    if (!validBleMac(mac)) {
        sendJson(400, "{\"msg\":\"MAC 地址格式应为 AA:BB:CC:DD:EE:FF，或留空\"}");
        return;
    }
    File connectFile = LittleFS.open("/ble_connect.txt", "w");
    if (!connectFile) { sendJson(500, "{\"msg\":\"无法保存连接脚本\"}"); return; }
    size_t connectWritten = connectFile.print(connectScript);
    connectFile.close();
    if (connectWritten != connectScript.length()) {
        sendJson(500, "{\"msg\":\"连接脚本写入不完整\"}");
        return;
    }
    File disconnectFile = LittleFS.open("/ble_disconnect.txt", "w");
    if (!disconnectFile) { sendJson(500, "{\"msg\":\"无法保存断开脚本\"}"); return; }
    size_t disconnectWritten = disconnectFile.print(disconnectScript);
    disconnectFile.close();
    if (disconnectWritten != disconnectScript.length()) {
        sendJson(500, "{\"msg\":\"断开脚本写入不完整\"}");
        return;
    }
    if (prefs.putString("bleMac", mac) == 0) {
        sendJson(500, "{\"msg\":\"MAC 筛选写入失败\"}");
        return;
    }
    bleConnectScript = connectScript;
    bleDisconnectScript = disconnectScript;
    bleMacFilter = mac;
    sendJson(200, "{\"msg\":\"蓝牙触发脚本已保存\"}");
}

void handleApiUsbStatus() {
    UsbTriggerEvent event;
    uint8_t count;
    portENTER_CRITICAL(&usbStatusMux);
    event = lastUsbEvent;
    count = usbDeviceCount;
    portEXIT_CRITICAL(&usbStatusMux);
    String error = jsonEscape(usbHostError);
    char json[384];
    snprintf(json, sizeof(json),
             "{\"ready\":%s,\"error\":\"%s\",\"count\":%u,\"last\":{\"connected\":%s,\"address\":%u,"
             "\"vid\":\"%04X\",\"pid\":\"%04X\",\"class\":\"0x%02X\","
             "\"subclass\":\"0x%02X\",\"protocol\":\"0x%02X\"}}",
             usbHostReady ? "true" : "false",
             error.c_str(),
             count,
             event.connected ? "true" : "false",
             event.address,
             event.vendorId,
             event.productId,
             event.deviceClass,
             event.deviceSubclass,
             event.deviceProtocol);
    sendJson(200, String(json));
}

void handleApiUsbScriptGet() {
    String j = "{\"connect\":\"" + jsonEscape(usbConnectScript) +
               "\",\"disconnect\":\"" + jsonEscape(usbDisconnectScript) + "\"}";
    sendJson(200, j);
}

void handleApiUsbScriptSave() {
    String body = server.arg("plain");
    String connectScript, disconnectScript;
    if (!extractJsonString(body, "connect", connectScript, 1024) ||
        !extractJsonString(body, "disconnect", disconnectScript, 1024)) {
        sendJson(400, "{\"msg\":\"脚本格式错误或内容超过 1024 字节\"}");
        return;
    }
    File connectFile = LittleFS.open("/usb_connect.txt", "w");
    if (!connectFile) { sendJson(500, "{\"msg\":\"无法保存 USB 插入脚本\"}"); return; }
    size_t connectWritten = connectFile.print(connectScript);
    connectFile.close();
    if (connectWritten != connectScript.length()) {
        sendJson(500, "{\"msg\":\"USB 插入脚本写入不完整\"}");
        return;
    }
    File disconnectFile = LittleFS.open("/usb_disconnect.txt", "w");
    if (!disconnectFile) { sendJson(500, "{\"msg\":\"无法保存 USB 拔出脚本\"}"); return; }
    size_t disconnectWritten = disconnectFile.print(disconnectScript);
    disconnectFile.close();
    if (disconnectWritten != disconnectScript.length()) {
        sendJson(500, "{\"msg\":\"USB 拔出脚本写入不完整\"}");
        return;
    }
    usbConnectScript = connectScript;
    usbDisconnectScript = disconnectScript;
    sendJson(200, "{\"msg\":\"USB 插拔脚本已保存\"}");
}

void handleApiScriptSave() {
    String body = server.arg("plain");
    int p = body.indexOf("\"script\":\"");
    if (p < 0) { sendJson(400, "{\"msg\":\"bad\"}"); return; }
    int s = p + 10;
    String out;
    for (int i = s; i < (int)body.length(); i++) {
        char c = body[i];
        if (c == '\\' && i + 1 < (int)body.length()) {
            char n = body[i + 1];
            if (n == 'n') { out += '\n'; i++; }
            else if (n == '"') { out += '"'; i++; }
            else if (n == '\\') { out += '\\'; i++; }
            else out += c;
        } else if (c == '"') break;
        else out += c;
    }
    if (out.length() >= SCRIPT_MAX) out = out.substring(0, SCRIPT_MAX - 1);
    strncpy(scriptBuf, out.c_str(), SCRIPT_MAX - 1);
    scriptBuf[SCRIPT_MAX - 1] = 0;
    File f = LittleFS.open("/script.txt", "w");
    if (f) { f.print(scriptBuf); f.close(); }
    sendJson(200, "{\"msg\":\"脚本已保存\"}");
}

bool isScriptGpioAllowed(int pin) {
    switch (pin) {
        case 1: case 2: case 3: case 14: case 21:
        case 41: case 42: case 47: case 48:
            return true;
        default:
            return false;
    }
}

int executeScriptText(const String &script) {
    String line;
    int executed = 0;
    for (int i = 0; i <= (int)script.length(); i++) {
        char c = i < (int)script.length() ? script[i] : 0;
        if (c == '\n' || c == '\r' || c == 0) {
            line.trim();
            if (line.length() == 0) { line = ""; continue; }
            if (line.startsWith("GPIO=")) {
                String rest = line.substring(5);
                int colon = rest.indexOf(':');
                if (colon > 0) {
                    int pin = rest.substring(0, colon).toInt();
                    int lvl = rest.substring(colon + 1).toInt();
                    if (isScriptGpioAllowed(pin) && (lvl == 0 || lvl == 1)) {
                        pinMode(pin, OUTPUT);
                        digitalWrite(pin, lvl ? HIGH : LOW);
                        executed++;
                    }
                }
            } else if (line.startsWith("DELAY=")) {
                int ms = line.substring(6).toInt();
                if (ms > 0 && ms < 30000) { delay(ms); executed++; }
            }
            line = "";
        } else line += c;
    }
    return executed;
}

void handleApiScriptRun() {
    int executed = executeScriptText(String(scriptBuf));
    if (recording) { recording = false; if (recFile) recFile.close(); }
    String msg = "执行 " + String(executed) + " 条指令";
    sendJson(200, "{\"msg\":\"" + msg + "\"}");
}

void processBleTriggers() {
    if (!bleTriggerQueue) return;
    BleTriggerEvent event;
    while (xQueueReceive(bleTriggerQueue, &event, 0) == pdTRUE) {
        String address(event.address);
        if (bleMacFilter.length() && !address.equalsIgnoreCase(bleMacFilter)) continue;
        const String &script = event.connected ? bleConnectScript : bleDisconnectScript;
        int executed = executeScriptText(script);
        Serial.printf("[ble] %s trigger from %s; %d commands\n",
                      event.connected ? "connect" : "disconnect",
                      event.address, executed);
    }
}

void processUsbTriggers() {
    if (!usbTriggerQueue) return;
    UsbTriggerEvent event;
    while (xQueueReceive(usbTriggerQueue, &event, 0) == pdTRUE) {
        const String &script = event.connected ? usbConnectScript : usbDisconnectScript;
        int executed = executeScriptText(script);
        Serial.printf("[usb] %s trigger VID=%04X PID=%04X class=%02X; %d commands\n",
                      event.connected ? "connect" : "disconnect",
                      event.vendorId, event.productId, event.deviceClass, executed);
    }
}

void handleApiDevRec() {
    if (!recording) {
        if (useSD && !sdMounted) { sendJson(503, "{\"msg\":\"未检测到板载 SD 卡\",\"rec\":false}"); return; }
        recPath = "/rec_" + String(millis()) + ".mjpeg";
        recFile = mediaStorage().open(recPath, "w");
        if (!recFile) { sendJson(500, "{\"msg\":\"无法创建文件\",\"rec\":false}"); return; }
        recording = true; recFrames = 0; recStartMs = millis();
        sendJson(200, "{\"msg\":\"开始保存 MJPEG\",\"rec\":true}");
    } else {
        recording = false;
        if (recFile) recFile.close();
        sendJson(200, "{\"msg\":\"已停止，保存 " + String(recFrames) + " 帧\",\"rec\":false}");
    }
}

void handleApiBrowserRecStart() {
    if (recOutput == 1) {
        sendJson(409, "{\"ok\":false,\"msg\":\"当前保存位置不包含 SD 卡\"}");
        return;
    }
    if (!sdMounted) {
        sendJson(503, "{\"ok\":false,\"msg\":\"未检测到板载 SD 卡\"}");
        return;
    }
    if (browserRecActive || browserRecFile) {
        sendJson(409, "{\"ok\":false,\"msg\":\"已有浏览器录像正在保存\"}");
        return;
    }
    String body = server.arg("plain");
    int p = body.indexOf("\"ext\":\"");
    if (p < 0) { sendJson(400, "{\"ok\":false,\"msg\":\"缺少录像格式\"}"); return; }
    p += 7;
    int end = body.indexOf('"', p);
    String ext = end < 0 ? "" : body.substring(p, end);
    if (ext != "mp4" && ext != "webm") {
        sendJson(400, "{\"ok\":false,\"msg\":\"不支持的录像格式\"}");
        return;
    }
    browserRecPath = "/ecmv_video_" + String(millis()) + "." + ext;
    browserRecFile = SD_MMC.open(browserRecPath, "w");
    if (!browserRecFile) {
        browserRecPath = "";
        sendJson(500, "{\"ok\":false,\"msg\":\"无法在 SD 卡创建录像文件\"}");
        return;
    }
    browserRecError = false;
    browserRecActive = true;
    sendJson(200, "{\"ok\":true}");
}

void handleApiBrowserRecFinish() {
    if (!browserRecActive || !browserRecFile) {
        sendJson(409, "{\"ok\":false,\"msg\":\"没有正在保存的浏览器录像\"}");
        return;
    }
    browserRecFile.flush();
    size_t fileSize = browserRecFile.size();
    browserRecFile.close();
    browserRecActive = false;
    if (browserRecError || fileSize == 0) {
        SD_MMC.remove(browserRecPath);
        browserRecPath = "";
        browserRecError = false;
        sendJson(500, "{\"ok\":false,\"msg\":\"录像写入不完整，文件已删除\"}");
        return;
    }
    String path = browserRecPath;
    browserRecPath = "";
    sendJson(200, "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\",\"size\":" + String(fileSize) + "}");
}

void handleApiBrowserRecAbort() {
    if (browserRecFile) browserRecFile.close();
    if (browserRecPath.length()) SD_MMC.remove(browserRecPath);
    browserRecPath = "";
    browserRecActive = false;
    browserRecError = false;
    sendJson(200, "{\"ok\":true}");
}

void handleApiAbout() {
    String j = "{";
    j += "\"chip\":\"" + String(ESP.getChipModel()) + "\"";
    j += ",\"cpu\":" + String(ESP.getCpuFreqMHz());
    j += ",\"flash\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) + "MB";
    j += ",\"psram\":\"" + String(psramFound() ? String(ESP.getPsramSize() / (1024 * 1024)) + "MB" : "无") + "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"uptime\":" + String(millis() / 1000);
    j += "}";
    sendJson(200, j);
}

// 启动
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[boot] setup started");

    prefsReady = prefs.begin("ecmv", false);
    if (!prefsReady) Serial.println("[prefs] failed to open NVS namespace ecmv");
    int settingsVersion = prefs.getInt("settingsVersion", 0);
    bool hasSavedResolution = prefs.isKey("res");
    currentRes = prefs.getInt("res", FRAMESIZE_SVGA);
    if (settingsVersion == 0) {
        if (hasSavedResolution) {
            switch (currentRes) {
                case 8: currentRes = FRAMESIZE_VGA; break;
                case 9: currentRes = FRAMESIZE_SVGA; break;
                case 10: currentRes = FRAMESIZE_XGA; break;
                case 11: currentRes = FRAMESIZE_HD; break;
            }
        }
        prefs.putInt("res", currentRes);
        prefs.putInt("settingsVersion", 1);
    }
    currentQual = prefs.getInt("qual", 12);
    currentTheme = prefs.getInt("theme", 0);
    currentBri = prefs.getInt("bri", 0);
    currentCon = prefs.getInt("con", 0);
    currentSat = prefs.getInt("sat", 0);
    currentWb = prefs.getInt("wb", 0);
    bool controlsLoaded = false;
    if (prefs.getBytesLength("camCtl") == sizeof(cameraExtendedControls)) {
        CameraExtendedControls savedControls = {};
        if (prefs.getBytes("camCtl", &savedControls, sizeof(savedControls)) == sizeof(savedControls) &&
            savedControls.sharpness >= -2 && savedControls.sharpness <= 2 &&
            savedControls.denoise <= 8 && savedControls.specialEffect <= 6 &&
            savedControls.aeLevel >= -2 && savedControls.aeLevel <= 2 &&
            savedControls.aecValue <= 1200 && savedControls.agcGain <= 30 &&
            savedControls.gainCeiling <= 6) {
            cameraExtendedControls = savedControls;
            controlsLoaded = true;
        }
    }
    if (!controlsLoaded &&
        prefs.putBytes("camCtl", &cameraExtendedControls, sizeof(cameraExtendedControls)) != sizeof(cameraExtendedControls)) {
        Serial.println("[prefs] failed to initialize camera controls");
    }
    recSegmentSec = prefs.getInt("recSeg", 30);
    recLoop = prefs.getBool("recLoop", true);
    bool savedTftEnabled = prefs.getBool("tftOn", true);
    tftFillScreen = prefs.getBool("tftFill", true);
    tftColorCorrection = prefs.getBool("tftColor", true);
    int savedRotation = prefs.getInt("tftRot", 1);
    tftRotation = savedRotation >= 0 && savedRotation <= 3 ? savedRotation : 1;
    int savedInterval = prefs.getInt("tftInt", 500);
    tftFrameIntervalMs = savedInterval == 250 || savedInterval == 500 || savedInterval == 750 ||
                         savedInterval == 1000 || savedInterval == 1500 ? savedInterval : 750;
    tftBacklightOn = prefs.getBool("tftBL", true);
    int savedProfile = prefs.getInt("hwProfile", HW_PROFILE_DISPLAY);
    int savedMode = prefs.getInt("uiMode", -1);
    if (savedMode < 0 || savedMode > 2) {
        savedMode = savedProfile == HW_PROFILE_STORAGE_USB ? 1 : 0;
        if (prefsReady) prefs.putInt("uiMode", savedMode);
    }
    uiMode = savedMode;
    hardwareProfile = uiMode == 1 ? HW_PROFILE_STORAGE_USB : HW_PROFILE_DISPLAY;
    networkEnabled = uiMode != 2;
    webEnabled = networkEnabled;
    tftEnabled = uiMode == 1 ? false : (uiMode == 2 ? true : savedTftEnabled);
    if (prefsReady && (prefs.getInt("hwProfile", -1) != hardwareProfile ||
                       prefs.getBool("tftOn", !tftEnabled) != tftEnabled)) {
        prefs.putInt("hwProfile", hardwareProfile);
        prefs.putBool("tftOn", tftEnabled);
    }
    button1Action = prefs.getInt("btn1Act", 1);
    button2Action = prefs.getInt("btn2Act", 2);
    if (button1Action < 0 || button1Action > 4) button1Action = 1;
    if (button2Action < 0 || button2Action > 4) button2Action = 2;
    bool legacyUseSD = prefs.getBool("useSD", false);
    recOutput = prefs.getInt("recOut", legacyUseSD ? 2 : 1);
    if (recOutput < 1 || recOutput > 3) recOutput = legacyUseSD ? 2 : 1;
    useSD = recOutput > 1;
    apSSID = prefs.getString("apSSID", "ECMV");
    apPass = prefs.getString("apPass", "ecmv123456");
    apEnabled = true;
    staSSID = prefs.getString("staSSID", "");
    staPass = prefs.getString("staPass", "");
    otgAsUsb = prefs.getBool("otgUsb", true);
    LittleFS.begin(true);
    if (hardwareProfile == HW_PROFILE_STORAGE_USB) {
        sdMounted = initSDCard();
    } else {
        sdMounted = false;
        if (recOutput > 1) {
            recOutput = 1;
            prefs.putInt("recOut", recOutput);
        }
        useSD = false;
        prefs.putBool("useSD", false);
        Serial.println("[board] display profile selected; SD and USB Host pins are reserved by TFT");
    }
    if (useSD && !sdMounted) {
        recOutput = 1;
        useSD = false;
        prefs.putInt("recOut", recOutput);
        prefs.putBool("useSD", false);
        Serial.println("[sd] selected card unavailable; using LittleFS");
    }
    File sf = LittleFS.open("/script.txt", "r");
    if (sf) { size_t n = sf.readBytes(scriptBuf, SCRIPT_MAX - 1); scriptBuf[n] = 0; sf.close(); }
    bleMacFilter = prefs.getString("bleMac", "");
    File bleConnectFile = LittleFS.open("/ble_connect.txt", "r");
    if (bleConnectFile) { bleConnectScript = bleConnectFile.readString(); bleConnectFile.close(); }
    File bleDisconnectFile = LittleFS.open("/ble_disconnect.txt", "r");
    if (bleDisconnectFile) { bleDisconnectScript = bleDisconnectFile.readString(); bleDisconnectFile.close(); }
    File usbConnectFile = LittleFS.open("/usb_connect.txt", "r");
    if (usbConnectFile) { usbConnectScript = usbConnectFile.readString(); usbConnectFile.close(); }
    File usbDisconnectFile = LittleFS.open("/usb_disconnect.txt", "r");
    if (usbDisconnectFile) { usbDisconnectScript = usbDisconnectFile.readString(); usbDisconnectFile.close(); }
    bleTriggerQueue = xQueueCreate(4, sizeof(BleTriggerEvent));
    if (!bleTriggerQueue) Serial.println("[ble] trigger queue allocation failed");

    if (hardwareProfile == HW_PROFILE_STORAGE_USB && otgAsUsb) {
        initUsbHost();
    } else if (hardwareProfile == HW_PROFILE_STORAGE_USB) {
        usbHostError = "USB Host 已关闭";
        pinMode(OTG_DP_PIN, OUTPUT);
        pinMode(OTG_DM_PIN, OUTPUT);
        digitalWrite(OTG_DP_PIN, LOW);
        digitalWrite(OTG_DM_PIN, LOW);
    } else {
        usbHostError = "当前 TFT 方案占用 GPIO19/20，USB Host 暂停";
    }

    Serial.println("[boot] initializing camera");
    bool cameraStarted = initCamera();
    if (!cameraStarted && currentRes != FRAMESIZE_SVGA) {
        Serial.println("[boot] requested resolution unavailable; retrying SVGA");
        currentRes = FRAMESIZE_SVGA;
        prefs.putInt("res", currentRes);
        cameraStarted = initCamera();
    }
    Serial.printf("[boot] camera %s\n", cameraStarted ? "started" : "failed");
    initializeCameraButtons();
    if (hardwareProfile == HW_PROFILE_DISPLAY && tftEnabled) {
        tftReady = initTftDisplay();
        if (!tftReady) Serial.println("[tft] initialization failed");
    }

    if (networkEnabled) {
        Serial.println("[boot] starting WiFi AP");
        WiFi.mode(WIFI_AP_STA);
        bool apStarted = WiFi.softAP(apSSID.c_str(), apPass.c_str());
        Serial.printf("AP %s: %s, IP: %s\n",
                      apStarted ? "started" : "failed",
                      apSSID.c_str(),
                      WiFi.softAPIP().toString().c_str());
        if (staSSID.length()) WiFi.begin(staSSID.c_str(), staPass.c_str());
    } else {
        WiFi.mode(WIFI_OFF);
        Serial.println("[boot] offline TFT mode; Wi-Fi disabled");
    }

    server.on("/", []() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        server.send_P(200, "text/html", INDEX_HTML);
    });
    server.on("/frame", handleFrame);
    server.on("/stream", handleStream);
    server.on("/download", handleDownload);
    server.on("/media", handleMediaPreview);
    server.on("/api/media-upload", HTTP_POST, handleMediaUploadComplete, handleMediaUploadChunk);
    server.on("/api/status", handleApiStatus);
    server.on("/api/net", handleApiNet);
    server.on("/api/files", handleApiFiles);
    server.on("/api/storage", handleApiStorageGet);
    server.on("/api/storage", HTTP_POST, handleApiStorageSet);
    server.on("/api/board-config", handleApiBoardConfigGet);
    server.on("/api/board-config", HTTP_POST, handleApiBoardConfigSet);
    server.on("/api/button-config", HTTP_POST, handleApiButtonConfigSet);
    server.on("/api/tft-config", HTTP_POST, handleApiTftConfigSet);
    server.on("/api/tft-backlight", HTTP_POST, handleApiTftBacklightToggle);
    server.on("/api/delete", HTTP_POST, handleApiDelete);
    server.on("/api/deleteall", HTTP_POST, handleApiDeleteAll);
    server.on("/api/cam", HTTP_POST, handleApiCam);
    server.on("/api/rec", HTTP_POST, handleApiRec);
    server.on("/api/ap", HTTP_POST, handleApiAp);
    server.on("/api/sta", HTTP_POST, handleApiSta);
    server.on("/api/sensor", HTTP_POST, handleApiSensor);
    server.on("/api/sensor/get", handleApiSensorGet);
    server.on("/api/otg", HTTP_POST, handleApiOtg);
    server.on("/api/otg/get", handleApiOtgGet);
    server.on("/api/ble", HTTP_POST, handleApiBle);
    server.on("/api/ble-script", handleApiBleScriptGet);
    server.on("/api/ble-script", HTTP_POST, handleApiBleScriptSave);
    server.on("/api/usb-status", handleApiUsbStatus);
    server.on("/api/usb-script", handleApiUsbScriptGet);
    server.on("/api/usb-script", HTTP_POST, handleApiUsbScriptSave);
    server.on("/api/script", handleApiScriptGet);
    server.on("/api/script/save", HTTP_POST, handleApiScriptSave);
    server.on("/api/script/run", HTTP_POST, handleApiScriptRun);
    server.on("/api/devrec", HTTP_POST, handleApiDevRec);
    server.on("/api/browser-rec/start", HTTP_POST, handleApiBrowserRecStart);
    server.on("/api/browser-rec/finish", HTTP_POST, handleApiBrowserRecFinish);
    server.on("/api/browser-rec/abort", HTTP_POST, handleApiBrowserRecAbort);
    server.on("/api/about", handleApiAbout);
    if (webEnabled) {
        server.begin();
        serverStarted = true;
        Serial.println("[boot] web server started");
    } else {
        Serial.println("[boot] web server disabled by run mode");
    }
}

void loop() {
    if (webEnabled) server.handleClient();
    processBleTriggers();
    processUsbTriggers();
    processRecoveryButtonCombo();
    processCameraButtons();
    processDeviceRecording();
    if (hardwareProfile == HW_PROFILE_DISPLAY) updateTftPreview();
    if (restartPending && (int32_t)(millis() - restartAt) >= 0) {
        Serial.println("[board] restarting to apply hardware profile");
        delay(80);
        ESP.restart();
    }
    if (bleActive && bleServer && !bleServer->getConnectedCount()) {
        // 保持广播
    }
    delay(1);
}
