#include "coap_server.h"

#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_openthread.h"
#include "openthread/platform/radio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"
#include "openthread/thread.h"

#include "node_config.h"
#include "config.h"
#include "sensor_sim.h"

static const char *TAG = "srv";

/* ── Recursos ── */
static coap_resource_t *g_res_env    = NULL;
static coap_resource_t *g_res_config = NULL;

/* ── Ultimos valores notificados para filtro de umbrales ── */
static float       g_last_temp    = TEMP_BASELINE;
static uint8_t     g_last_hum     = HUM_BASELINE;
static TickType_t  g_last_notify  = 0;

/* ── CBOR: float32 → float16 (half-precision IEEE 754) ── */
static uint16_t f32_to_f16(float f)
{
    uint32_t x; memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t  exp  = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (x >> 13) & 0x3FF;
    if (exp <= 0)  return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

/* ── CBOR: encode /env payload (5 keys, ~25 bytes) ──
   A5 61 74 F9 hh ll 61 68 18 hh 61 62 19 hh ll 61 72 38 hh 61 75 1A hh hh hh hh
        t   float16    h   uint8    b   uint16     r   negint   u   uint32      */
static size_t encode_env_cbor(float t, uint8_t h, int8_t rssi,
                               uint16_t batt, uint32_t up, uint8_t out[32])
{
    uint16_t tf = f32_to_f16(t);
    uint8_t rn  = (uint8_t)((-(int16_t)rssi) - 1); /* negint encoding */
    size_t pos  = 0;

    out[pos++] = 0xA5;                          /* map(5)                 */
    out[pos++] = 0x61; out[pos++] = 't';        /* text(1) "t"            */
    out[pos++] = 0xF9;                          /* float16                */
    out[pos++] = (uint8_t)(tf >> 8);
    out[pos++] = (uint8_t)(tf & 0xFF);

    out[pos++] = 0x61; out[pos++] = 'h';        /* text(1) "h"            */
    if (h < 24) { out[pos++] = h; }
    else        { out[pos++] = 0x18; out[pos++] = h; }

    out[pos++] = 0x61; out[pos++] = 'b';        /* text(1) "b" (battery)  */
    out[pos++] = 0x19;                          /* uint16                 */
    out[pos++] = (uint8_t)(batt >> 8);
    out[pos++] = (uint8_t)(batt & 0xFF);

    out[pos++] = 0x61; out[pos++] = 'r';        /* text(1) "r" (rssi)     */
    out[pos++] = 0x38;                          /* negint(8bit)           */
    out[pos++] = rn;

    out[pos++] = 0x61; out[pos++] = 'u';        /* text(1) "u" (uptime)   */
    out[pos++] = 0x1A;                          /* uint32                 */
    out[pos++] = (uint8_t)(up >> 24);
    out[pos++] = (uint8_t)((up >> 16) & 0xFF);
    out[pos++] = (uint8_t)((up >> 8) & 0xFF);
    out[pos++] = (uint8_t)(up & 0xFF);

    return pos;
}

/* ── RSSI hacia el padre ── */
static int8_t get_rssi(void)
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return -65;
    int8_t rssi;
    if (otThreadGetParentLastRssi(ot, &rssi) == OT_ERROR_NONE &&
        rssi < 0 && rssi > -128) return rssi;
    if (otThreadGetParentAverageRssi(ot, &rssi) == OT_ERROR_NONE &&
        rssi < 0 && rssi > -128) return rssi;
    return -65;
}

/* ── Handler: GET /env (con Observe) ── */
static void hnd_get_env(coap_resource_t *r, coap_session_t *s,
                         const coap_pdu_t *req, const coap_string_t *q,
                         coap_pdu_t *resp)
{
    (void)r; (void)s; (void)req; (void)q;

    sensor_lectura_t lectura = sensor_leer();
    uint8_t buf[32];
    size_t len = encode_env_cbor(lectura.temperatura_c, lectura.humedad_pct,
                                  get_rssi(), 3100,
                                  (uint32_t)(xTaskGetTickCount() *
                                             portTICK_PERIOD_MS / 1000),
                                  buf);

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    unsigned char enc[4];
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(enc, sizeof(enc),
                                         COAP_MEDIATYPE_APPLICATION_CBOR), enc);
    coap_add_option(resp, COAP_OPTION_MAXAGE,
                    coap_encode_var_safe(enc, sizeof(enc),
                                         g_config.heartbeat_s), enc);
    coap_add_data(resp, len, buf);
}

