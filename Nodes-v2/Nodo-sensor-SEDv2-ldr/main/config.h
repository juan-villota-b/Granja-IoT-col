#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    float    luz_threshold;
    uint16_t heartbeat_s;
    uint32_t sample_interval_ms;
    float    lat;
    float    lng;
} nodo_config_t;

extern nodo_config_t g_config;

esp_err_t config_init(void);
esp_err_t config_save(void);
