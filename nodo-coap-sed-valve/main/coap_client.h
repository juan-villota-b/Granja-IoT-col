#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t coap_send_telemetry(float temp_c, uint8_t hum_pct);
bool      coap_should_send(float temp_c, uint8_t hum_pct);
