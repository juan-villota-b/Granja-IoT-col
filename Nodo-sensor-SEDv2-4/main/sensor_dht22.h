#pragma once

#include "sensor_temp.h"

#ifndef DHT22_GPIO
#define DHT22_GPIO 4  /* GPIO por defecto */
#endif

void sensor_dht22_init(void);
sensor_temp_t sensor_dht22_leer(void);
