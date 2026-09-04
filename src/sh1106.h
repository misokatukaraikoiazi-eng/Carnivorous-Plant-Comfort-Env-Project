#pragma once

#include "driver/gpio.h"
#include "sensor_data.h"
#include <stdbool.h>
#include <stdint.h>

#define SH1106_I2C_SDA_PIN GPIO_NUM_21
#define SH1106_I2C_SCL_PIN GPIO_NUM_22
#define SH1106_I2C_ADDR    0x3C  /* 8bit表記の 0x78 は 7bit アドレスで 0x3C (0x78 >> 1) */

/*
 * sh1106_init
 * 概要: SH1106 OLED ディスプレイ (I2C) を初期化する。
 * 引数:
 *   sda_pin  - SDA ピン (デフォルト: GPIO_NUM_21)
 *   scl_pin  - SCL ピン (デフォルト: GPIO_NUM_22)
 *   i2c_addr - I2C アドレス (デフォルト: 0x3C)
 * 戻り値:
 *   初期化成功時 true、失敗時 false
 */
bool sh1106_init(gpio_num_t sda_pin, gpio_num_t scl_pin, uint8_t i2c_addr);

/*
 * sh1106_clear
 * 概要: ディスプレイバッファをクリア（全消灯）する。
 */
void sh1106_clear(void);

/*
 * sh1106_draw_string
 * 概要: 指定した行（0〜7）とX座標（0〜127）に文字列を描画する。
 * 引数:
 *   row - ページ行 (0〜7)
 *   x   - X座標ピクセル (0〜127)
 *   str - 表示する文字列 (ASCII)
 */
void sh1106_draw_string(uint8_t row, uint8_t x, const char *str);

/*
 * sh1106_update
 * 概要: ディスプレイバッファを SH1106 画面へ一括転送する。
 */
void sh1106_update(void);

/*
 * sh1106_display_sensor_data
 * 概要: 気温・湿度・水温・土壌（DRY/OK）・ファン・ポンプ等の状態を表示する。
 * 引数:
 *   data       - 最新のセンサーデータ
 *   is_ac_mode - AC電源モードか否か
 */
void sh1106_display_sensor_data(const sensor_data_t *data, bool is_ac_mode);
