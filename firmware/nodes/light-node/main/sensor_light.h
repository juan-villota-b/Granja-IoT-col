#pragma once

#include <stdint.h>

typedef struct {
    float porcentaje_luz;
} sensor_reading_t;

#ifndef LDR_GPIO
#define LDR_GPIO 4
#endif

void sensor_light_init(void);
sensor_reading_t sensor_light_read(void);
