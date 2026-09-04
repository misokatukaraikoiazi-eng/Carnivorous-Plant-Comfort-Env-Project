extern "C" {
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dht22.h"
#include "ds18b20.h"
#include "sensor_data.h"
#include "sh1106.h"
#include "webserver.h"
#include "wifi_ap.h"
}

namespace {

constexpr gpio_num_t PIN_DHT = GPIO_NUM_26;
constexpr gpio_num_t PIN_WATER_TEMP = GPIO_NUM_25;
constexpr gpio_num_t PIN_FAN = GPIO_NUM_19;
constexpr gpio_num_t PIN_WARNING_LED = GPIO_NUM_23;
constexpr gpio_num_t PIN_MODE_SWITCH = GPIO_NUM_27;
constexpr adc_channel_t PIN_SOIL_MOISTURE = ADC_CHANNEL_6;  // GPIO34 / ADC1
constexpr gpio_num_t PIN_BUTTON_MENU = GPIO_NUM_32;
constexpr gpio_num_t PIN_BUTTON_UP = GPIO_NUM_33;
constexpr gpio_num_t PIN_BUTTON_DOWN = GPIO_NUM_16;
constexpr gpio_num_t PIN_BUTTON_SAVE = GPIO_NUM_17;

constexpr float DEFAULT_WATER_TEMP_HIGH_C = 28.0F;
constexpr float DEFAULT_WATER_TEMP_SAFE_C = 26.5F;
constexpr int32_t DEFAULT_SOIL_DRY_THRESHOLD = 2200;
constexpr uint32_t PWM_FREQUENCY_HZ = 25000;
constexpr uint32_t PWM_DUTY_MAX = 1023;  // LEDC 10 bit
constexpr uint32_t WIFI_GRACE_PERIOD_MS = 15UL * 1000UL;
constexpr uint32_t FAN_COOLING_MS = 5UL * 60UL * 1000UL;
constexpr uint64_t DEEP_SLEEP_US = 30ULL * 60ULL * 1000000ULL;
constexpr char WIFI_SSID[] = "Terrarium-Monitor";
constexpr char WIFI_PASSWORD[] = "password123";
constexpr char TAG[] = "terrarium";

struct Settings {
    float water_temp_high_c;
    float water_temp_safe_c;
    int32_t soil_dry_threshold;
};

Settings settings = {DEFAULT_WATER_TEMP_HIGH_C, DEFAULT_WATER_TEMP_SAFE_C,
                     DEFAULT_SOIL_DRY_THRESHOLD};
adc_oneshot_unit_handle_t adc_handle = nullptr;
bool water_overheat = false;
bool warning_led_on = false;
int64_t last_led_toggle_ms = 0;

enum class SettingsPage : uint8_t {
    Monitor,
    WaterHigh,
    WaterSafe,
    SoilDry,
};

struct Button {
    gpio_num_t pin;
    bool stable_pressed;
    bool sampled_pressed;
    int64_t changed_ms;
};

Button buttons[] = {
    {PIN_BUTTON_MENU, false, false, 0},
    {PIN_BUTTON_UP, false, false, 0},
    {PIN_BUTTON_DOWN, false, false, 0},
    {PIN_BUTTON_SAVE, false, false, 0},
};
SettingsPage settings_page = SettingsPage::Monitor;

int64_t now_ms() {
    return esp_timer_get_time() / 1000;
}

/* NVSには整数化した値を保存し、書き込み回数と浮動小数点の丸め誤差を抑える。 */
void load_settings_from_nvs() {
    esp_err_t init_result = nvs_flash_init();
    if (init_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        init_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        init_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(init_result);

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("terrarium", NVS_READWRITE, &handle));

    int32_t value = 0;
    if (nvs_get_i32(handle, "water_hi_x10", &value) == ESP_OK) {
        settings.water_temp_high_c = static_cast<float>(value) / 10.0F;
    } else {
        ESP_ERROR_CHECK(nvs_set_i32(handle, "water_hi_x10", 280));
    }
    if (nvs_get_i32(handle, "water_ok_x10", &value) == ESP_OK) {
        settings.water_temp_safe_c = static_cast<float>(value) / 10.0F;
    } else {
        ESP_ERROR_CHECK(nvs_set_i32(handle, "water_ok_x10", 265));
    }
    if (nvs_get_i32(handle, "soil_dry", &value) == ESP_OK) {
        settings.soil_dry_threshold = value;
    } else {
        ESP_ERROR_CHECK(nvs_set_i32(handle, "soil_dry", DEFAULT_SOIL_DRY_THRESHOLD));
    }
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

/* SAVEボタンが押された時だけNVSを書き換え、フラッシュの寿命を保つ。 */
void save_settings_to_nvs() {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("terrarium", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "water_hi_x10",
                                static_cast<int32_t>(settings.water_temp_high_c * 10.0F)));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "water_ok_x10",
                                static_cast<int32_t>(settings.water_temp_safe_c * 10.0F)));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "soil_dry", settings.soil_dry_threshold));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    ESP_LOGI(TAG, "Settings saved to NVS");
}

