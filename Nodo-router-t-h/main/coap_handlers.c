#include "coap_handlers.h"

#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"

#include "node_config.h"
#include "config.h"
#include "sensor_sim.h"

static const char *TAG = "coap";

/* Recursos observables globales para poder notificar desde el loop principal */
static coap_resource_t *g_res_temp = NULL;
static coap_resource_t *g_res_hum  = NULL;

/* Últimos valores notificados (para supresión por umbral) */
static float    g_last_temp = TEMP_BASELINE;
static uint8_t  g_last_hum  = HUM_BASELINE;

/* Ticks de la última notificación (heartbeat) */
static TickType_t g_last_notify_tick = 0;

/*
 * Convierte float32 a float16 (IEEE 754 half-precision).
 * Formato: 1 bit signo, 5 bits exponente, 10 bits mantisa.
 */
static uint16_t float32_to_float16(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign  = (x >> 16) & 0x8000;
    int32_t  exp   = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant  = (x >> 13) & 0x3FF;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

/*
 * Codifica {t: float16} en CBOR (6 bytes).
 * CDDL: env-temp-reading = { t: float16 }
 */
static size_t encode_temp_cbor(float value, uint8_t out[6]) {
    uint16_t h = float32_to_float16(value);
    out[0] = 0xA1;
    out[1] = 0x61; out[2] = 0x74;   /* "t" */
    out[3] = 0xF9;                   /* float16 tag */
    out[4] = (uint8_t)(h >> 8);
    out[5] = (uint8_t)(h & 0xFF);
    return 6;
}

/*
 * Codifica {h: uint8} en CBOR (4 bytes).
 * CDDL: env-hum-reading = { h: uint8 }
 */
static size_t encode_hum_cbor(uint8_t value, uint8_t out[4]) {
    out[0] = 0xA1;
    out[1] = 0x61; out[2] = 0x68;   /* "h" */
    out[3] = value;
    return 4;
}

/*
 * Codifica {id: text, zone: text, type: text, x: float, y: float, ver: text}
 * en CBOR. Formato escalable — el gateway ignora campos que no entienda.
 */
static size_t encode_info_cbor(uint8_t *out, size_t max_len) {
    /* Por simplicidad inicial: JSON plano dentro de CBOR byte string */
    /* En producción usar biblioteca CBOR (libcoap o tinycbor) */
    /* Por ahora retornamos texto plano compatible */
    return snprintf((char *)out, max_len,
        "{\"id\":\"%s\",\"zone\":\"%s\",\"type\":\"%s\","
        "\"x\":%.1f,\"y\":%.1f,\"ver\":\"%s\"}",
        NODE_ID, ZONE_ID, NODE_TYPE, POS_X, POS_Y, FW_VERSION);
}

/*
 * Codifica {batt: uint16, rssi: int8, up: uint32} en texto/CBOR.
 */
static size_t encode_health_cbor(uint8_t *out, size_t max_len) {
    /* RSSI simulado — en producción leer de otPlatRadioGetRssi() */
    int8_t  rssi_sim = -65;
    uint16_t batt_sim = 3100;
    uint32_t uptime_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    return snprintf((char *)out, max_len,
        "{\"batt\":%u,\"rssi\":%d,\"up\":%lu}",
        batt_sim, rssi_sim, (unsigned long)uptime_s);
}

/* ====================== HANDLERS COAP ======================== */

/*
 * GET /env/temp → responde con la temperatura actual en CBOR.
 * Si el cliente incluyó Observe:0, queda subscripto para notificaciones NON.
 */
static void hnd_get_temp(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query, coap_pdu_t *response) {
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

    ESP_LOGD(TAG, "GET /env/temp → %.2f °C", lectura.temperatura_c);
}

/*
 * GET /env/hum → responde con la humedad actual en CBOR.
 * Observe disponible (NON).
 */
static void hnd_get_hum(coap_resource_t *resource, coap_session_t *session,
                        const coap_pdu_t *request,
                        const coap_string_t *query, coap_pdu_t *response) {
    (void)session; (void)request; (void)query;

    sensor_lectura_t lectura = sensor_leer();

    uint8_t buf[4];
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

    ESP_LOGD(TAG, "GET /env/hum → %u %%", lectura.humedad_pct);
}

/*
 * GET /sys/info → devuelve la identidad del nodo (CON).
 * El gateway consulta esto cuando descubre un nuevo nodo en la red.
 */
static void hnd_get_info(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query, coap_pdu_t *response) {
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

/*
 * GET /sys/health → devuelve batería, RSSI y uptime (CON).
 */
static void hnd_get_health(coap_resource_t *resource, coap_session_t *session,
                           const coap_pdu_t *request,
                           const coap_string_t *query, coap_pdu_t *response) {
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

/* ============== HANDLERS DE CONFIGURACIÓN (PUT) ================== */

/*
 * Parsea el payload JSON de configuración y extrae los valores.
 * Formato esperado: {"tt":0.5,"ht":3,"hb":45}
 * Usa búsqueda simple de strings para evitar dependencia con cJSON.
 */
static bool parse_config_payload(const uint8_t *data, size_t len,
                                 float *out_tt, uint8_t *out_ht, uint16_t *out_hb) {
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

/*
 * PUT /config/thresholds → actualiza umbrales y heartbeat.
 * Payload: {"tt":0.5,"ht":3,"hb":45}
 * Responde 2.04 Changed si OK, 4.00 Bad Request si inválido.
 * Guarda en NVS automáticamente.
 */
static void hnd_put_thresholds(coap_resource_t *resource, coap_session_t *session,
                               const coap_pdu_t *request,
                               const coap_string_t *query, coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;

    size_t len;
    const uint8_t *data;
    coap_get_data(request, &len, &data);

    if (!data || len == 0) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        ESP_LOGW(TAG, "PUT /config/thresholds: payload vacío");
        return;
    }

    float new_tt = g_config.temp_threshold_c;
    uint8_t new_ht = g_config.hum_threshold_pct;
    uint16_t new_hb = g_config.heartbeat_s;

    parse_config_payload(data, len, &new_tt, &new_ht, &new_hb);

    /* Validar rangos */
    if (new_tt < 0.1f || new_tt > 10.0f) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        ESP_LOGW(TAG, "PUT /config/thresholds: temp_threshold %.2f fuera de rango", new_tt);
        return;
    }
    if (new_ht < 1 || new_ht > 50) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        ESP_LOGW(TAG, "PUT /config/thresholds: hum_threshold %u fuera de rango", new_ht);
        return;
    }
    if (new_hb < 10 || new_hb > 600) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        ESP_LOGW(TAG, "PUT /config/thresholds: heartbeat %u fuera de rango", new_hb);
        return;
    }

    /* Aplicar */
    g_config.temp_threshold_c  = new_tt;
    g_config.hum_threshold_pct = new_ht;
    g_config.heartbeat_s       = new_hb;

    /* Persistir en NVS */
    esp_err_t err = config_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config_save falló: %s (valores aplicados pero no persistidos)",
                 esp_err_to_name(err));
    }

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGI(TAG, "PUT /config/thresholds → tt=%.1f ht=%u hb=%u %s",
             new_tt, new_ht, new_hb,
             (err == ESP_OK) ? "(NVS OK)" : "(NVS FAIL)");
}

