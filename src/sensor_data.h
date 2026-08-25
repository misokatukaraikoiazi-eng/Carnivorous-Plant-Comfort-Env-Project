#pragma once

#include <stdbool.h>

typedef struct {
    float air_temp_c;
    float humidity_percent;
    float water_temp_c;
    int soil_raw;
    bool soil_dry;
    bool pump_on;
    bool fan_on;
} sensor_data_t;

void sensor_data_init(void);
void sensor_data_set(const sensor_data_t *data);
void sensor_data_get(sensor_data_t *data);
