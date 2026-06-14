#pragma once

#define NODE_ID "BOMBA"
#define ZONE_ID "ZONA-D"
#define NODE_TYPE "actuator_pump"
#define FW_VERSION "5.2.0"

#define ACCESS_TOKEN "4"
#define TELEMETRY_KEY 'v'
#define TELEMETRY_FMT 'u'

#define TEMP_THRESHOLD_C_DEFAULT 0.0f
#define HEARTBEAT_S_DEFAULT 1
#define SAMPLE_INTERVAL_MS 1000
#define TEMP_BASELINE 25.0f

#define LAT_DEFAULT 5.028877f
#define LNG_DEFAULT -75.472472f /* ZONA-D: ~20m al sur */

#define COAP_PORT 5683
#define BRIDGE_IPV6 "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
#define BRIDGE_PORT 5685

#define PROV_KEY "ARrCmgL3fBIft94k8F14" /* access token de TB CE */

#define REGISTER_MAX_RETRIES 3
#define REGISTER_RETRY_MS 500
