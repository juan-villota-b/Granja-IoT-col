#include "coap_handlers.h"

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

static const char *TAG = "coap";

static coap_resource_t *g_res_temp = NULL;
static coap_resource_t *g_res_hum  = NULL;

static float    g_last_temp = TEMP_BASELINE;
static uint8_t  g_last_hum  = HUM_BASELINE;

static TickType_t g_last_notify_tick = 0;

static uint16_t float32_to_float16(float f)
{
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign  = (x >> 16) & 0x8000;
    int32_t  exp   = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant  = (x >> 13) & 0x3FF;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

static size_t encode_temp_cbor(float value, uint8_t out[6])
{
    uint16_t h = float32_to_float16(value);
    out[0] = 0xA1;
    out[1] = 0x61; out[2] = 0x74;
    out[3] = 0xF9;
    out[4] = (uint8_t)(h >> 8);
    out[5] = (uint8_t)(h & 0xFF);
    return 6;
}

static size_t encode_hum_cbor(uint8_t value, uint8_t out[5])
{
    out[0] = 0xA1;
    out[1] = 0x61; out[2] = 0x68;
    if (value < 24) {
        out[3] = value;
        return 4;
    }
    out[3] = 0x18;
    out[4] = value;
    return 5;
}

static size_t encode_info_cbor(uint8_t *out, size_t max_len)
{
    return snprintf((char *)out, max_len,
        "{\"id\":\"%s\",\"zone\":\"%s\",\"type\":\"%s\","
        "\"lat\":%.6f,\"lng\":%.6f,\"ver\":\"%s\"}",
        NODE_ID, ZONE_ID, NODE_TYPE, LAT, LNG, FW_VERSION);
}

static int8_t get_rssi(void)
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return -65;

    int8_t rssi;
    if (otThreadGetParentLastRssi(ot, &rssi) == OT_ERROR_NONE && rssi < 0 && rssi > -128) {
        return rssi;
    }
    if (otThreadGetParentAverageRssi(ot, &rssi) == OT_ERROR_NONE && rssi < 0 && rssi > -128) {
        return rssi;
    }
    return -65;
}

static size_t encode_health_cbor(uint8_t *out, size_t max_len)
{
    int8_t  rssi_sim = get_rssi();
    uint16_t batt_sim = 3100;
    uint32_t uptime_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    return snprintf((char *)out, max_len,
        "{\"batt\":%u,\"rssi\":%d,\"up\":%lu}",
        batt_sim, rssi_sim, (unsigned long)uptime_s);
}

static void hnd_get_temp(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query, coap_pdu_t *response)
{
    (void)session; (void)request; (void)query;

    sensor_lectura_t lectura = sensor_leer();

    uint8_t buf[6];
    size_t len = encode_temp_cbor(lectura.temperatura_c, buf);

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    unsigned char encoded[4];
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         COAP_MEDIATYPE_APPLICATION_CBOR),
                    encoded);
    coap_add_option(response, COAP_OPTION_MAXAGE,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         g_config.heartbeat_s),
                    encoded);
    coap_add_data(response, len, buf);
}

static void hnd_get_hum(coap_resource_t *resource, coap_session_t *session,
                        const coap_pdu_t *request,
                        const coap_string_t *query, coap_pdu_t *response)
{
    (void)session; (void)request; (void)query;

    sensor_lectura_t lectura = sensor_leer();

    uint8_t buf[5];
    size_t len = encode_hum_cbor(lectura.humedad_pct, buf);

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    unsigned char encoded[4];
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         COAP_MEDIATYPE_APPLICATION_CBOR),
                    encoded);
    coap_add_option(response, COAP_OPTION_MAXAGE,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         g_config.heartbeat_s),
                    encoded);
    coap_add_data(response, len, buf);
}

static void hnd_get_info(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query, coap_pdu_t *response)
{
    (void)resource; (void)session; (void)request; (void)query;

    uint8_t buf[256];
    size_t len = encode_info_cbor(buf, sizeof(buf));

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    unsigned char encoded[4];
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         COAP_MEDIATYPE_APPLICATION_CBOR),
                    encoded);
    coap_add_data(response, len, buf);

    ESP_LOGI(TAG, "GET /sys/info → %s / %s", NODE_ID, ZONE_ID);
}

