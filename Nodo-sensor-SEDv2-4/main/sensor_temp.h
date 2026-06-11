#pragma once

#include <stdint.h>

typedef struct {
    float temperatura_c;
} sensor_temp_t;

void sensor_temp_init(void);
sensor_temp_t sensor_temp_leer(void);