/* ファンと警告LEDはLEDCで制御する。ポンプはゲートへ直接GPIOを出力する。 */
void ledc_init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = PWM_FREQUENCY_HZ;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const gpio_num_t pins[] = {PIN_FAN, PIN_WARNING_LED};
    for (int channel = 0; channel < 2; ++channel) {
        ledc_channel_config_t config = {};
        config.gpio_num = pins[channel];
        config.speed_mode = LEDC_LOW_SPEED_MODE;
        config.channel = static_cast<ledc_channel_t>(channel);
        config.timer_sel = LEDC_TIMER_0;
        config.duty = 0;
        config.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&config));
    }
}

void set_pwm(ledc_channel_t channel, bool enabled) {
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel,
                                 enabled ? PWM_DUTY_MAX : 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
}

void hardware_init() {
    gpio_config_t switch_config = {};
    switch_config.pin_bit_mask = 1ULL << PIN_MODE_SWITCH;
    switch_config.mode = GPIO_MODE_INPUT;
    switch_config.pull_up_en = GPIO_PULLUP_ENABLE;
    switch_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&switch_config));

    /* 各ボタンはGPIOとGNDの間に接続する。内部プルアップにより未押下はHIGHとなる。 */
    gpio_config_t button_config = {};
    button_config.pin_bit_mask = (1ULL << PIN_BUTTON_MENU) | (1ULL << PIN_BUTTON_UP) |
                                 (1ULL << PIN_BUTTON_DOWN) | (1ULL << PIN_BUTTON_SAVE);
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&button_config));

    ledc_init();
    adc_oneshot_unit_init_cfg_t adc_config = {};
    adc_config.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc_handle));
    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    channel_config.atten = ADC_ATTEN_DB_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, PIN_SOIL_MOISTURE,
                                               &channel_config));

    dht22_init(PIN_DHT);
    ds18b20_init(PIN_WATER_TEMP);
    sensor_data_init();
    if (!sh1106_init(SH1106_I2C_SDA_PIN, SH1106_I2C_SCL_PIN, SH1106_I2C_ADDR)) {
        ESP_LOGW(TAG, "SH1106 OLEDを初期化できません。制御は継続します。");
    }
}

/* センサー読み取りは制御タスクだけが担当し、Web表示側とは共有データ経由で同期する。 */
void read_sensors(sensor_data_t *data) {
    static int64_t last_dht_read_ms = -2000;
    const int64_t current_ms = now_ms();
    if (current_ms - last_dht_read_ms >= 2000) {
        last_dht_read_ms = current_ms;
        dht22_reading_t dht = dht22_read(PIN_DHT);
        if (dht.valid) {
            data->air_temp_c = dht.temperature_c;
            data->humidity_percent = dht.humidity_percent;
        }
    }

    float water_temp = 0.0F;
    if (ds18b20_read_temp_c(PIN_WATER_TEMP, &water_temp)) {
        data->water_temp_c = water_temp;
    }

    int soil_raw = 0;
    if (adc_oneshot_read(adc_handle, PIN_SOIL_MOISTURE, &soil_raw) == ESP_OK) {
        data->soil_raw = soil_raw;
        data->soil_dry = soil_raw < settings.soil_dry_threshold;
    }
}

