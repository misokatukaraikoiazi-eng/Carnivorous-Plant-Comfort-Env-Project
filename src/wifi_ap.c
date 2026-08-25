#include "wifi_ap.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdbool.h>

static bool s_wifi_started = false;
static bool s_netif_ready = false;

/*
 * wifi_ap_start
 * 概要: ESP32 をアクセスポイントモードで起動し、携帯端末が Wi-Fi に接続できるようにする。
 * 引数:
 *   ssid     - AP の SSID 名
 *   password - 接続用パスワード
 * 備考: まず NVS を初期化し、その後 Wi-Fi ネットワークと AP を設定する。
 */
void wifi_ap_start(const char *ssid, const char *password) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!s_netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap(); /* default AP IP is 192.168.4.1 */
        s_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.channel = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_started = true;
}

/*
 * wifi_ap_stop
 * 概要: AP モードを停止して Wi-Fi を切断する。
 * 役割: エネルギー節約やモード切り替え直前にネットワークを閉じる。
 */
void wifi_ap_stop(void) {
    if (!s_wifi_started) {
        return;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    s_wifi_started = false;
}
