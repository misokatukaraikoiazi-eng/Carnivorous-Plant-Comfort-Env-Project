#pragma once

#include <stdbool.h>

/* ---------------- Sensor Data Structure ---------------- */
/*
 * センサー値をまとめて保持する構造体。
 * 監視画面や制御ロジックで共通の状態として参照する。
 */
typedef struct {
    float air_temp_c;        /* 室内の空気温度[°C] */
    float humidity_percent;  /* 相対湿度[%] */
    float water_temp_c;      /* 水温[°C] */
    int soil_raw;            /* 土壌センサーの生ADC値（0〜4095程度） */
    bool soil_dry;           /* 土が乾燥しているかどうか */
    bool pump_on;            /* ポンプの動作状態 */
    bool fan_on;             /* ファンの動作状態 */
} sensor_data_t;

/*
 * sensor_data_init
 * 概要: センサーデータ管理用の内部状態を初期化する。
 * 例: mutex の作成など、共有データの排他制御を準備する。
 */
void sensor_data_init(void);

/*
 * sensor_data_set
 * 概要: 最新のセンサーデータを内部保存領域に格納する。
 * 引数:
 *   data - 保存したいセンサーデータのポインタ
 */
void sensor_data_set(const sensor_data_t *data);

/*
 * sensor_data_get
 * 概要: 内部に保存されているセンサーデータを取得する。
 * 引数:
 *   data - 取得したデータを書き込む先のポインタ
 */
void sensor_data_get(sensor_data_t *data);