static void hnd_get_health(coap_resource_t *resource, coap_session_t *session,
                           const coap_pdu_t *request,
                           const coap_string_t *query, coap_pdu_t *response)
{
    (void)resource; (void)session; (void)request; (void)query;

    uint8_t buf[128];
    size_t len = encode_health_cbor(buf, sizeof(buf));

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    unsigned char encoded[4];
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(encoded, sizeof(encoded),
                                         COAP_MEDIATYPE_APPLICATION_CBOR),
                    encoded);
    coap_add_data(response, len, buf);
}

static bool parse_config_payload(const uint8_t *data, size_t len,
                                 float *out_tt, uint8_t *out_ht, uint16_t *out_hb)
{
    char buf[128];
    size_t copy_len = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';

    char *p;

    p = strstr(buf, "\"tt\"");
    if (p) { *out_tt = strtof(p + 4, NULL); }

    p = strstr(buf, "\"ht\"");
    if (p) { *out_ht = (uint8_t)atoi(p + 4); }

    p = strstr(buf, "\"hb\"");
    if (p) { *out_hb = (uint16_t)atoi(p + 4); }

    return true;
}

static void hnd_put_thresholds(coap_resource_t *resource, coap_session_t *session,
                                const coap_pdu_t *request,
                                const coap_string_t *query, coap_pdu_t *response)
{
    (void)resource; (void)session; (void)query;

    size_t len;
    const uint8_t *data;
    coap_get_data(request, &len, &data);

    if (!data || len == 0) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    float new_tt = g_config.temp_threshold_c;
    uint8_t new_ht = g_config.hum_threshold_pct;
    uint16_t new_hb = g_config.heartbeat_s;

    parse_config_payload(data, len, &new_tt, &new_ht, &new_hb);

    if (new_tt < 0.1f || new_tt > 10.0f ||
        new_ht < 1 || new_ht > 50 ||
        new_hb < 10 || new_hb > 600) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    g_config.temp_threshold_c  = new_tt;
    g_config.hum_threshold_pct = new_ht;
    g_config.heartbeat_s       = new_hb;

    config_save();

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGI(TAG, "PUT /config/thresholds → tt=%.1f ht=%u hb=%u",
             new_tt, new_ht, new_hb);
}

