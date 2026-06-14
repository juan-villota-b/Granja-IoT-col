#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float    temperatura_c;
    uint8_t  humedad_pct;
    uint16_t luz_lux;
} sensor_temp_t;

esp_err_t push_telemetry(sensor_temp_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first, int8_t *out_valve);
esp_err_t provisioning_send(const char *prov_key);
