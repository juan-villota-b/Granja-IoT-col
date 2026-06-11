#pragma once

#include "esp_err.h"

/* Envia CON POST /readings al Bridge con payload CBOR de telemetria.
   Mismo mecanismo que registration_client.c (CON fiable).
   payload: map(5) {t:float16, h:uint8, b:uint16, r:negint8, u:uint32} */
esp_err_t push_telemetry(float temp_c, uint8_t hum_pct,
                         uint16_t batt_mv, int8_t rssi, uint32_t uptime_s);
