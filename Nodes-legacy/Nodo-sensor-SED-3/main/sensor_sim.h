#pragma once

#include <stdint.h>

typedef struct {
    float temperatura_c;
    uint8_t humedad_pct;
} sensor_lectura_t;

void sensor_init(void);

sensor_lectura_t sensor_leer(void);
