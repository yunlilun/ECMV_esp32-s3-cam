日本語
概要
ECMV は ESP32-S3 カメラボード向けの単一ファイルファームウェアです。カメラ監視、Web 設定センター、Wi-Fi AP/STA、BLE プロビジョニング、USB Host 検出、SD カードと LittleFS 保存、ST7789 TFT ローカルプレビュー、物理ボタン、簡易 GPIO スクリプトエンジンを統合しています。

主な機能
ブラウザで JPEG カメラ映像をリアルタイム表示

カメラ、センサー、ネットワーク、保存、TFT、ボタン、スクリプトの Web 設定

Wi-Fi AP ホットスポットと STA ルーター接続

BLE プロビジョニングと BLE 接続/切断トリガースクリプト

USB Host デバイス挿抜検出とトリガースクリプト

ブラウザでのスナップショット保存と録画保存

デバイス側 MJPEG 録画

SD カードと LittleFS のファイル管理

ST7789 240x320 TFT ローカルプレビューとステータス画面

2 つの設定可能な物理ボタン

TFT のみモードでは、両方のボタンを 5 秒長押しすると Web と Wi-Fi を復元

ハードウェアと固定ピン
カメラピン：

信号	GPIO
XCLK	15
SIOD	4
SIOC	5
D0-D7	11, 9, 8, 10, 12, 18, 17, 16
VSYNC	6
HREF	7
PCLK	13
TFT ST7789 ピン：

信号	GPIO
SCLK	19
MOSI	20
CS	45
DC	47
RST	21
BL	38
その他：

機能	GPIO
ボタン1	0
ボタン2	46
USB OTG D+	20
USB OTG D-	19
SD_MMC 1-bit CLK	39
SD_MMC 1-bit CMD	38
SD_MMC 1-bit D0	40
注意：TFT と USB OTG は GPIO19/20 を共用します。既定の「Web + TFT」モードでは SD と USB Host は停止します。SD または USB Host を使う場合は「Web のみ」モードに切り替えて再起動してください。

動作モード
Web + TFT：既定モード。Wi-Fi、Web、TFT を有効化。ピン共用のため SD と USB Host は停止。

Web のみ：TFT を無効化し、SD カードと USB Host 関連機能を有効化。再起動が必要。

TFT のみ：Wi-Fi と Web を無効化し、TFT プレビューのみ。再起動が必要。両ボタンを 5 秒長押しすると Web と Wi-Fi を復元。

ビルド依存
Arduino IDE または PlatformIO で ESP32-S3 向けにビルドしてください。PSRAM を有効にしてください。

主な依存：

esp32-camera

WiFi と WebServer

LittleFS と SD_MMC

NimBLE-Arduino

Adafruit GFX

TJpg_Decoder

ESP-IDF USB Host 関連コンポーネント

使用方法
書き込み後、デバイスは Wi-Fi ホットスポットを開始します。既定 SSID は ECMV、既定パスワードは ecmv123456。

ホットスポットに接続し、デバイスの Web ページを開きます。

設定センターで解像度、JPEG 画質、センサー制御、TFT、保存先、ネットワークを設定します。

BLE ツールで ECMV-BLE の RX 特性に JSON を書き込みプロビジョニングできます：
{"ssid":"MyWiFi","pass":"12345678"}

ファイルページでプレビュー、ダウンロード、削除ができます。

OTG とスクリプトページで USB Host 状態、USB 挿抜スクリプト、BLE トリガースクリプト、簡易 GPIO スクリプトを設定できます。

スクリプト形式
1 行 1 コマンド：

GPIO=2:1
DELAY=500
GPIO=2:0
スクリプトで使用可能な GPIO：1, 2, 3, 14, 21, 41, 42, 47, 48。カメラ、SD、USB、PSRAM、UART 関連ピンは使用できません。

注意事項
ピン共用が多いため、動作モード変更後は再起動が必要です。

USB Host モードではインターフェースに 5V VBUS が必要です。

本プロジェクトは接続されたスマートフォンや PC にキー入力を送信せず、ホストコマンドも実行しません。

センサー制御の対応可否はセンサーモデルにより異なります。未対応項目はスキップされます。

SD カードと TFT のピン割り当てはボードにより異なります。使用前に回路図を確認してください。
