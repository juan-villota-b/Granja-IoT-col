#pragma once

#include "esp_err.h"
#include <stdint.h>

/* ── Configuracion persistente en NVS ── */
typedef struct {
    float    temp_threshold_c;      /* umbral de notificacion temperatura    */
    uint8_t  hum_threshold_pct;     /* umbral de notificacion humedad        */
    uint16_t heartbeat_s;           /* intervalo max sin notificar           */
    uint32_t sample_interval_ms;    /* cada cuanto leer sensor y dormir      */
    float    lat;                   /* latitud GPS                          */
    float    lng;                   /* longitud GPS                         */
} nodo_config_t;

extern nodo_config_t g_config;

/* Carga desde NVS; si no hay datos, usa defaults de node_config.h */
esp_err_t config_init(void);

/* Persiste g_config en NVS (sobrevive reinicios) */
esp_err_t config_save(void);
