#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    float   temp_threshold_c;
    uint8_t hum_threshold_pct;
    uint16_t heartbeat_s;
} nodo_config_t;

extern nodo_config_t g_config;

esp_err_t config_init(void);

esp_err_t config_save(void);
