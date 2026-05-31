#pragma once

#include "esp_err.h"

/**
 * Inicia el servidor CoAP en una tarea separada.
 * Expone los recursos:
 *   /env/temp      GET + Observe  → {t: float16}  (NON)
 *   /env/hum       GET + Observe  → {h: uint8}    (NON)
 *   /sys/info      GET             → {id,zone,type,x,y,ver} (CON)
 *   /sys/health    GET             → {batt,rssi,up} (CON)
 */
esp_err_t coap_server_start(void);

/**
 * Notifica a todos los observadores del recurso /env/temp
 * que la temperatura cambió.
 */
void coap_notify_temp(void);

/**
 * Notifica a todos los observadores del recurso /env/hum
 * que la humedad cambió.
 */
void coap_notify_hum(void);

/*
 * Evalúa si debe notificar según umbrales (temp > 0.5°C, hum > 3%)
 * o heartbeat (45s sin notificar). Llamar desde el loop principal.
 */
void coap_check_and_notify(float temp_c, uint8_t hum_pct);
