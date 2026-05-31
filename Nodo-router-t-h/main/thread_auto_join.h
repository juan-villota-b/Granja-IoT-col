#pragma once

#include "esp_err.h"

/**
 * Inicia el proceso de unión automática a la red Thread sin intervención
 * del usuario (headless). Lee el dataset activo de NVS o usa los valores
 * por defecto de Kconfig/node_config.h.
 *
 * Retorna ESP_OK cuando el nodo alcanza el estado router o child.
 * Retorna ESP_FAIL si no puede unirse tras reintentos.
 */
esp_err_t thread_auto_join(void);
