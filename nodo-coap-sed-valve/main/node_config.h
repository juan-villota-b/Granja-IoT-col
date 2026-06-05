#pragma once

#define NODE_ID             "NODO-VALVE-AUTO"
#define ZONE_ID             "ZONA-A"
#define NODE_TYPE           "valve"
#define LAT                 5.031500f
#define LNG                 -75.469100f
#define FW_VERSION          "1.0.0"

#define TEMP_BASELINE       25.0f
#define HUM_BASELINE        60
#define SAMPLE_INTERVAL_MS  30000

#define HEARTBEAT_INTERVAL_S    60    // s — keepalive máximo sin enviar
#define TEMP_THRESHOLD_C        0.5f   // °C — cambio mínimo para notificar
#define HUM_THRESHOLD_PCT       3      // %  — cambio mínimo para notificar

#define COAP_SERVER_PORT       5685
#define COAP_TIMEOUT_MS         5000
#define BRIDGE_IPV6             "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
