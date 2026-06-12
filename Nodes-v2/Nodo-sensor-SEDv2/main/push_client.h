#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sensor_ldr.h"

esp_err_t push_telemetry(sensor_data_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first);
