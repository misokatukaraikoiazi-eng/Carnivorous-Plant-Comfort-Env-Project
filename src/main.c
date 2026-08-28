#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#include "dht22.h"
#include "ds18b20.h"
#include "sensor_data.h"
#include "webserver.h"
#include "wifi_ap.h"

/* ---------------- Pin Assignments ---------------- */
#define PIN_SOIL_MOISTURE  ADC_CHANNEL_6    /* GPIO34 on ADC_UNIT_1: capacitive soil moisture sensor v1.2 */
#define PIN_DHT            GPIO_NUM_26      /* DHT22 temperature/humidity sensor */
#define PIN_WATER_TEMP     GPIO_NUM_25      /* DS18B20 waterproof water temp sensor */
#define PIN_PUMP           GPIO_NUM_18      /* MOSFET1 -> submersible pump */
#define PIN_FAN            GPIO_NUM_19      /* MOSFET2 -> cooling fan */
#define PIN_LED            GPIO_NUM_23      /* red warning LED */
#define PIN_MODE_SWITCH    GPIO_NUM_27      /* operation mode switch (pullup) */

/* ---------------- Thresholds / Calibration ---------------- */
#define WATER_TEMP_HIGH       28.0f   /* C: overheat -> stop pump, start fan */
#define WATER_TEMP_SAFE       26.5f   /* C: safe -> stop fan, resume pump */
#define SOIL_DRY_THRESHOLD    2200    /* ADC raw value (0-4095); calibrate for actual sensor */
#define LED_BLINK_INTERVAL_MS 300

/* Pump intermittent cycle (AC adapter mode) */
#define PUMP_ON_MS  (5UL  * 60UL * 1000UL)
#define PUMP_OFF_MS (30UL * 60UL * 1000UL)

/* Mobile battery mode timings */
#define AP_GRACE_PERIOD_MS (15UL * 1000UL)
#define FAN_COOLING_MS     (5UL  * 60UL * 1000UL)
#define DEEP_SLEEP_US      (30ULL * 60ULL * 1000000ULL)

#define WIFI_SSID     "Terrarium-Monitor"
#define WIFI_PASSWORD "password123"

/* Set to 0 after confirming the dashboard update path. */
#define USE_SIMULATED_SENSOR_VALUES 1
#define SIMULATED_AIR_TEMP_C        24.8f
#define SIMULATED_HUMIDITY_PERCENT  68.0f
#define SIMULATED_WATER_TEMP_C      23.7f
#define SIMULATED_SOIL_RAW          2800

static const char *TAG = "terrarium";

static adc_oneshot_unit_handle_t s_adc_handle;
static bool g_water_overheat = false;
static bool g_pump_cycle_phase_on = false;
static int64_t g_pump_cycle_start_ms = 0;
static bool g_led_state = false;
static int64_t g_last_led_toggle_ms = 0;
/*
 * now_ms
 * 概要: ESP32 の起動時刻からの経過時間をミリ秒単位で取得する。
 * 戻り値: ブート後の経過時間[ms]
 */
static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

/*
 * gpio_outputs_init
 * 概要: ポンプ、ファン、LED、モードスイッチの GPIO を初期化する。
 * 役割:
 *   - ポンプ・ファン・LED は出力として設定
 *   - モードスイッチは入力として設定し、プルアップを有効化
 */
static void gpio_outputs_init(void) {
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_PUMP) | (1ULL << PIN_FAN) | (1ULL << PIN_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(PIN_PUMP, 0);
    gpio_set_level(PIN_FAN, 0);
    gpio_set_level(PIN_LED, 0);

    gpio_config_t sw_cfg = {
        .pin_bit_mask = (1ULL << PIN_MODE_SWITCH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_cfg);
}

/*
 * adc_init
 * 概要: 土壌湿度計の ADC を初期化する。
 * 役割:
 *   - ADCユニットを生成
 *   - 乾燥度判定用の土壌センサー入力を有効化する
 */
static void adc_init(void) {
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, PIN_SOIL_MOISTURE, &chan_config));
}

/*
 * read_all_sensors
 * 概要: DHT22, DS18B20, 土壌センサーから最新の値をまとめて読み取る。
 * 引数:
 *   out - 読み取った値を格納する sensor_data_t のポインタ
 */
