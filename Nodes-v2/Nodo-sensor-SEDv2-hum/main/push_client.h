#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "sensor_hw390.h"
#include "coap3/coap.h"

esp_err_t push_telemetry(const char *prov_key, sensor_data_t *lectura,
                          int8_t rssi, uint32_t uptime_s, bool is_first);
esp_err_t provisioning_send(const char *prov_key);
bool cbor_get_string(const uint8_t *data, size_t len,
                     const char *key, char *value, size_t max_len);