void update_control(sensor_data_t *data) {
    if (data->water_temp_c >= settings.water_temp_high_c) {
        water_overheat = true;
    } else if (data->water_temp_c <= settings.water_temp_safe_c) {
        water_overheat = false;
    }

    /* 一旦、ファンとポンプは常時ONにする */
    data->fan_on = true;
    set_pwm(LEDC_CHANNEL_0, true);

    if (!data->soil_dry) {
        warning_led_on = false;
    } else if (now_ms() - last_led_toggle_ms >= 300) {
        warning_led_on = !warning_led_on;
        last_led_toggle_ms = now_ms();
    }
    set_pwm(LEDC_CHANNEL_1, warning_led_on);
}

/* 50ms以上状態が変わらなかった押下だけを、1回のボタンイベントとして返す。 */
bool button_pressed(Button *button) {
    const bool pressed = gpio_get_level(button->pin) == 0;
    const int64_t now = now_ms();
    if (pressed != button->sampled_pressed) {
        button->sampled_pressed = pressed;
        button->changed_ms = now;
    }
    if (button->stable_pressed != button->sampled_pressed && now - button->changed_ms >= 50) {
        button->stable_pressed = button->sampled_pressed;
        return button->stable_pressed;
    }
    return false;
}

void render_settings_screen() {
    char line[22];
    sh1106_clear();
    sh1106_draw_string(0, 0, "-- SETTINGS --");
    snprintf(line, sizeof(line), "%c HIGH: %4.1f C", settings_page == SettingsPage::WaterHigh ? '>' : ' ',
             settings.water_temp_high_c);
    sh1106_draw_string(1, 0, line);
    snprintf(line, sizeof(line), "%c SAFE: %4.1f C", settings_page == SettingsPage::WaterSafe ? '>' : ' ',
             settings.water_temp_safe_c);
    sh1106_draw_string(2, 0, line);
    snprintf(line, sizeof(line), "%c SOIL: %4ld", settings_page == SettingsPage::SoilDry ? '>' : ' ',
             static_cast<long>(settings.soil_dry_threshold));
    sh1106_draw_string(3, 0, line);
    sh1106_draw_string(5, 0, "UP/DN: CHANGE");
    sh1106_draw_string(6, 0, "MENU: NEXT");
    sh1106_draw_string(7, 0, "SAVE: STORE/EXIT");
    sh1106_update();
}

void adjust_selected_setting(int direction) {
    switch (settings_page) {
        case SettingsPage::WaterHigh:
            settings.water_temp_high_c += 0.5F * direction;
            if (settings.water_temp_high_c < settings.water_temp_safe_c + 0.5F) {
                settings.water_temp_high_c = settings.water_temp_safe_c + 0.5F;
            }
            break;
        case SettingsPage::WaterSafe:
            settings.water_temp_safe_c += 0.5F * direction;
            if (settings.water_temp_safe_c > settings.water_temp_high_c - 0.5F) {
                settings.water_temp_safe_c = settings.water_temp_high_c - 0.5F;
            }
            break;
        case SettingsPage::SoilDry:
            settings.soil_dry_threshold += 50 * direction;
            if (settings.soil_dry_threshold < 0) settings.soil_dry_threshold = 0;
            if (settings.soil_dry_threshold > 4095) settings.soil_dry_threshold = 4095;
            break;
        case SettingsPage::Monitor:
            break;
    }
}

