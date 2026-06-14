#include "push_client.h"

#include <arpa/inet.h>
#include <string.h>

#include "esp_log.h"
#include "esp_openthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"

#include "node_config.h"
#include "config.h"

static const char *TAG = "push";

static uint32_t f32_to_u32(float f) { uint32_t x; memcpy(&x, &f, sizeof(x)); return x; }

/* ── CBOR parser ────────────────────────────────────────────────── */

bool cbor_get_string(const uint8_t *data, size_t len,
                     const char *key, char *value, size_t max_len)
{
    size_t kl = strlen(key);
    size_t p = 0;
    while (p < len) {
        uint8_t head = data[p];
        unsigned int mt = head >> 5;
        if (mt == 3 && (head & 0x1F) == (unsigned)kl && p + 1 + kl <= len) {
            p++;
            if (memcmp(data + p, key, kl) == 0) {
                p += kl;
                if (p >= len) return false;
                uint8_t vhead = data[p++];
                unsigned int vmt = vhead >> 5;
                size_t vl = vhead & 0x1F;
                if (vl < 24) {}
                else if (vl == 24 && p < len) { vl = data[p++]; }
                else if (vl == 25 && p + 1 < len) { vl = (data[p] << 8) | data[p + 1]; p += 2; }
                else return false;
                if (vmt == 3 && vl < max_len && p + vl <= len) {
                    memcpy(value, data + p, vl);
                    value[vl] = '\0';
                    return true;
                }
            }
        }
        if (data[p] == 0xFF) break;
        if ((head & 0x1F) < 24) p++;
        else if ((head & 0x1F) == 24) p += 2;
        else if ((head & 0x1F) == 25) p += 3;
        else p += 2;
    }
    return false;
}

/* ── CBOR: provisioning payload ───────────────────────────────── ──
   {prov_key:"XXXX", state:"pending"}                              */

static size_t encode_prov_cbor(const char *prov_key, uint8_t out[64])
{
    size_t p = 0, pk_len = strlen(prov_key);
    out[p++] = 0xA2;
    out[p++] = 0x68;
    memcpy(out + p, "prov_key", 8); p += 8;
    if (pk_len < 24) { out[p++] = (uint8_t)(0x60 | pk_len); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)pk_len; }
    memcpy(out + p, prov_key, pk_len); p += pk_len;
    out[p++] = 0x65;
    memcpy(out + p, "state", 5); p += 5;
    out[p++] = 0x67;
    memcpy(out + p, "pending", 7); p += 7;
    return p;
}

/* ── CBOR: telemetry payload ─────────────────────────────────────
   {prov_key:"XXXX", h:float16, r:negint8, u:uint32}               */

static size_t encode_cbor_telemetry(const char *prov_key, sensor_data_t *t,
                                     int8_t rssi, uint32_t up, uint8_t out[80])
{
    size_t p = 0, pk_len = strlen(prov_key);
    out[p++] = 0xA4;

    out[p++] = 0x68;
    memcpy(out + p, "prov_key", 8); p += 8;
    if (pk_len < 24) { out[p++] = (uint8_t)(0x60 | pk_len); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)pk_len; }
    memcpy(out + p, prov_key, pk_len); p += pk_len;

    out[p++] = 0x61; out[p++] = 'h';
    out[p++] = 0xF9;
    uint32_t x = f32_to_u32(t->humedad);
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

/* ── RSSI ──────────────────────────────────────────────────────── */

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

/* ── CoAP helpers ──────────────────────────────────────────────── */

static bool g_got_ack = false;
static uint8_t g_resp_data[256];
static size_t  g_resp_len = 0;

static coap_response_t push_handler(coap_session_t *s, const coap_pdu_t *sent,
                                    const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    (void)s; (void)sent; (void)mid;
    if (!rcvd) return COAP_RESPONSE_OK;
    unsigned int cls = coap_pdu_get_code(rcvd) >> 5;
    if (cls == 2) {
        g_got_ack = true;
        size_t dlen = 0;
        const uint8_t *d = NULL;
        coap_get_data(rcvd, &dlen, &d);
        if (d && dlen > 0 && dlen < sizeof(g_resp_data)) {
            memcpy(g_resp_data, d, dlen);
            g_resp_len = dlen;
        }
    }
    return COAP_RESPONSE_OK;
}

