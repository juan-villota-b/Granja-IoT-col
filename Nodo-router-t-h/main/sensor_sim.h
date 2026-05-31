#pragma once

#include <stdint.h>

/*
 * Estructura con una lectura completa de temperatura y humedad.
 */
typedef struct {
    float temperatura_c;    /* °C */
    uint8_t humedad_pct;    /* %RH */
} sensor_lectura_t;

/**
 * Inicializa el generador de lecturas simuladas.
 */
void sensor_init(void);

/**
 * Obtiene una nueva lectura del sensor (simulado).
 * En el futuro, reemplazar la implementación con el driver real
 * (DHT22, BME280, SHT30, etc.) sin cambiar la interfaz.
 */
sensor_lectura_t sensor_leer(void);
