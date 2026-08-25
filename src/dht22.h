#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

typedef struct {
    float temperature_c;
    float humidity_percent;
    bool valid;
} dht22_reading_t;

void dht22_init(gpio_num_t pin);
dht22_reading_t dht22_read(gpio_num_t pin);