/* ── Parser minimal JSON para PUT /config ──
   Acepta llaves: tt, ht, hb, si, lat, lng. Valores numericos.
   Ejemplo: {"tt":1.0,"ht":5,"hb":60,"si":10000,"lat":5.03,"lng":-75.47}   */
static bool parse_config_json(const uint8_t *data, size_t len)
{
    char buf[160];
    size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, data, n); buf[n] = '\0';

    bool ok = false;
    char *p;

    p = strstr(buf, "\"tt\"");  /* temp threshold */
    if (p) { float v = strtof(p + 4, NULL); if (v >= 0.1f && v <= 10.0f)
        { g_config.temp_threshold_c = v; ok = true; } }

    p = strstr(buf, "\"ht\"");  /* hum threshold */
    if (p) { long v = strtol(p + 4, NULL, 10); if (v >= 1 && v <= 50)
        { g_config.hum_threshold_pct = (uint8_t)v; ok = true; } }

    p = strstr(buf, "\"hb\"");  /* heartbeat */
    if (p) { long v = strtol(p + 4, NULL, 10); if (v >= 10 && v <= 600)
        { g_config.heartbeat_s = (uint16_t)v; ok = true; } }

    p = strstr(buf, "\"si\"");  /* sample interval ms */
    if (p) { long v = strtol(p + 4, NULL, 10); if (v >= 1000 && v <= 60000)
        { g_config.sample_interval_ms = (uint32_t)v; ok = true; } }

    p = strstr(buf, "\"lat\""); /* latitud */
    if (p) { float v = strtof(p + 5, NULL); if (v >= -90.0f && v <= 90.0f)
        { g_config.lat = v; ok = true; } }

    p = strstr(buf, "\"lng\""); /* longitud */
    if (p) { float v = strtof(p + 5, NULL); if (v >= -180.0f && v <= 180.0f)
        { g_config.lng = v; ok = true; } }

    return ok;
}

