#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

/*
 * DHT22 センサーの読み取り結果を保持する構造体。
 */
typedef struct {
    float temperature_c;    /* 温度[°C] */
    float humidity_percent; /* 相対湿度[%] */
    bool valid;             /* データが正常に取得できたかどうか */
} dht22_reading_t;

/*
 * dht22_init
 * 概要: DHT22 と接続する GPIO ピンを初期化する。
 * 引数:
 *   pin - DHT22 を接続した GPIO 番号
 */
void dht22_init(gpio_num_t pin);

/*
 * dht22_read
 * 概要: DHT22 から温度と湿度を1回読み取る。
 * 引数:
 *   pin - DHT22 を接続した GPIO 番号
 * 戻り値:
 *   読み取った温度・湿度・妥当性を含む構造体
 */
dht22_reading_t dht22_read(gpio_num_t pin);
