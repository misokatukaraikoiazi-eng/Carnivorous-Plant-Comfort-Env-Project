#include "webserver.h"
#include "sensor_data.h"
#include <stdio.h>

/*
 * ダッシュボード画面の HTML を定義する。
 * ブラウザ側で /data から JSON を取得し、温度・湿度・換気状態を表示する。
 */
static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>Terrarium Monitor</title><style>"
":root{--bg:#0f1a14;--card:#16241d;--accent:#4caf7d;--warn:#ff5252;--text:#e8f5ec;}"
"*{box-sizing:border-box;}body{margin:0;font-family:'Segoe UI',sans-serif;background:var(--bg);color:var(--text);padding:24px;}"
"h1{text-align:center;font-weight:600;letter-spacing:1px;margin-bottom:24px;}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;max-width:900px;margin:0 auto;}"
".card{background:var(--card);border-radius:14px;padding:18px;box-shadow:0 4px 12px rgba(0,0,0,0.3);text-align:center;}"
".label{font-size:13px;opacity:0.7;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px;}"
".value{font-size:28px;font-weight:700;color:var(--accent);}"
".status-on{color:var(--accent);}.status-off{color:#8a9a92;}.status-warn{color:var(--warn);}"
"</style></head><body>"
"<h1>Carnivorous Plant Terrarium</h1>"
"<div class='grid'>"
"<div class='card'><div class='label'>Air Temp</div><div class='value' id='airTemp'>--</div></div>"
"<div class='card'><div class='label'>Humidity</div><div class='value' id='humidity'>--</div></div>"
"<div class='card'><div class='label'>Water Temp</div><div class='value' id='waterTemp'>--</div></div>"
"<div class='card'><div class='label'>Soil Moisture (raw)</div><div class='value' id='soilRaw'>--</div></div>"
"<div class='card'><div class='label'>Pump</div><div class='value' id='pumpStatus'>--</div></div>"
"<div class='card'><div class='label'>Fan</div><div class='value' id='fanStatus'>--</div></div>"
"</div>"
"<script>"
"function refresh(){fetch('/data').then(r=>r.json()).then(d=>{"
"document.getElementById('airTemp').innerText=d.airTemp+' C';"
"document.getElementById('humidity').innerText=d.humidity+' %';"
"document.getElementById('waterTemp').innerText=d.waterTemp+' C';"
"document.getElementById('soilRaw').innerText=d.soilRaw;"
"var p=document.getElementById('pumpStatus');p.innerText=d.pumpOn?'ON':'OFF';p.className='value '+(d.pumpOn?'status-on':'status-off');"
"var f=document.getElementById('fanStatus');f.innerText=d.fanOn?'ON':'OFF';f.className='value '+(d.fanOn?'status-warn':'status-off');"
"});}setInterval(refresh,2000);refresh();"
"</script></body></html>";

/*
 * root_get_handler
 * 概要: 監視ダッシュボードの HTML をクライアントへ返す。
 * 引数:
 *   req - HTTP リクエスト情報
 */
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
}

/*
 * data_get_handler
 * 概要: 現在のセンサーデータを JSON 形式で返す。
 * 役割: Web UI が定期的に /data を読み取り、値を更新できるようにする。
 * 引数:
 *   req - HTTP リクエスト情報
 */
static esp_err_t data_get_handler(httpd_req_t *req) {
    sensor_data_t d;
    sensor_data_get(&d);
    char json[256];
    snprintf(json, sizeof(json),
        "{\"airTemp\":%.1f,\"humidity\":%.1f,\"waterTemp\":%.1f,\"soilRaw\":%d,"
        "\"soilDry\":%s,\"pumpOn\":%s,\"fanOn\":%s}",
        d.air_temp_c, d.humidity_percent, d.water_temp_c, d.soil_raw,
        d.soil_dry ? "true" : "false",
        d.pump_on ? "true" : "false",
        d.fan_on ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/*
 * webserver_start
 * 概要: Web UI 用の HTTP サーバーを開始する。
 * 戻り値: 起動した server のハンドル。失敗時は NULL。
 */
httpd_handle_t webserver_start(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root_uri);
        httpd_uri_t data_uri = { .uri = "/data", .method = HTTP_GET, .handler = data_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &data_uri);
    }
    return server;
}

/*
 * webserver_stop
 * 概要: HTTP サーバーを停止し、リソースを解放する。
 * 引数:
 *   server - 停止対象の HTTP サーバーのハンドル
 */
void webserver_stop(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
    }
}