static void hnd_put_reboot(coap_resource_t *resource, coap_session_t *session,
                            const coap_pdu_t *request,
                            const coap_string_t *query, coap_pdu_t *response)
{
    (void)resource; (void)session; (void)request; (void)query;

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGW(TAG, "PUT /sys/reboot → REINICIANDO...");

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void coap_server_task(void *pvParameters)
{
    (void)pvParameters;

    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin6.sin6_family = AF_INET6;
    addr.addr.sin6.sin6_port   = htons(COAP_PORT);

    otInstance *instance = esp_openthread_get_instance();
    const otIp6Address *ml_eid;

    for (int retry = 0; retry < 50; retry++) {
        ml_eid = otThreadGetMeshLocalEid(instance);
        const uint8_t *b = (const uint8_t *)ml_eid;
        if (b[0] != 0 || b[1] != 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    memcpy(&addr.addr.sin6.sin6_addr, ml_eid, sizeof(addr.addr.sin6.sin6_addr));

    char addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr.addr.sin6.sin6_addr, addr_str, sizeof(addr_str));
    ESP_LOGI(TAG, "CoAP bindeado a: %s", addr_str);

    coap_set_log_level(COAP_LOG_WARN);

    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) {
        ESP_LOGE(TAG, "coap_new_context fallo");
        vTaskDelete(NULL);
        return;
    }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    if (!coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP)) {
        ESP_LOGE(TAG, "coap_new_endpoint fallo en puerto %d", COAP_PORT);
        coap_free_context(ctx);
        vTaskDelete(NULL);
        return;
    }

    g_res_temp = coap_resource_init(coap_make_str_const("env/temp"), 0);
    coap_register_handler(g_res_temp, COAP_REQUEST_GET, hnd_get_temp);
    coap_resource_set_get_observable(g_res_temp, 1);
    coap_add_resource(ctx, g_res_temp);

    g_res_hum = coap_resource_init(coap_make_str_const("env/hum"), 0);
    coap_register_handler(g_res_hum, COAP_REQUEST_GET, hnd_get_hum);
    coap_resource_set_get_observable(g_res_hum, 1);
    coap_add_resource(ctx, g_res_hum);

    coap_resource_t *r_info = coap_resource_init(
        coap_make_str_const("sys/info"), 0);
    coap_register_handler(r_info, COAP_REQUEST_GET, hnd_get_info);
    coap_add_resource(ctx, r_info);

    coap_resource_t *r_health = coap_resource_init(
        coap_make_str_const("sys/health"), 0);
    coap_register_handler(r_health, COAP_REQUEST_GET, hnd_get_health);
    coap_add_resource(ctx, r_health);

    coap_resource_t *r_cfg = coap_resource_init(
        coap_make_str_const("config/thresholds"), 0);
    coap_register_handler(r_cfg, COAP_REQUEST_PUT, hnd_put_thresholds);
    coap_add_resource(ctx, r_cfg);

    coap_resource_t *r_reboot = coap_resource_init(
        coap_make_str_const("sys/reboot"), 0);
    coap_register_handler(r_reboot, COAP_REQUEST_PUT, hnd_put_reboot);
    coap_add_resource(ctx, r_reboot);

    ESP_LOGI(TAG, "Servidor CoAP en UDP/%d", COAP_PORT);
    ESP_LOGI(TAG, "  /env/temp        GET + Observe");
    ESP_LOGI(TAG, "  /env/hum         GET + Observe");
    ESP_LOGI(TAG, "  /sys/info        GET");
    ESP_LOGI(TAG, "  /sys/health      GET");
    ESP_LOGI(TAG, "  /config/thresholds PUT");
    ESP_LOGI(TAG, "  /sys/reboot      PUT");

    g_last_notify_tick = xTaskGetTickCount();

    while (1) {
        coap_io_process(ctx, 200);
    }
}

esp_err_t coap_server_start(void)
{
    BaseType_t ret = xTaskCreate(coap_server_task, "coap_server",
                                 6144, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea coap_server");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void coap_notify_temp(void)
{
    if (g_res_temp) {
        coap_resource_notify_observers(g_res_temp, NULL);
    }
}

void coap_notify_hum(void)
{
    if (g_res_hum) {
        coap_resource_notify_observers(g_res_hum, NULL);
    }
}

void coap_check_and_notify(float temp_c, uint8_t hum_pct)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t since_last = now - g_last_notify_tick;
    bool hubo_notificacion = false;

    float diff_temp = fabsf(temp_c - g_last_temp);
    if (diff_temp > g_config.temp_threshold_c) {
        g_last_temp = temp_c;
        coap_notify_temp();
        ESP_LOGI(TAG, "notify temp: %.2f C", temp_c);
        hubo_notificacion = true;
    }

    int16_t diff_hum = abs((int16_t)hum_pct - (int16_t)g_last_hum);
    if (diff_hum > g_config.hum_threshold_pct) {
        g_last_hum = hum_pct;
        coap_notify_hum();
        ESP_LOGI(TAG, "notify hum: %u %%", hum_pct);
        hubo_notificacion = true;
    }

    if (!hubo_notificacion &&
        (since_last >= pdMS_TO_TICKS(g_config.heartbeat_s * 1000))) {
        g_last_temp = temp_c;
        g_last_hum  = hum_pct;
        coap_notify_temp();
        coap_notify_hum();
        ESP_LOGI(TAG, "heartbeat: T=%.2f H=%u", temp_c, hum_pct);
        hubo_notificacion = true;
    }

    if (hubo_notificacion) {
        g_last_notify_tick = now;
    }
}
