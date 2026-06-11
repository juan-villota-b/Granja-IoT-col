#pragma once

#include <stdint.h>

typedef struct {
    float    temperatura_c;
    uint8_t  humedad_pct;
    uint16_t luz_lux;
} sensor_temp_t;

void sensor_temp_init(void);
sensor_temp_t sensor_temp_leer(void);