static void read_all_sensors(sensor_data_t *out) {
#if USE_SIMULATED_SENSOR_VALUES
    out->air_temp_c = SIMULATED_AIR_TEMP_C;
    out->humidity_percent = SIMULATED_HUMIDITY_PERCENT;
    out->water_temp_c = SIMULATED_WATER_TEMP_C;
    out->soil_raw = SIMULATED_SOIL_RAW;
    out->soil_dry = false;
    ESP_LOGW(TAG, "Using simulated sensor values");
    return;
#endif

    dht22_reading_t dht = dht22_read(PIN_DHT);
    if (dht.valid) {
        out->air_temp_c = dht.temperature_c;
        out->humidity_percent = dht.humidity_percent;
    }

    float water_temp;
    if (ds18b20_read_temp_c(PIN_WATER_TEMP, &water_temp)) {
        out->water_temp_c = water_temp;
    }

    int soil_raw = 0;
    adc_oneshot_read(s_adc_handle, PIN_SOIL_MOISTURE, &soil_raw);
    out->soil_raw = soil_raw;
    out->soil_dry = (out->soil_raw < SOIL_DRY_THRESHOLD);
}

/*
 * update_water_safety_control
 * 概要: 水温が高すぎるときにファンを回し、ポンプを止める安全制御を行う。
 * 役割:
 *   - WATER_TEMP_HIGH を超えたら過熱状態とみなす
 *   - WATER_TEMP_SAFE を下回ると状態を解除する
 *   - ヒステリシスを使って ON/OFF の切り替えを安定化させる
 * 引数:
 *   data - 現在のセンサーデータを参照・更新するポインタ
 */
static void update_water_safety_control(sensor_data_t *data) {
    if (data->water_temp_c >= WATER_TEMP_HIGH) {
        g_water_overheat = true;
    } else if (data->water_temp_c <= WATER_TEMP_SAFE) {
        g_water_overheat = false;
    }
    /* between thresholds: keep previous state */

    data->fan_on = g_water_overheat;
    gpio_set_level(PIN_FAN, data->fan_on ? 1 : 0);

    if (g_water_overheat) {
        data->pump_on = false;
        gpio_set_level(PIN_PUMP, 0);
    }
}

/*
 * update_pump_cycle
 * 概要: AC電源稼働時にポンプを断続的に回すための周期制御を行う。
 * 仕様:
 *   - 30分 OFF / 5分 ON のサイクルで運転
 *   - 過熱状態のときは強制的に停止する
 * 引数:
 *   data - 現在のポンプ状態と制御結果を反映するポインタ
 */
static void update_pump_cycle(sensor_data_t *data) {
    int64_t now = now_ms();
    int64_t elapsed = now - g_pump_cycle_start_ms;

    if (g_pump_cycle_phase_on) {
        if (elapsed >= PUMP_ON_MS) {
            g_pump_cycle_phase_on = false;
            g_pump_cycle_start_ms = now;
        }
    } else {
        if (elapsed >= PUMP_OFF_MS) {
            g_pump_cycle_phase_on = true;
            g_pump_cycle_start_ms = now;
        }
    }

    data->pump_on = g_water_overheat ? false : g_pump_cycle_phase_on;
    gpio_set_level(PIN_PUMP, data->pump_on ? 1 : 0);
}

/*
 * update_soil_warning_led
 * 概要: 土が乾燥しているときだけ警告 LED を点滅させる。
 * 役割:
 *   - 土が乾いていないときは LED を消灯
 *   - 乾燥状態では LED を非同期に点滅させる
 * 引数:
 *   data - 土壌乾燥状態を確認するセンサーデータ
 */
static void update_soil_warning_led(const sensor_data_t *data) {
    if (!data->soil_dry) {
        g_led_state = false;
        gpio_set_level(PIN_LED, 0);
        return;
    }
    int64_t now = now_ms();
    if (now - g_last_led_toggle_ms >= LED_BLINK_INTERVAL_MS) {
        g_last_led_toggle_ms = now;
        g_led_state = !g_led_state;
        gpio_set_level(PIN_LED, g_led_state ? 1 : 0);
    }
}