static coap_address_t bridge_addr(void)
{
    coap_address_t dst;
    coap_address_init(&dst);
    dst.addr.sin6.sin6_family = AF_INET6;
    dst.addr.sin6.sin6_port   = htons(BRIDGE_PORT);
    inet_pton(AF_INET6, BRIDGE_IPV6, &dst.addr.sin6.sin6_addr);
    return dst;
}

/* ── Provisioning ──────────────────────────────────────────────── */

esp_err_t provisioning_send(const char *prov_key)
{
    ESP_LOGI(TAG, "Provisioning CON /readings key=%.8s", prov_key);

    uint8_t payload[64];
    size_t plen = encode_prov_cbor(prov_key, payload);

    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return ESP_FAIL;

    coap_address_t dst = bridge_addr();
    static unsigned int msg_id = 300;

    for (int attempt = 0; attempt < REGISTER_MAX_RETRIES; attempt++) {
        coap_context_t *ctx = coap_new_context(NULL);
        if (!ctx) continue;
        coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
        coap_register_response_handler(ctx, push_handler);

        coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
        if (!sess) { coap_free_context(ctx); continue; }

        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
            ++msg_id, coap_session_max_pdu_size(sess));
        if (!pdu) { coap_session_release(sess); coap_free_context(ctx); continue; }

        uint8_t tok = 0xB0;
        coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8, (const uint8_t *)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
            coap_encode_var_safe(enc, sizeof(enc), COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);

        g_got_ack = false;
        g_resp_len = 0;
        coap_send(sess, pdu);

        TickType_t start = xTaskGetTickCount();
        while (!g_got_ack && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(REGISTER_RETRY_MS))
            coap_io_process(ctx, 100);

        coap_session_release(sess);
        coap_free_context(ctx);

        if (g_got_ack && g_resp_len > 0) {
            char cmd[16] = {0};
            cbor_get_string(g_resp_data, g_resp_len, "cmd", cmd, sizeof(cmd));
            if (strcmp(cmd, "start") == 0) {
                ESP_LOGI(TAG, "Provisioning OK");
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return ESP_FAIL;
}

/* ── Telemetry push ────────────────────────────────────────────── */

esp_err_t push_telemetry(const char *prov_key, sensor_data_t *lectura,
                          int8_t rssi, uint32_t uptime_s, bool is_first)
{
    if (rssi == 0) rssi = get_rssi();

    otInstance *ot = esp_openthread_get_instance();
    if (!ot) { ESP_LOGW(TAG, "Sin instancia OT"); return ESP_FAIL; }

    uint8_t payload[80];
    size_t plen = encode_cbor_telemetry(prov_key, lectura, rssi, uptime_s, payload);

    coap_address_t dst = bridge_addr();
    static unsigned int msg_id = 100;

    ESP_LOGI(TAG, "Push CON /readings (intentos=%d)", is_first ? 3 : 2);

    for (int attempt = 0; attempt < (is_first ? REGISTER_MAX_RETRIES : 2); attempt++) {
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
            coap_encode_var_safe(enc, sizeof(enc), COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);

        g_got_ack = false;
        coap_send(sess, pdu);

        TickType_t start = xTaskGetTickCount();
        int wait_ms = is_first ? REGISTER_RETRY_MS : 1500;
        while (!g_got_ack && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(wait_ms))
            coap_io_process(ctx, 100);

        coap_session_release(sess);
        coap_free_context(ctx);

        if (g_got_ack) {
            ESP_LOGI(TAG, "Push OK intento %d", attempt + 1);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Push intento %d/%d sin ACK", attempt + 1, is_first ? REGISTER_MAX_RETRIES : 2);
    }
    return ESP_FAIL;
}
