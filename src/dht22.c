#include "dht22.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* Wait until pin reaches the given level, up to timeout_us. Returns elapsed us or -1 on timeout. */
static int wait_level(gpio_num_t pin, int level, int timeout_us) {
    int elapsed = 0;
    while (gpio_get_level(pin) != level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        esp_rom_delay_us(1);
        elapsed++;
    }
    return elapsed;
}

void dht22_init(gpio_num_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 1);
}

dht22_reading_t dht22_read(gpio_num_t pin) {
    dht22_reading_t result = { .temperature_c = 0, .humidity_percent = 0, .valid = false };
    uint8_t data[5] = {0};

    /* Start signal: host pulls the line low, then releases it. */
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(pin, 1);
    esp_rom_delay_us(30);

    /* Sensor response: low ~80us then high ~80us. */
    if (wait_level(pin, 0, 100) < 0) return result;
    if (wait_level(pin, 1, 100) < 0) return result;
    if (wait_level(pin, 0, 100) < 0) return result;

    for (int i = 0; i < 40; i++) {
        if (wait_level(pin, 1, 100) < 0) return result;
        int high_us = wait_level(pin, 0, 100);
        if (high_us < 0) return result;
        uint8_t bit = (high_us > 40) ? 1 : 0; /* ~26-28us = 0, ~70us = 1 */
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | bit);
    }

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return result;
    }

    result.humidity_percent = ((data[0] << 8) | data[1]) / 10.0f;
    int16_t raw_temp = (int16_t)(((data[2] & 0x7F) << 8) | data[3]);
    float temp = raw_temp / 10.0f;
    if (data[2] & 0x80) {
        temp = -temp;
    }
    result.temperature_c = temp;
    result.valid = true;
    return result;
}