/*
 * PUT /sys/reboot → reinicia el ESP32-C6.
 * Responde 2.04 Changed y luego ejecuta esp_restart() con un pequeño delay.
 */
static void hnd_put_reboot(coap_resource_t *resource, coap_session_t *session,
                           const coap_pdu_t *request,
                           const coap_string_t *query, coap_pdu_t *response) {
    (void)resource; (void)session; (void)request; (void)query;

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    ESP_LOGW(TAG, "PUT /sys/reboot → REINICIANDO...");

    /*
     * La respuesta se envía antes de reiniciar.
     * vTaskDelay da tiempo a que el ACK salga.
     */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

/* ==================== TAREA DEL SERVIDOR COAP ===================== */

static void coap_server_task(void *pvParameters) {
    (void)pvParameters;

    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin6.sin6_family = AF_INET6;
    addr.addr.sin6.sin6_port   = htons(COAP_PORT);
    addr.addr.sin6.sin6_addr   = in6addr_any;

    coap_set_log_level(COAP_LOG_WARN);

    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) {
        ESP_LOGE(TAG, "coap_new_context falló");
        vTaskDelete(NULL);
        return;
    }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    if (!coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP)) {
        ESP_LOGE(TAG, "coap_new_endpoint falló en puerto %d", COAP_PORT);
        coap_free_context(ctx);
        vTaskDelete(NULL);
        return;
    }

    /* Recurso /env/temp (GET + Observe, notificaciones NON) */
    g_res_temp = coap_resource_init(coap_make_str_const("env/temp"), 0);
    coap_register_handler(g_res_temp, COAP_REQUEST_GET, hnd_get_temp);
    coap_resource_set_get_observable(g_res_temp, 1);
    coap_add_resource(ctx, g_res_temp);

    /* Recurso /env/hum (GET + Observe, notificaciones NON) */
    g_res_hum = coap_resource_init(coap_make_str_const("env/hum"), 0);
    coap_register_handler(g_res_hum, COAP_REQUEST_GET, hnd_get_hum);
    coap_resource_set_get_observable(g_res_hum, 1);
    coap_add_resource(ctx, g_res_hum);

    /* Recurso /sys/info (GET, CON — solo consulta del gateway) */
    coap_resource_t *r_info = coap_resource_init(
        coap_make_str_const("sys/info"), 0);
    coap_register_handler(r_info, COAP_REQUEST_GET, hnd_get_info);
    coap_add_resource(ctx, r_info);

    /* Recurso /sys/health (GET, CON) */
    coap_resource_t *r_health = coap_resource_init(
        coap_make_str_const("sys/health"), 0);
    coap_register_handler(r_health, COAP_REQUEST_GET, hnd_get_health);
    coap_add_resource(ctx, r_health);

    /* Recurso /config/thresholds (PUT, CON — configuración remota) */
    coap_resource_t *r_cfg = coap_resource_init(
        coap_make_str_const("config/thresholds"), 0);
    coap_register_handler(r_cfg, COAP_REQUEST_PUT, hnd_put_thresholds);
    coap_add_resource(ctx, r_cfg);

    /* Recurso /sys/reboot (PUT, CON — reinicio remoto) */
    coap_resource_t *r_reboot = coap_resource_init(
        coap_make_str_const("sys/reboot"), 0);
    coap_register_handler(r_reboot, COAP_REQUEST_PUT, hnd_put_reboot);
    coap_add_resource(ctx, r_reboot);

    ESP_LOGI(TAG, "Servidor CoAP escuchando en UDP/%d", COAP_PORT);
    ESP_LOGI(TAG, "  /env/temp        (GET + Observe → NON)");
    ESP_LOGI(TAG, "  /env/hum         (GET + Observe → NON)");
    ESP_LOGI(TAG, "  /sys/info        (GET → CON)");
    ESP_LOGI(TAG, "  /sys/health      (GET → CON)");
    ESP_LOGI(TAG, "  /config/thresholds (PUT → CON)");
    ESP_LOGI(TAG, "  /sys/reboot      (PUT → CON)");

    g_last_notify_tick = xTaskGetTickCount();

    while (1) {
        coap_io_process(ctx, 200);
    }
}