/* ボタン操作の結果を即時にOLEDへ描画し、SAVE時だけ設定をNVSへ確定する。 */
void update_settings_ui(const sensor_data_t *data, bool sensor_updated) {
    bool redraw = sensor_updated;
    if (button_pressed(&buttons[0])) {
        if (settings_page == SettingsPage::Monitor) {
            settings_page = SettingsPage::WaterHigh;
        } else {
            settings_page = static_cast<SettingsPage>(static_cast<uint8_t>(settings_page) + 1);
            if (settings_page > SettingsPage::SoilDry) settings_page = SettingsPage::WaterHigh;
        }
        redraw = true;
    }
    if (settings_page != SettingsPage::Monitor) {
        if (button_pressed(&buttons[1])) {
            adjust_selected_setting(1);
            redraw = true;
        }
        if (button_pressed(&buttons[2])) {
            adjust_selected_setting(-1);
            redraw = true;
        }
        if (button_pressed(&buttons[3])) {
            save_settings_to_nvs();
            settings_page = SettingsPage::Monitor;
            redraw = true;
        }
    }

    if (!redraw) return;

    if (settings_page == SettingsPage::Monitor) {
        sh1106_display_sensor_data(data, true);
    } else {
        render_settings_screen();
    }
}

/* AC電源時はセンサーと制御を1秒周期、ボタンと画面を50ms周期で処理する。 */
void control_task(void *) {
    sensor_data_t data = {};
    int64_t last_sensor_read_ms = 0;
    while (true) {
        bool sensor_updated = false;
        if (now_ms() - last_sensor_read_ms >= 1000) {
            last_sensor_read_ms = now_ms();
            read_sensors(&data);
            update_control(&data);
            sensor_data_set(&data);
            sensor_updated = true;
            ESP_LOGI(TAG, "air=%.1fC humidity=%.1f%% water=%.1fC soil=%d fan=%s pump=%s",
                     data.air_temp_c, data.humidity_percent, data.water_temp_c, data.soil_raw,
                     data.fan_on ? "ON" : "OFF", data.pump_on ? "ON" : "OFF");
        }
                update_settings_ui(&data, sensor_updated);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* Wi-Fi APはバッテリー時だけ15秒で停止し、AC電源時は監視ページを継続提供する。 */
void wifi_task(void *argument) {
    const bool stop_after_grace_period = argument != nullptr;
    wifi_ap_start(WIFI_SSID, WIFI_PASSWORD);
    httpd_handle_t server = webserver_start();
    if (stop_after_grace_period) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_GRACE_PERIOD_MS));
        webserver_stop(server);
        wifi_ap_stop();
    }
    vTaskDelete(nullptr);
}

void start_task(TaskFunction_t function, const char *name, uint32_t stack_size, void *argument) {
    BaseType_t result = xTaskCreatePinnedToCore(function, name, stack_size, argument, 5, nullptr, 1);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_FAIL);
}

void run_battery_cycle() {
    start_task(wifi_task, "wifi_task", 4096, reinterpret_cast<void *>(1));
    vTaskDelay(pdMS_TO_TICKS(WIFI_GRACE_PERIOD_MS));

    sensor_data_t data = {};
    read_sensors(&data);
    /* テスト用: モバイルバッテリーモード時もポンプとファンをON */
    data.fan_on = true;
    set_pwm(LEDC_CHANNEL_0, true);
    sensor_data_set(&data);
    sh1106_display_sensor_data(&data, false);

    if (data.fan_on) {
        vTaskDelay(pdMS_TO_TICKS(FAN_COOLING_MS));
        set_pwm(LEDC_CHANNEL_0, false);
    }
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US);
    esp_deep_sleep_start();
}

}  // namespace

extern "C" void app_main(void) {
    load_settings_from_nvs();
    hardware_init();

    const bool ac_adapter_mode = gpio_get_level(PIN_MODE_SWITCH) == 0;
    if (ac_adapter_mode) {
        start_task(wifi_task, "wifi_task", 4096, nullptr);
        start_task(control_task, "control_task", 6144, nullptr);
        vTaskDelete(nullptr);
    }
    run_battery_cycle();
}