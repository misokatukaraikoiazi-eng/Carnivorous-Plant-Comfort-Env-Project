#include "dht22.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/*
 * wait_level
 * 概要: 指定した GPIO が target の状態になるまで待ち、経過時間を返す。
 * 引数:
 *   pin        - 調べる GPIO 番号
 *   level      - 待ちたいレベル（0/1）
 *   timeout_us - タイムアウト時間[us]
 * 戻り値:
 *   経過時間[us]。タイムアウトした場合は -1
 */
static int wait_level(gpio_num_t pin, int level, int timeout_us) {
    int elapsed = 0;
    while (gpio_get_level(pin) != level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        esp_rom_delay_us(1);
        elapsed++;
    }
    return elapsed;
}

/*
 * dht22_init
 * 概要: DHT22 の通信線をオープンドレイン出力として初期化する。
 * 役割: 高レベル時に pull-up を有効にして、センサーとの通信に備える。
 * 引数:
 *   pin - DHT22 を接続した GPIO 番号
 */
void dht22_init(gpio_num_t pin) {
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
 * dht22_read
 * 概要: DHT22 から温度と湿度を1回読み取る。
 * 役割: 初期化信号を送信し、40bit のデータとチェックサムを受信して解釈する。
 * 引数:
 *   pin - DHT22 を接続した GPIO 番号
 * 戻り値:
 *   温度・湿度・妥当性を含む dht22_reading_t
 */
dht22_reading_t dht22_read(gpio_num_t pin) {
    dht22_reading_t result = { .temperature_c = 0, .humidity_percent = 0, .valid = false };
    uint8_t data[5] = {0};

    /* Start signal: host pulls the line low, then releases it. */
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(pin, 1);
    esp_rom_delay_us(30);

    /* Sensor response: low ~80us then high ~80us. */
    if (wait_level(pin, 0, 100) < 0) return result;
    if (wait_level(pin, 1, 100) < 0) return result;
    if (wait_level(pin, 0, 100) < 0) return result;

    for (int i = 0; i < 40; i++) {
        if (wait_level(pin, 1, 100) < 0) return result;
        int high_us = wait_level(pin, 0, 100);
        if (high_us < 0) return result;
        uint8_t bit = (high_us > 40) ? 1 : 0; /* ~26-28us = 0, ~70us = 1 */
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | bit);
    }

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return result;
    }

    result.humidity_percent = ((data[0] << 8) | data[1]) / 10.0f;
    int16_t raw_temp = (int16_t)(((data[2] & 0x7F) << 8) | data[3]);
    float temp = raw_temp / 10.0f;
    if (data[2] & 0x80) {
        temp = -temp;
    }
    result.temperature_c = temp;
    result.valid = true;
    return result;
}
