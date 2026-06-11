#pragma once

/* ── Identidad del nodo ── */
#define NODE_ID              "NODO-SENSOR-1"
#define ZONE_ID              "ZONA-A"
#define NODE_TYPE            "sensor_sed"
#define FW_VERSION           "5.0.0"

/* ── Valores por defecto (sobrescritos por NVS si existe) ── */
#define TEMP_THRESHOLD_C_DEFAULT   0.5f
#define HUM_THRESHOLD_PCT_DEFAULT  3
#define HEARTBEAT_S_DEFAULT        300
#define SAMPLE_INTERVAL_MS         30000

/* ── Coordenadas GPS por defecto ── */
#define LAT_DEFAULT           5.029056f
#define LNG_DEFAULT          -75.472472f

/* ── Sensores simulados ── */
#define TEMP_BASELINE         25.0f
#define HUM_BASELINE          60

/* ── Red ── */
#define COAP_PORT              5683       /* puerto servidor CoAP local       */
#define BRIDGE_IPV6            "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
#define BRIDGE_PORT            5685       /* puerto CoAP del Bridge           */

/* ── Registro inicial (CON, una sola vez por arranque) ── */
#define REGISTER_MAX_RETRIES   5
#define REGISTER_RETRY_MS      3000       /* espera entre reintentos          */
