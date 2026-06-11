#pragma once

#include <stdint.h>

typedef struct {
    float humedad;
} sensor_data_t;

#ifndef HW390_GPIO
#define HW390_GPIO 4
#endif

/* Curva de calibracion HW-390: el voltaje baja cuando aumenta la humedad. */
#define HW390_AIR_MV    2800   /* valor aproximado en aire o suelo seco */
#define HW390_WATER_MV  1300   /* valor aproximado en agua o suelo humedo */

void sensor_hw390_init(void);
sensor_data_t sensor_hw390_leer(void);
