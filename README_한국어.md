한국어
개요
ECMV는 ESP32-S3 카메라 보드용 단일 파일 펌웨어 프로젝트입니다. 카메라 모니터링, 웹 설정 센터, Wi-Fi AP/STA, BLE 프로비저닝, USB Host 감지, SD 카드 및 LittleFS 저장, ST7789 TFT 로컬 미리보기, 물리 버튼, 간단한 GPIO 스크립트 엔진을 통합합니다.

주요 기능
브라우저에서 JPEG 카메라 화면 실시간 보기

카메라, 센서, 네트워크, 저장, TFT, 버튼, 스크립트를 위한 웹 설정 센터

Wi-Fi AP 핫스팟 및 STA 라우터 연결

BLE 프로비저닝 및 BLE 연결/해제 트리거 스크립트

USB Host 장치 연결/해제 감지 및 트리거 스크립트

브라우저 스냅샷 다운로드 및 브라우저 녹화 다운로드

장치 측 MJPEG 녹화

SD 카드 및 LittleFS 파일 관리

ST7789 240x320 TFT 로컬 미리보기 및 상태 페이지

구성 가능한 물리 버튼 2개

TFT 전용 모드에서 두 버튼을 5초간 길게 누르면 웹과 Wi-Fi 복구

하드웨어 및 고정 핀
카메라 핀:

신호	GPIO
XCLK	15
SIOD	4
SIOC	5
D0-D7	11, 9, 8, 10, 12, 18, 17, 16
VSYNC	6
HREF	7
PCLK	13
TFT ST7789 핀:

신호	GPIO
SCLK	19
MOSI	20
CS	45
DC	47
RST	21
BL	38
기타 핀:

기능	GPIO
버튼 1	0
버튼 2	46
USB OTG D+	20
USB OTG D-	19
SD_MMC 1-bit CLK	39
SD_MMC 1-bit CMD	38
SD_MMC 1-bit D0	40
참고: TFT와 USB OTG는 GPIO19/20을 공유합니다. 기본 "웹 + TFT" 모드에서는 SD와 USB Host가 중지됩니다. SD 또는 USB Host를 사용하려면 "웹 전용" 모드로 전환하고 재시작하세요.

실행 모드
웹 + TFT: 기본 모드. Wi-Fi, 웹, TFT를 활성화합니다. 핀 다중화로 SD와 USB Host는 중지됩니다.

웹 전용: TFT를 끄고 SD 카드와 USB Host 관련 기능을 활성화합니다. 재시작 필요.

TFT 전용: Wi-Fi와 웹을 끄고 로컬 TFT 미리보기만 사용합니다. 재시작 필요. 두 버튼을 5초간 길게 누르면 웹과 Wi-Fi를 복구합니다.

빌드 의존성
Arduino IDE 또는 PlatformIO로 ESP32-S3용 빌드를 사용하세요. PSRAM을 활성화해야 합니다.

주요 의존성:

esp32-camera

WiFi 및 WebServer

LittleFS 및 SD_MMC

NimBLE-Arduino

Adafruit GFX

TJpg_Decoder

ESP-IDF USB Host 관련 구성 요소

사용 방법
펌웨어 플래시 후 장치는 Wi-Fi 핫스팟을 시작합니다. 기본 SSID는 ECMV, 기본 비밀번호는 ecmv123456입니다.

핫스팟에 연결한 뒤 장치 웹 페이지를 엽니다.

설정 센터에서 해상도, JPEG 품질, 센서 제어, TFT, 저장 위치, 네트워크를 구성합니다.

BLE 도구로 ECMV-BLE의 RX 특성에 JSON을 작성하여 프로비저닝할 수 있습니다:
{"ssid":"MyWiFi","pass":"12345678"}

파일 페이지에서 미리보기, 다운로드, 삭제를 지원합니다.

OTG 및 스크립트 페이지에서 USB Host 상태, USB 연결/해제 스크립트, BLE 트리거 스크립트, 간단한 GPIO 스크립트를 구성합니다.

스크립트 형식
한 줄에 하나의 명령:

GPIO=2:1
DELAY=500
GPIO=2:0
스크립트에서 허용되는 GPIO: 1, 2, 3, 14, 21, 41, 42, 47, 48. 카메라, SD, USB, PSRAM, UART 관련 핀은 사용할 수 없습니다.

주의 사항
핀 다중화가 많으므로 실행 모드 변경 후 재시작해야 합니다.

USB Host 모드에서는 인터페이스에 5V VBUS가 필요합니다.

이 프로젝트는 연결된 휴대폰이나 컴퓨터로 키 입력을 보내지 않으며 호스트 명령도 실행하지 않습니다.

지원되는 센서 제어 항목은 센서 모델에 따라 다릅니다. 지원되지 않는 항목은 건너뜁니다.

SD 카드와 TFT 핀 할당은 보드에 따라 다릅니다. 사용 전에 회로도를 확인하세요.

