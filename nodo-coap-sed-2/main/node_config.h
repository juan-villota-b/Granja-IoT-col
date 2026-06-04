#pragma once

#define NODE_ID             "NODO-TH-AUTO-2"
#define ZONE_ID             "ZONA-B"
#define NODE_TYPE           "th_auto"
#define LAT                 5.030611f
#define LNG                 -75.467694f
#define FW_VERSION          "2.0.0"

#define TEMP_BASELINE       25.0f
#define HUM_BASELINE        60
#define SAMPLE_INTERVAL_MS  30000

#define HEARTBEAT_INTERVAL_S    300    // s — keepalive máximo sin enviar
#define TEMP_THRESHOLD_C        0.5f   // °C — cambio mínimo para notificar
#define HUM_THRESHOLD_PCT       3      // %  — cambio mínimo para notificar

#define COAP_SERVER_PORT       5685
#define COAP_TIMEOUT_MS         5000
#define BRIDGE_IPV6             "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
