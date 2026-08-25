#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

/*
 * ds18b20_init
 * 概要: DS18B20 を接続した GPIO ピンを初期化する。
 * 引数:
 *   pin - DS18B20 を接続した GPIO 番号
 */
void ds18b20_init(gpio_num_t pin);

/*
 * ds18b20_read_temp_c
 * 概要: DS18B20 から水温を読み取る。
 * 引数:
 *   pin       - DS18B20 を接続した GPIO 番号
 *   out_temp_c - 読み取った水温[°C]を書き込むポインタ
 * 戻り値:
 *   読み取りに成功した場合は true、失敗時は false
 */
bool ds18b20_read_temp_c(gpio_num_t pin, float *out_temp_c);
