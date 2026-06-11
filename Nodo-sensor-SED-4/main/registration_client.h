#pragma once

#include "esp_err.h"

/* Envia CON POST /register al Bridge con identidad del nodo.
   Payload CBOR: {id, tp, v, zn, lat:float32, lng:float32}
   Reintenta hasta REGISTER_MAX_RETRIES con backoff.
   Retorna ESP_OK si recibe ACK (2.xx), ESP_FAIL si no.        */
esp_err_t registration_send_once(float lat, float lng);