/* ── Handler: PUT /config ── */
static void hnd_put_config(coap_resource_t *r, coap_session_t *s,
                            const coap_pdu_t *req, const coap_string_t *q,
                            coap_pdu_t *resp)
{
    (void)r; (void)s; (void)q;

    size_t len; const uint8_t *data;
    coap_get_data(req, &len, &data);
    if (!data || len == 0) {
        coap_pdu_set_code(resp, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    if (!parse_config_json(data, len)) {
        coap_pdu_set_code(resp, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    config_save();
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGI(TAG, "PUT /config → tt=%.1f ht=%u hb=%u si=%lu lat=%.6f lng=%.6f",
             (double)g_config.temp_threshold_c, g_config.hum_threshold_pct,
             g_config.heartbeat_s, (unsigned long)g_config.sample_interval_ms,
             (double)g_config.lat, (double)g_config.lng);
}

/* ── Handler: PUT /sys/reboot ── */
static void hnd_put_reboot(coap_resource_t *r, coap_session_t *s,
                            const coap_pdu_t *req, const coap_string_t *q,
                            coap_pdu_t *resp)
{
    (void)r; (void)s; (void)req; (void)q;
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGW(TAG, "REINICIANDO...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

/* ── Handler: GET /sys/info ── */
static void hnd_get_info(coap_resource_t *r, coap_session_t *s,
                          const coap_pdu_t *req, const coap_string_t *q,
                          coap_pdu_t *resp)
{
    (void)r; (void)s; (void)req; (void)q;
    char j[256]; unsigned char enc[4];
    size_t len = (size_t)snprintf(j, sizeof(j),
        "{\"id\":\"%s\",\"zone\":\"%s\",\"type\":\"%s\","
        "\"lat\":%.6f,\"lng\":%.6f,\"ver\":\"%s\"}",
        NODE_ID, ZONE_ID, NODE_TYPE,
        (double)g_config.lat, (double)g_config.lng, FW_VERSION);

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(enc, sizeof(enc),
                                         COAP_MEDIATYPE_APPLICATION_CBOR), enc);
    coap_add_data(resp, len, (const uint8_t *)j);
}

/* ── Tarea principal del servidor: YA NO ES UNA TAREA APARTE.
   El contexto CoAP se inicializa via coap_server_init() y
   coap_server_io_process() se llama desde app_main — misma tarea.
   Esto evita la race condition de libcoap (NO es thread-safe)
   que corrompia los sequence numbers de Observe (RFC 7641). ── */

coap_context_t *g_coap_ctx = NULL;

coap_context_t *coap_server_init(void)
{
    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin6.sin6_family = AF_INET6;
    addr.addr.sin6.sin6_port   = htons(COAP_PORT);

    otInstance *ot = esp_openthread_get_instance();
    const otIp6Address *ml_eid = NULL;
    for (int retry = 0; retry < 80; retry++) {
        ml_eid = otThreadGetMeshLocalEid(ot);
        if (ml_eid && ml_eid->mFields.m8[0] != 0) break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!ml_eid || ml_eid->mFields.m8[0] == 0) {
        ESP_LOGE(TAG, "MLEID no disponible, abortando servidor");
        return NULL;
    }
    memcpy(&addr.addr.sin6.sin6_addr, ml_eid, sizeof(addr.addr.sin6.sin6_addr));

    char astr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr.addr.sin6.sin6_addr, astr, sizeof(astr));
    ESP_LOGI(TAG, "CoAP servidor en [%s]:%d", astr, COAP_PORT);

    coap_set_log_level(COAP_LOG_WARN);
    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) { ESP_LOGE(TAG, "coap_new_context"); return NULL; }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    if (!coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP)) {
        ESP_LOGE(TAG, "endpoint fallo"); coap_free_context(ctx);
        return NULL;
    }

    g_res_env = coap_resource_init(coap_make_str_const("env"),
                                    COAP_RESOURCE_FLAGS_NOTIFY_NON_ALWAYS);
    coap_register_handler(g_res_env, COAP_REQUEST_GET, hnd_get_env);
    coap_resource_set_get_observable(g_res_env, 1);
    coap_add_resource(ctx, g_res_env);
             g_res_env ? 1 : 0, (unsigned)(g_res_env ? g_res_env->flags : 0));

    coap_resource_t *r_info = coap_resource_init(coap_make_str_const("sys/info"), 0);
    coap_register_handler(r_info, COAP_REQUEST_GET, hnd_get_info);
    coap_add_resource(ctx, r_info);

    g_res_config = coap_resource_init(coap_make_str_const("config"), 0);
    coap_register_handler(g_res_config, COAP_REQUEST_PUT, hnd_put_config);
    coap_add_resource(ctx, g_res_config);

    coap_resource_t *r_reboot = coap_resource_init(coap_make_str_const("sys/reboot"), 0);
    coap_register_handler(r_reboot, COAP_REQUEST_PUT, hnd_put_reboot);
    coap_add_resource(ctx, r_reboot);

    ESP_LOGI(TAG, "Recursos: /env (GET+Observe) /sys/info /config (PUT) /sys/reboot");

    g_last_notify = xTaskGetTickCount();
    g_coap_ctx = ctx;
    return ctx;
}

void coap_server_io_process(coap_context_t *ctx)
{
    coap_io_process(ctx, 500);  /* 500ms para despachar notificaciones y ACKs */
}

/* ── API publica ── */
esp_err_t coap_server_start(void)
{
    /* wrapper de compatibilidad */
    coap_context_t *ctx = coap_server_init();
    return ctx ? ESP_OK : ESP_FAIL;
}

void coap_server_notify(float temp_c, uint8_t hum_pct)
{
    TickType_t now = xTaskGetTickCount();
    bool notifico = false;

    float dt = fabsf(temp_c - g_last_temp);
    if (dt > g_config.temp_threshold_c) {
        g_last_temp = temp_c;
        if (g_res_env) {
            int ret = coap_resource_set_dirty(g_res_env, NULL);
            ESP_LOGI(TAG, "notify temp: %.2f C (delta=%.2f) ret=%d",
                     (double)temp_c, (double)dt, ret);
        }
        notifico = true;
    }

    int16_t dh = abs((int16_t)hum_pct - (int16_t)g_last_hum);
    if (dh > g_config.hum_threshold_pct) {
        g_last_hum = hum_pct;
        if (g_res_env) coap_resource_notify_observers(g_res_env, NULL);
        ESP_LOGI(TAG, "notify hum: %u %% (delta=%d)", hum_pct, (int)dh);
        notifico = true;
    }

    /* Heartbeat: si no se notifico en heartbeat_s segundos */
    if (!notifico &&
        (now - g_last_notify) >= pdMS_TO_TICKS(g_config.heartbeat_s * 1000)) {
        g_last_temp = temp_c;
        g_last_hum  = hum_pct;
        if (g_res_env) coap_resource_notify_observers(g_res_env, NULL);
        ESP_LOGI(TAG, "heartbeat: T=%.2f H=%u", (double)temp_c, hum_pct);
        notifico = true;
    }

    if (notifico) g_last_notify = now;
}
