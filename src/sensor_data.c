#include "sensor_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static sensor_data_t s_data = {0};
static SemaphoreHandle_t s_mutex = NULL;

/*
 * sensor_data_init
 * 概要: 共有センサーデータの保存領域と排他制御用の mutex を準備する。
 * 役割: 複数タスクからデータを安全に更新・取得できるようにする。
 */
void sensor_data_init(void) {
    s_mutex = xSemaphoreCreateMutex();
}

/*
 * sensor_data_set
 * 概要: 最新のセンサーデータを内部変数に保存する。
 * 引数:
 *   data - 保存したいセンサーデータのアドレス
 * 備考: mutex を使ってデータ競合を防ぐ。
 */
void sensor_data_set(const sensor_data_t *data) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data = *data;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/*
 * sensor_data_get
 * 概要: 保存済みのセンサーデータを読み出して呼び出し元へ返す。
 * 引数:
 *   data - 読み出したデータを書き込むバッファのアドレス
 */
void sensor_data_get(sensor_data_t *data) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    *data = s_data;
    if (s_mutex) xSemaphoreGive(s_mutex);
}
