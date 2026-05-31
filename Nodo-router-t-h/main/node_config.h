#pragma once

/*
 * =========================================================================
 *  node_config.h — Configuración única del nodo
 * =========================================================================
 *
 *  ⭐ Único archivo que un usuario nuevo debe editar para personalizar
 *     su nodo. Cambiar estos defines y flashear.
 *
 *  Uso:
 *    1. Copiar todo el proyecto
 *    2. Editar IDENTIDAD, ZONA y POSICIÓN
 *    3. Si la red Thread cambió, editar RED THREAD
 *    4. idf.py build flash monitor
 *
 * =========================================================================
 */

/* ============================ IDENTIDAD ================================ */

/* Nombre único del nodo dentro del proyecto. Ej: "NODO-TH-01", "SENSOR-A" */
#define NODE_ID             "NODO-TH-01"

/* Zona agrícola a la que pertenece. Ej: "ZONA-A", "INVERNADERO-1" */
#define ZONE_ID             "ZONA-A"

/* Tipo de dispositivo. Usar "router_th" para router temperatura+humedad */
#define NODE_TYPE           "router_th"

/* Posición en el plano de la granja (metros, coordenadas cartesianas) */
#define POS_X               25.0f
#define POS_Y               40.0f

/* Versión del firmware para trazabilidad */
#define FW_VERSION          "1.0.0"

/* ============================== RED THREAD ============================= */

/* Canal IEEE 802.15.4 (11-26). Usar 15 si está limpio. */
#define THREAD_CHANNEL      15

/* PAN ID de la red Thread (hex, 0x0001-0xFFFE) */
#define THREAD_PANID        0x1234

/* Nombre de la red Thread */
#define THREAD_NETWORK_NAME "IOT-LAB-NET"

/* Clave maestra de la red (16 bytes en hex) */
#define THREAD_NETWORK_KEY  "00112233445566778899aabbccddeeff"

/* Extended PAN ID (8 bytes en hex) */
#define THREAD_EXT_PAN_ID   "dead00beef00cafe"

/* PSKc para commissioner (16 bytes en hex) */
#define THREAD_PSKC         "104810e2315100afd6bc9215a6bfac53"

/* ========================= SENSOR (SIMULADO) =========================== */

/* Temperatura base para la simulación (°C) */
#define TEMP_BASELINE       25.0f

/* Humedad base para la simulación (%RH) */
#define HUM_BASELINE        60

/* Intervalo entre lecturas de sensor (ms) */
#define SAMPLE_INTERVAL_MS  30000

/* ============================ TELEMETRÍA =============================== */

/* Intervalo máximo entre notificaciones (heartbeat) en segundos */
/* Se puede modificar remotamente via CoAP PUT /config/thresholds */
#define HEARTBEAT_INTERVAL_S    45

/* Umbral de cambio de temperatura para notificar (°C) */
/* Se puede modificar remotamente via CoAP PUT /config/thresholds */
#define TEMP_THRESHOLD_C        0.5f

/* Umbral de cambio de humedad para notificar (%RH) */
/* Se puede modificar remotamente via CoAP PUT /config/thresholds */
#define HUM_THRESHOLD_PCT       3

/* ============================ ENERGÍA ================================== */

/* 1 = habilitar light sleep + DFS entre transmisiones */
/* 0 = mantener CPU siempre activa (mayor consumo, menor latencia) */
#define POWER_SAVE_ENABLED      1

/* 1 = modo batería: deshabilita logs verbosos, reduce consumo */
/* 0 = modo debug: todos los logs visibles por USB */
#define BATTERY_MODE            1

/* ============================== COAP =================================== */

/* Puerto CoAP estándar */
#define COAP_PORT           5683
