#pragma once

#include <stdint.h>

typedef struct {
    float porcentaje_luz;
} sensor_data_t;

#ifndef LDR_GPIO
#define LDR_GPIO 4
#endif

void sensor_ldr_init(void);
sensor_data_t sensor_ldr_leer(void);