/* ==================== API PÚBLICA ======================== */

esp_err_t coap_server_start(void) {
    BaseType_t ret = xTaskCreate(coap_server_task, "coap_server",
                                 6144, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea coap_server");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void coap_notify_temp(void) {
    if (g_res_temp) {
        coap_resource_notify_observers(g_res_temp, NULL);
    }
}

void coap_notify_hum(void) {
    if (g_res_hum) {
        coap_resource_notify_observers(g_res_hum, NULL);
    }
}

/*
 * Consulta si se debe notificar según umbrales y heartbeat.
 * Llamada desde el loop principal de app_main.
 */
void coap_check_and_notify(float temp_c, uint8_t hum_pct) {
    TickType_t now = xTaskGetTickCount();
    TickType_t since_last = now - g_last_notify_tick;
    bool hubo_notificacion = false;

    /* Umbral de temperatura */
    float diff_temp = fabsf(temp_c - g_last_temp);
    if (diff_temp > g_config.temp_threshold_c) {
        g_last_temp = temp_c;
        coap_notify_temp();
        ESP_LOGI(TAG, "notify temp: %.2f °C (Δ=%.2f)", temp_c, diff_temp);
        hubo_notificacion = true;
    }

    /* Umbral de humedad */
    int16_t diff_hum = abs((int16_t)hum_pct - (int16_t)g_last_hum);
    if (diff_hum > g_config.hum_threshold_pct) {
        g_last_hum = hum_pct;
        coap_notify_hum();
        ESP_LOGI(TAG, "notify hum: %u %% (Δ=%d)", hum_pct, diff_hum);
        hubo_notificacion = true;
    }

    /* Heartbeat: si no se notificó nada en g_config.heartbeat_s, forzar */
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
