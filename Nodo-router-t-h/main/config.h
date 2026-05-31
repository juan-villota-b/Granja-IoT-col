#pragma once

#include "esp_err.h"
#include <stdint.h>

/*
 * Estructura de configuración dinámica del nodo.
 * Los valores se cargan desde NVS al boot (o defaults de node_config.h).
 * Se actualizan via CoAP PUT /config/thresholds.
 */
typedef struct {
    float   temp_threshold_c;      /* °C: umbral de cambio para notificar */
    uint8_t hum_threshold_pct;     /* %RH: umbral de cambio para notificar */
    uint16_t heartbeat_s;          /* s: intervalo máximo sin notificar */
} nodo_config_t;

/* Variable global de configuración — leer desde cualquier módulo */
extern nodo_config_t g_config;

/**
 * Inicializa la configuración:
 *   - Si hay datos guardados en NVS, los carga
 *   - Si no, usa los defines de node_config.h como default
 *   - Los defaults quedan disponibles para el primer arranque
 */
esp_err_t config_init(void);

/**
 * Persiste la configuración actual (g_config) en NVS.
 * Llamar después de cada PUT exitoso.
 */
esp_err_t config_save(void);
