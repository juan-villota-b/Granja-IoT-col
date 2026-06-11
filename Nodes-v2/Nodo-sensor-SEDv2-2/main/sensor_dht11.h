#pragma once

#include "sensor_temp.h"

#ifndef DHT11_GPIO
#define DHT11_GPIO 4
#endif

void sensor_dht11_init(void);
sensor_temp_t sensor_dht11_leer(void);
