#pragma once

#include "esp_err.h"

/**
 * Verifica si el nodo ya está registrado en el gateway.
 *
 * En lugar de mantener un flag local, el nodo siempre sirve /sys/info.
 * El gateway descubre el nodo a través de Thread y consulta GET /sys/info
 * para obtener su identidad. Así el nodo no necesita conocer la IP del gateway.
 *
 * Esta función es un placeholder para futura lógica de registro activo
 * (ej: CoAP POST a una dirección de gateway descubierta vía mDNS).
 *
 * Por ahora retorna ESP_OK inmediatamente.
 */
esp_err_t registration_check(void);
