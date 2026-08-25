#pragma once

#include "esp_http_server.h"

/*
 * webserver_start
 * 概要: センサー状態を表示する簡易 Web サーバーを起動する。
 * 戻り値:
 *   httpd ハンドル。起動失敗時は NULL を返す。
 */
httpd_handle_t webserver_start(void);

/*
 * webserver_stop
 * 概要: 起動中の Web サーバーを停止する。
 * 引数:
 *   server - 停止対象の httpd ハンドル
 */
void webserver_stop(httpd_handle_t server);
