# Carnivorous-Plant-Comfort-Env-Project

ESP32 (ESP-WROOM-32) を使用した食虫植物テラリウムの自動制御プロジェクト。
PlatformIO + ESP-IDF (C言語) で実装。

## ピン配置

| GPIO | 用途 |
|---|---|
| 34 | 静電容量式土壌水分センサー (ADC) |
| 26 | DHT22 温湿度センサー (GPIO26) |
| 25 | DS18B20 防水水温センサー (OneWire) |
| 18 | MOSFET1: 水中ポンプ |
| 19 | MOSFET2: 冷却ファン |
| 23 | 赤色警告LED |
| 27 | 動作モード切替スイッチ (LOW=ACアダプター常時稼働 / HIGH=モバイルバッテリー省電力) |
| 21 | SH1106 OLED ディスプレイ (I2C SDA) |
| 22 | SH1106 OLED ディスプレイ (I2C SCL) |
| 32 | MENUボタン（設定項目の選択） |
| 33 | UPボタン（設定値を増加） |
| 16 | DOWNボタン（設定値を減少） |
| 17 | SAVEボタン（NVSへ保存して監視画面へ戻る） |

ボタンは各GPIOとGNDの間に接続します。ファームウェアの内部プルアップを使用するため、未押下はHIGH、押下時はLOWです。

## OLED設定操作

- `MENU` を押すと設定画面へ入り、水温上限、復帰温度、土壌乾燥しきい値を順に選択します。
- `UP` / `DOWN` で選択中の値を変更します。値はその場で制御ロジックとSH1106画面に反映されます。
- `SAVE` で変更値をNVSへ保存し、通常の監視画面へ戻ります。

## ファイル構成

```
platformio.ini          # PlatformIO設定 (env:esp32dev, framework=espidf)
Makefile                 # ビルド/書き込み用コマンドのラッパー
push.sh                  # git add/commit/push をまとめて行うスクリプト
wokwi.toml               # Wokwiシミュレーター用設定
src/
  main.c                 # app_main、モード判定、センサー/アクチュエーター制御ループ
  dht22.c / dht22.h       # DHT22 ビットバンギング読み取り
  ds18b20.c / ds18b20.h   # OneWire + DS18B20 読み取り
  sh1106.c / sh1106.h     # SH1106 I2C OLED ディスプレイ表示制御
  wifi_ap.c / wifi_ap.h   # Wi-Fi SoftAP 起動/停止
  webserver.c / webserver.h # HTTPダッシュボード (HTML/JSON)
  sensor_data.c / sensor_data.h # ミューテックス保護された共有センサー状態
```

## ビルド

```bash
make build
```

または直接PlatformIOを使う場合:

```bash
pio run -e esp32dev
```

成功すると以下に成果物が生成される:

- `.pio/build/esp32dev/firmware.bin`
- `.pio/build/esp32dev/bootloader.bin`
- `.pio/build/esp32dev/partitions.bin`

3つをまとめた単一の書き込みイメージが欲しい場合:

```bash
make merge
```

→ `build/terrarium-flash.bin` が生成される（オフセット0x0に書き込み用）。

## 書き込み

ESP32をUSB接続した状態で:

```bash
make upload
```

または直接:

```bash
pio run -e esp32dev -t upload
```

## プッシュ

変更をコミットしてリポジトリへプッシュする場合:

```bash
./push.sh "コミットメッセージ"
```

引数を省略すると `"update"` というメッセージでコミットされる。
