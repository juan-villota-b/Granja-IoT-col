#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sensor_temp.h"

esp_err_t push_telemetry(sensor_temp_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first);

esp_err_t provisioning_send(const char *prov_key);
