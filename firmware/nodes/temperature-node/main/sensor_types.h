#pragma once

#include <stdint.h>

typedef struct {
    float    temperatura_c;
    uint8_t  humedad_pct;
    uint16_t luz_lux;
} sensor_reading_t;
