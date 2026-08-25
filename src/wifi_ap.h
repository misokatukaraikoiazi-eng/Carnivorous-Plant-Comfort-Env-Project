#pragma once

/*
 * wifi_ap_start
 * 概要: ESP32 をアクセスポイントとして起動し、ブラウザから状態を確認できるようにする。
 * 引数:
 *   ssid     - 無線LANのESSID。端末側で接続するアクセスポイント名
 *   password - 接続に使用するパスワード
 */
void wifi_ap_start(const char *ssid, const char *password);

/*
 * wifi_ap_stop
 * 概要: 無線アクセスポイントを停止し、電波の発信を止める。
 */
void wifi_ap_stop(void);
