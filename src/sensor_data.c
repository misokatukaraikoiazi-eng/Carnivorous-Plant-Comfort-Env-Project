#include "sensor_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static sensor_data_t s_data = {0};
static SemaphoreHandle_t s_mutex = NULL;

void sensor_data_init(void) {
    s_mutex = xSemaphoreCreateMutex();
}

void sensor_data_set(const sensor_data_t *data) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data = *data;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void sensor_data_get(sensor_data_t *data) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    *data = s_data;
    if (s_mutex) xSemaphoreGive(s_mutex);
}
