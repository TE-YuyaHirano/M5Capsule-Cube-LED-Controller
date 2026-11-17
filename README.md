# M5Capsule Cube LED Controller

M5Stack Capsule (ESP32-S3) から、Cube用Edge LED基板を制御するArduinoファームウェアです。

## 機能

- Edge LED 16灯の制御
- Forward / Reverse
- Knight Rider
- Right / Left Wink
- Red Patrol
- Edge All On
- 3種類のLED間引きパターン
- パターンごとの輝度・スピード調整
- USB Serial経由のJSON Linesコマンド
- WAKEボタンによるプリセット切り替え

Top Matrix (DIN2) は現在のプロトタイプ検証では常時消灯します。

## 必要環境

- Arduino IDE 2.x または Arduino CLI
- ESP32 Arduino Core
- Adafruit NeoPixel
- Board: `M5Stack Capsule`

## ピン設定

| Signal | GPIO |
| --- | ---: |
| DIN1 / Edge LEDs | 13 |
| DIN2 / Top Matrix | 15 |
| WAKE button | 42 |

## ビルド

```sh
arduino-cli compile \
  --fqbn esp32:esp32:m5stack_capsule \
  M5Capsule_Cube_LED_WebController
```

## 書き込み

`PORT` をM5 Capsuleのシリアルポートに置き換えます。

```sh
arduino-cli upload \
  -p PORT \
  --fqbn esp32:esp32:m5stack_capsule \
  M5Capsule_Cube_LED_WebController
```

## Serialコマンド

115200 bps、UTF-8のJSON Lines形式です。

```json
{"cmd":"preset","id":1,"brightness":40,"layout":0,"speed":6}
{"cmd":"speed","value":8}
{"cmd":"brightness","value":30}
{"cmd":"layout","value":2}
{"cmd":"pause","value":1}
```

`layout` は次の間引き設定です。

- `0`: 1辺5個すべて
- `1`: 1・2・4・5
- `2`: 1・3・5

