#pragma once

#include "sensor_types.h"

#ifndef DHT11_GPIO
#define DHT11_GPIO 4
#endif

void sensor_dht11_init(void);
sensor_reading_t sensor_dht11_read(void);
