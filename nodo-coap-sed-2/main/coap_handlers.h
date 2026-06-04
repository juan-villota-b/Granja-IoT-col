#pragma once

#include "esp_err.h"

esp_err_t coap_server_start(void);

void coap_notify_temp(void);

void coap_notify_hum(void);

void coap_check_and_notify(float temp_c, uint8_t hum_pct);
