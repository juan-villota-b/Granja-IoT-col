#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    float    temp_threshold_c;
    uint16_t heartbeat_s;
    uint32_t sample_interval_ms;
    float    lat;
    float    lng;
} runtime_config_t;

extern runtime_config_t g_runtime_config;

esp_err_t runtime_config_init(void);
esp_err_t runtime_config_save(void);
