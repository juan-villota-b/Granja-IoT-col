#include "push_client.h"

#include <arpa/inet.h>
#include <string.h>

#include "esp_log.h"
#include "esp_openthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"

#include "node_config.h"

static const char *TAG = "push";

static uint32_t f32_to_u32(float f) { uint32_t x; memcpy(&x, &f, sizeof(x)); return x; }

static size_t encode_cbor_first(uint8_t out[80], sensor_temp_t *t, int8_t rssi, uint32_t up)
{
    size_t p = 0;
    out[p++] = 0xA6;
    out[p++] = 0x62; out[p++] = 'i'; out[p++] = 'd';
    size_t nl = strlen(NODE_ID);
    out[p++] = (uint8_t)(0x60 | nl);
    memcpy(out + p, NODE_ID, nl); p += nl;
    out[p++] = 0x62; out[p++] = 't'; out[p++] = 'p';
    size_t tl = strlen(NODE_TYPE);
    out[p++] = (uint8_t)(0x60 | tl);
    memcpy(out + p, NODE_TYPE, tl); p += tl;
    out[p++] = 0x61; out[p++] = 'v';
    size_t vl = strlen(FW_VERSION);
    out[p++] = (uint8_t)(0x60 | vl);
    memcpy(out + p, FW_VERSION, vl); p += vl;
    out[p++] = 0x61; out[p++] = 't';
    out[p++] = 0xF9;
    uint32_t x = f32_to_u32(t->temperatura_c);
    uint16_t sign = (x >> 16) & 0x8000;
    int16_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t mant = (x >> 13) & 0x3FF;
    uint16_t tv;
    if (exp <= 0) tv = (uint16_t)sign;
    else if (exp >= 31) tv = (uint16_t)(sign | 0x7C00);
    else tv = sign | ((uint16_t)exp << 10) | mant;
    out[p++] = (uint8_t)(tv >> 8); out[p++] = (uint8_t)(tv & 0xFF);
    out[p++] = 0x61; out[p++] = 'r';
    out[p++] = 0x38;
    out[p++] = (uint8_t)((-(int16_t)rssi) - 1);
    out[p++] = 0x61; out[p++] = 'u';
    out[p++] = 0x1A;
    out[p++] = (uint8_t)(up >> 24);
    out[p++] = (uint8_t)((up >> 16) & 0xFF);
    out[p++] = (uint8_t)((up >> 8) & 0xFF);
    out[p++] = (uint8_t)(up & 0xFF);
    return p;
}

static size_t encode_cbor_push(uint8_t out[48], sensor_temp_t *t, int8_t rssi, uint32_t up)
{
    size_t p = 0;
    out[p++] = 0xA4;
    out[p++] = 0x62; out[p++] = 'i'; out[p++] = 'd';
    size_t nl = strlen(NODE_ID);
    out[p++] = (uint8_t)(0x60 | nl);
    memcpy(out + p, NODE_ID, nl); p += nl;
    out[p++] = 0x61; out[p++] = 't';
    out[p++] = 0xF9;
    uint32_t x = f32_to_u32(t->temperatura_c);
    uint16_t sign = (x >> 16) & 0x8000;
    int16_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t mant = (x >> 13) & 0x3FF;
    uint16_t tv;
    if (exp <= 0) tv = (uint16_t)sign;
    else if (exp >= 31) tv = (uint16_t)(sign | 0x7C00);
    else tv = sign | ((uint16_t)exp << 10) | mant;
    out[p++] = (uint8_t)(tv >> 8); out[p++] = (uint8_t)(tv & 0xFF);
    out[p++] = 0x61; out[p++] = 'r';
    out[p++] = 0x38;
    out[p++] = (uint8_t)((-(int16_t)rssi) - 1);
    out[p++] = 0x61; out[p++] = 'u';
    out[p++] = 0x1A;
    out[p++] = (uint8_t)(up >> 24);
    out[p++] = (uint8_t)((up >> 16) & 0xFF);
    out[p++] = (uint8_t)((up >> 8) & 0xFF);
    out[p++] = (uint8_t)(up & 0xFF);
    return p;
}

static int8_t get_rssi(void)
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return -65;
    int8_t rssi;
    if (otThreadGetParentLastRssi(ot, &rssi) == OT_ERROR_NONE && rssi < 0 && rssi > -128)
        return rssi;
    if (otThreadGetParentAverageRssi(ot, &rssi) == OT_ERROR_NONE && rssi < 0 && rssi > -128)
        return rssi;
    return -65;
}

static bool g_got_ack = false;

static coap_response_t push_handler(coap_session_t *s, const coap_pdu_t *sent,
                                    const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    (void)s; (void)sent; (void)mid;
    if (rcvd) {
        unsigned int cls = coap_pdu_get_code(rcvd) >> 5;
        if (cls == 2) {
            g_got_ack = true;
            ESP_LOGI(TAG, "ACK 2.xx del Bridge");
        }
    }
    return COAP_RESPONSE_OK;
}

esp_err_t push_telemetry(sensor_temp_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first)
{
    if (rssi == 0) rssi = get_rssi();

    otInstance *ot = esp_openthread_get_instance();
    if (!ot) { ESP_LOGW(TAG, "Sin instancia OT"); return ESP_FAIL; }

    coap_address_t dst;
    coap_address_init(&dst);
    dst.addr.sin6.sin6_family = AF_INET6;
    dst.addr.sin6.sin6_port   = htons(BRIDGE_PORT);
    if (inet_pton(AF_INET6, BRIDGE_IPV6, &dst.addr.sin6.sin6_addr) != 1) {
        ESP_LOGE(TAG, "inet_pton: %s", BRIDGE_IPV6);
        return ESP_FAIL;
    }

    uint8_t payload[80];
    size_t plen;
    if (is_first)
        plen = encode_cbor_first(payload, lectura, rssi, uptime_s);
    else
        plen = encode_cbor_push(payload, lectura, rssi, uptime_s);

    static unsigned int msg_id = 100;
    int max_retries = is_first ? REGISTER_MAX_RETRIES : 2;
    int wait_timeout_ms = is_first ? REGISTER_RETRY_MS : 1500;

    ESP_LOGI(TAG, "Push CON /readings → [%s]:%d (intentos=%d timeout=%dms)", BRIDGE_IPV6, BRIDGE_PORT, max_retries, wait_timeout_ms);

    for (int attempt = 0; attempt < max_retries; attempt++) {
        coap_context_t *ctx = coap_new_context(NULL);
        if (!ctx) continue;
        coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
        coap_register_response_handler(ctx, push_handler);

        coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
        if (!sess) { coap_free_context(ctx); continue; }

        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
                                         ++msg_id, coap_session_max_pdu_size(sess));
        if (!pdu) { coap_session_release(sess); coap_free_context(ctx); continue; }

        uint8_t tok = 0xA1;
        coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8, (const uint8_t *)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(enc, sizeof(enc),
                                             COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);

        g_got_ack = false;
        coap_send(sess, pdu);

        TickType_t start = xTaskGetTickCount();
        while (!g_got_ack &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(wait_timeout_ms)) {
            coap_io_process(ctx, 100);
        }

        coap_session_release(sess);
        coap_free_context(ctx);

        if (g_got_ack) {
            ESP_LOGI(TAG, "Push exitoso intento %d — T=%.1fC", attempt + 1, (double)lectura->temperatura_c);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Push intento %d/%d sin ACK", attempt + 1, max_retries);
    }

    ESP_LOGE(TAG, "Push fallido tras %d intentos", max_retries);
    return ESP_FAIL;
}