/*
 * run_ac_adapter_mode
 * 概要: AC電源モードでの常時監視・制御ループを実行する。
 * 動作:
 *   - Wi-Fi AP を起動してブラウザ確認を可能にする
 *   - 2秒ごとにセンサー値を更新する
 *   - 水温/土壌状態に応じてファンとポンプを制御する
 *   - この関数は通常終了しない
 */
static void run_ac_adapter_mode(void) {
    ESP_LOGI(TAG, "AC Adapter Mode (serial sensor debug)");

    sensor_data_t data = {0};
    read_all_sensors(&data);
    sensor_data_set(&data);
    ESP_LOGI(TAG, "air=%.1f C, humidity=%.1f %%, water=%.1f C, soil_raw=%d, soil_dry=%s",
             data.air_temp_c, data.humidity_percent, data.water_temp_c,
             data.soil_raw, data.soil_dry ? "true" : "false");

    g_pump_cycle_start_ms = now_ms();
    g_pump_cycle_phase_on = false;

    int64_t last_sensor_read_ms = now_ms();

    while (1) {
        int64_t now = now_ms();
        ESP_LOGD(TAG, "Main loop iteration at %" PRIi64 " ms", now);
        if (now - last_sensor_read_ms >= 2000) {
            last_sensor_read_ms = now;
            read_all_sensors(&data);
            ESP_LOGI(TAG, "air=%.1f C, humidity=%.1f %%, water=%.1f C, soil_raw=%d, soil_dry=%s",
                     data.air_temp_c, data.humidity_percent, data.water_temp_c,
                     data.soil_raw, data.soil_dry ? "true" : "false");
        }
        update_water_safety_control(&data);
        update_pump_cycle(&data);
        update_soil_warning_led(&data);
        sensor_data_set(&data);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/*
 * run_mobile_battery_mode_once
 * 概要: バッテリーモードで 1 サイクルだけ動作し、その後深睡眠に入る。
 * 動作:
 *   - 一時的に AP を起動してデータ確認可能にする
 *   - 15秒の猶予期間後に AP を停止
 *   - 1回だけ土壌・水温を計測
 *   - 過熱時はファンで冷却し、その後 30 分間の深睡眠へ移行
 */
static void run_mobile_battery_mode_once(void) {
    ESP_LOGI(TAG, "Mobile Battery Mode (deep sleep cycle)");

    /* Grace period: allow emergency data reading via SoftAP for 15 seconds. */
    wifi_ap_start(WIFI_SSID, WIFI_PASSWORD);
    httpd_handle_t server = webserver_start();

    int64_t ap_start = now_ms();
    while (now_ms() - ap_start < AP_GRACE_PERIOD_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    webserver_stop(server);
    wifi_ap_stop();

    sensor_data_t data = {0};
    read_all_sensors(&data);

    gpio_set_level(PIN_PUMP, 0); /* pump stays off in mobile battery mode */
    data.pump_on = false;

    bool overheat = (data.water_temp_c >= WATER_TEMP_HIGH);
    if (overheat) {
        ESP_LOGI(TAG, "Water overheat detected -> cooling fan 5 min");
        gpio_set_level(PIN_FAN, 1);
        vTaskDelay(pdMS_TO_TICKS(FAN_COOLING_MS));
        gpio_set_level(PIN_FAN, 0);
    } else {
        gpio_set_level(PIN_FAN, 0);
    }

    ESP_LOGI(TAG, "Entering deep sleep for 30 minutes");
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US);
    esp_deep_sleep_start();
}

/*
 * app_main
 * 概要: アプリケーションのエントリーポイント。
 * 役割:
 *   - GPIO と ADC, センサードライバを初期化
 *   - モードスイッチの状態で AC 電源モードまたはバッテリーモードを選択
 *   - 対応する制御ループを実行する
 */
void app_main(void) {
    gpio_outputs_init();
    adc_init();
    dht22_init(PIN_DHT);
    ds18b20_init(PIN_WATER_TEMP);
    sensor_data_init();

    bool is_ac_mode = (gpio_get_level(PIN_MODE_SWITCH) == 0);

    if (is_ac_mode) {
        run_ac_adapter_mode(); /* never returns */
    } else {
        run_mobile_battery_mode_once(); /* ends in deep sleep, never returns */
    }
}
