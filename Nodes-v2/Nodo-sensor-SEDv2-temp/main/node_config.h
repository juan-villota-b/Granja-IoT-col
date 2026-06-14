#pragma once

#define NODE_ID "Nodo-Temperatura"
#define ZONE_ID "ZONA-B"
#define NODE_TYPE "sensor_sed"
#define FW_VERSION "5.2.0"

#define DHT11_GPIO 4
#define TEMP_THRESHOLD_C_DEFAULT 0.0f
#define HEARTBEAT_S_DEFAULT 300
#define SAMPLE_INTERVAL_MS 30000
#define TEMP_BASELINE 25.0f

#define LAT_DEFAULT 5.029153f
#define LNG_DEFAULT -75.472661f /* ZONA-B */

#define COAP_PORT 5683
#define BRIDGE_IPV6 "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
#define BRIDGE_PORT 5685

#define PROV_KEY "TmueBdWJgGwWdRMGAABv" /* <- access token del dashboard */

#define REGISTER_MAX_RETRIES 3
#define REGISTER_RETRY_MS 1500
