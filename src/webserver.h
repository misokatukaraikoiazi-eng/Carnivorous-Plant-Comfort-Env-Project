#pragma once

#include "esp_http_server.h"

httpd_handle_t webserver_start(void);
void webserver_stop(httpd_handle_t server);
