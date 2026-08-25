#include "ds18b20.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * ds18b20_init
 * 概要: DS18B20 のデータ線を開放ドレイン I/O として初期化する。
 * 役割: 1-Wire 通信の基準レベルを整え、センサとやり取りできる状態にする。
 * 引数:
 *   pin - DS18B20 を接続した GPIO 番号
 */
void ds18b20_init(gpio_num_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 1);
}

/*
 * onewire_reset
 * 概要: 1-Wire バスのリセット信号を送り、センサーの存在確認を行う。
 * 戻り値:
 *   センサーが存在すれば true、見つからなければ false
 */
static bool onewire_reset(gpio_num_t pin) {
    gpio_set_level(pin, 0);
    esp_rom_delay_us(480);
    gpio_set_level(pin, 1);
    esp_rom_delay_us(70);
    bool presence = (gpio_get_level(pin) == 0);
    esp_rom_delay_us(410);
    return presence;
}

/*
 * onewire_write_bit
 * 概要: 1-Wire の 1 ビットを書き込む。
 * 引数:
 *   pin - 通信対象の GPIO
 *   bit - 書き込むビット（0 または 1）
 */
static void onewire_write_bit(gpio_num_t pin, int bit) {
    gpio_set_level(pin, 0);
    esp_rom_delay_us(bit ? 6 : 60);
    gpio_set_level(pin, 1);
    esp_rom_delay_us(bit ? 64 : 10);
}

/*
 * onewire_read_bit
 * 概要: 1-Wire から 1 ビットを読み取る。
 * 戻り値:
 *   受信したビット（0 または 1）
 */
static int onewire_read_bit(gpio_num_t pin) {
    gpio_set_level(pin, 0);
    esp_rom_delay_us(3);
    gpio_set_level(pin, 1);
    esp_rom_delay_us(10);
    int bit = gpio_get_level(pin);
    esp_rom_delay_us(50);
    return bit;
}

/*
 * onewire_write_byte
 * 概要: 1 バイト分のコマンドを順番に書き込む。
 * 引数:
 *   pin  - 通信対象の GPIO
 *   byte - 書き込む 8bit データ
 */
static void onewire_write_byte(gpio_num_t pin, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

/*
 * onewire_read_byte
 * 概要: 1-Wire から 1 バイトを読み取る。
 * 戻り値:
 *   読み取った 8bit データ
 */
static uint8_t onewire_read_byte(gpio_num_t pin) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (uint8_t)(byte | (onewire_read_bit(pin) << i));
    }
    return byte;
}

/*
 * ds18b20_read_temp_c
 * 概要: DS18B20 から水温を取得し、摂氏温度として返す。
 *引数:
 *   pin       - DS18B20 を接続した GPIO 番号
 *   out_temp_c - 読み取った温度[°C]を書き込むポインタ
 * 戻り値:
 *   成功時 true、通信失敗時 false
 */
bool ds18b20_read_temp_c(gpio_num_t pin, float *out_temp_c) {
    if (!onewire_reset(pin)) {
        return false;
    }
    onewire_write_byte(pin, 0xCC); /* Skip ROM (single device on the bus) */
    onewire_write_byte(pin, 0x44); /* Convert T */
    vTaskDelay(pdMS_TO_TICKS(750)); /* max conversion time at 12-bit resolution */

    if (!onewire_reset(pin)) {
        return false;
    }
    onewire_write_byte(pin, 0xCC);
    onewire_write_byte(pin, 0xBE); /* Read Scratchpad */

    uint8_t lsb = onewire_read_byte(pin);
    uint8_t msb = onewire_read_byte(pin);

    int16_t raw = (int16_t)((msb << 8) | lsb);
    *out_temp_c = raw / 16.0f;
    return true;
}
