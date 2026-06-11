#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "coap3/coap.h"

/* Inicializa el servidor CoAP en MLEID:5683 y retorna el contexto.
   Registra /env (GET+Observe), /sys/info, /config (PUT), /sys/reboot.
   Debe llamarse UNA vez al arranque. El contexto retornado debe usarse
   en coap_server_io_process() desde la MISMA tarea que llama a
   coap_server_notify() — libcoap NO es thread-safe.
   Observar: ya no crea tarea aparte. El caller es responsable del loop. */
coap_context_t *coap_server_init(void);

/* Wrapper de compatibilidad. Llama a coap_server_init(). */
esp_err_t coap_server_start(void);

/* Procesa paquetes CoAP pendientes (20ms max).
   Debe llamarse desde la MISMA tarea que coap_server_notify(). */
void coap_server_io_process(coap_context_t *ctx);

/* Notifica a observadores de /env si |ΔT| > umbral, |ΔH| > umbral,
   o vence el heartbeat (45s). Llama desde el loop principal. */
void coap_server_notify(float temp_c, uint8_t hum_pct);
