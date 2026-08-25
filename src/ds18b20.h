#pragma once

#include "driver/gpio.h"
#include <stdbool.h>

void ds18b20_init(gpio_num_t pin);
bool ds18b20_read_temp_c(gpio_num_t pin, float *out_temp_c);
