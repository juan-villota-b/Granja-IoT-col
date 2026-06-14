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
#include "actuador.h"

static const char *TAG = "push";

static uint32_t f32_to_u32(float f) { uint32_t x; memcpy(&x, &f, sizeof(x)); return x; }

static size_t encode_cbor_first(uint8_t out[80], sensor_temp_t *t, int8_t rssi, uint32_t up)
{
    size_t p = 0;
    out[p++] = 0xA8;
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
    out[p++] = 0x61; out[p++] = TELEMETRY_KEY;
    if (TELEMETRY_FMT == 'f') {
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
    } else if (TELEMETRY_FMT == 'u') {
        out[p++] = 0x18;
        out[p++] = t->humedad_pct;
    } else if (TELEMETRY_FMT == 'U') {
        out[p++] = 0x19;
        out[p++] = (uint8_t)(t->luz_lux >> 8);
        out[p++] = (uint8_t)(t->luz_lux & 0xFF);
    }
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'a'; out[p++] = 't';
    out[p++] = 0xFA;
    uint32_t lat_u32 = f32_to_u32(g_config.lat);
    out[p++] = (uint8_t)(lat_u32 >> 24);
    out[p++] = (uint8_t)((lat_u32 >> 16) & 0xFF);
    out[p++] = (uint8_t)((lat_u32 >> 8) & 0xFF);
    out[p++] = (uint8_t)(lat_u32 & 0xFF);
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'n'; out[p++] = 'g';
    out[p++] = 0xFA;
    uint32_t lng_u32 = f32_to_u32(g_config.lng);
    out[p++] = (uint8_t)(lng_u32 >> 24);
    out[p++] = (uint8_t)((lng_u32 >> 16) & 0xFF);
    out[p++] = (uint8_t)((lng_u32 >> 8) & 0xFF);
    out[p++] = (uint8_t)(lng_u32 & 0xFF);
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

static size_t encode_cbor_push(uint8_t out[64], sensor_temp_t *t, int8_t rssi, uint32_t up)
{
    size_t p = 0;
    out[p++] = 0xA6;
    out[p++] = 0x62; out[p++] = 'i'; out[p++] = 'd';
    size_t nl = strlen(NODE_ID);
    out[p++] = (uint8_t)(0x60 | nl);
    memcpy(out + p, NODE_ID, nl); p += nl;
    out[p++] = 0x61; out[p++] = TELEMETRY_KEY;
    if (TELEMETRY_FMT == 'f') {
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
    } else if (TELEMETRY_FMT == 'u') {
        out[p++] = 0x18;
        out[p++] = t->humedad_pct;
    } else if (TELEMETRY_FMT == 'U') {
        out[p++] = 0x19;
        out[p++] = (uint8_t)(t->luz_lux >> 8);
        out[p++] = (uint8_t)(t->luz_lux & 0xFF);
    }
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'a'; out[p++] = 't';
    out[p++] = 0xFA;
    uint32_t lat_u32 = f32_to_u32(g_config.lat);
    out[p++] = (uint8_t)(lat_u32 >> 24);
    out[p++] = (uint8_t)((lat_u32 >> 16) & 0xFF);
    out[p++] = (uint8_t)((lat_u32 >> 8) & 0xFF);
    out[p++] = (uint8_t)(lat_u32 & 0xFF);
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'n'; out[p++] = 'g';
    out[p++] = 0xFA;
    uint32_t lng_u32 = f32_to_u32(g_config.lng);
    out[p++] = (uint8_t)(lng_u32 >> 24);
    out[p++] = (uint8_t)((lng_u32 >> 16) & 0xFF);
    out[p++] = (uint8_t)((lng_u32 >> 8) & 0xFF);
    out[p++] = (uint8_t)(lng_u32 & 0xFF);
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
static int8_t g_received_valve = -1;

static coap_response_t push_handler(coap_session_t *s, const coap_pdu_t *sent,
                                    const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    (void)s; (void)sent; (void)mid;
    if (rcvd) {
        unsigned int cls = coap_pdu_get_code(rcvd) >> 5;
        if (cls == 2) {
            g_got_ack = true;

            size_t len;
            const uint8_t *data;
            coap_get_data(rcvd, &len, &data);
            if (len >= 4 && data[0] == 0xA1 && data[1] == 0x61 && data[2] == 0x76) {
                g_received_valve = (int8_t)data[3];
                ESP_LOGI(TAG, "Comando recibido: v=%d", g_received_valve);
            }
        }
    }
    return COAP_RESPONSE_OK;
}

esp_err_t push_telemetry(sensor_temp_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first, int8_t *out_valve)
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

    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) return ESP_FAIL;
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
    coap_register_response_handler(ctx, push_handler);

    coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!sess) { coap_free_context(ctx); return ESP_FAIL; }

    esp_err_t ret = ESP_FAIL;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
                                         ++msg_id, coap_session_max_pdu_size(sess));
        if (!pdu) continue;

        uint8_t tok = 0xA1;
        coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8, (const uint8_t *)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(enc, sizeof(enc),
                                             COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);

        g_got_ack = false;
        g_received_valve = -1;
        coap_send(sess, pdu);

        TickType_t start = xTaskGetTickCount();
        while (!g_got_ack &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(wait_timeout_ms)) {
            coap_io_process(ctx, 100);
        }

        if (g_got_ack) {
            if (out_valve && g_received_valve < 0) {
                TickType_t t1 = xTaskGetTickCount();
                while (g_received_valve < 0 &&
                       (xTaskGetTickCount() - t1) < pdMS_TO_TICKS(300)) {
                    coap_io_process(ctx, 50);
                }
            }
            if (out_valve && g_received_valve >= 0)
                *out_valve = g_received_valve;
            ESP_LOGI(TAG, "Push exitoso intento %d — v=%d", attempt + 1, act_get());
            ret = ESP_OK;
            break;
        }
        ESP_LOGW(TAG, "Push intento %d/%d sin ACK", attempt + 1, max_retries);
    }

    coap_session_release(sess);
    coap_free_context(ctx);

    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Push fallido tras %d intentos", max_retries);
    return ret;
}

/* ── Provisioning ── */
#define PROV_KEY_LEN 16

bool cbor_get_string(const uint8_t *data, size_t len,
                     const char *key, char *value, size_t max_len)
{
    size_t kl = strlen(key), p = 0;
    while (p < len) {
        uint8_t head = data[p];
        unsigned int mt = head >> 5;
        if (mt == 3 && (head & 0x1F) == (unsigned)kl && p + 1 + kl <= len) {
            p++; if (memcmp(data + p, key, kl)) { p += kl; continue; }
            p += kl; if (p >= len) return false;
            uint8_t vh = data[p++]; unsigned vm = vh >> 5;
            size_t vl = vh & 0x1F;
            if (vl < 24) {}
            else if (vl == 24 && p < len) vl = data[p++];
            else if (vl == 25 && p+1 < len) { vl = (data[p]<<8)|data[p+1]; p+=2; }
            else return false;
            if (vm == 3 && vl < max_len && p + vl <= len) {
                memcpy(value, data + p, vl); value[vl] = 0; return true;
            }
        }
        if (data[p] == 0xFF) break;
        if ((head & 0x1F) < 24) p++;
        else if ((head & 0x1F) == 24) p += 2;
        else if ((head & 0x1F) == 25) p += 3; else p += 2;
    }
    return false;
}

static size_t encode_prov_cbor(const char *pk, uint8_t out[64])
{
    size_t p = 0, l = strlen(pk);
    out[p++] = 0xA2; out[p++] = 0x68;
    memcpy(out+p, "prov_key", 8); p += 8;
    if (l < 24) out[p++] = (uint8_t)(0x60|l); else { out[p++]=0x78; out[p++]=(uint8_t)l; }
    memcpy(out+p, pk, l); p += l;
    out[p++] = 0x65; memcpy(out+p, "state", 5); p += 5;
    out[p++] = 0x67; memcpy(out+p, "pending", 7); p += 7;
    return p;
}

static uint8_t _prov_resp[256];
static size_t  _prov_resp_len = 0;
static bool    _prov_ack = false;

static coap_response_t prov_resp_handler(coap_session_t *s, const coap_pdu_t *sent,
                                          const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    (void)s; (void)sent; (void)mid;
    if (!rcvd) return COAP_RESPONSE_OK;
    if ((coap_pdu_get_code(rcvd) >> 5) == 2) {
        _prov_ack = true;
        size_t dlen = 0; const uint8_t *d = NULL;
        coap_get_data(rcvd, &dlen, &d);
        if (d && dlen < sizeof(_prov_resp)) { memcpy(_prov_resp, d, dlen); _prov_resp_len = dlen; }
    }
    return COAP_RESPONSE_OK;
}

esp_err_t provisioning_send(const char *prov_key)
{
    ESP_LOGI("prov", "Key=%.8s", prov_key);
    uint8_t payload[64];
    size_t plen = encode_prov_cbor(prov_key, payload);

    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return ESP_FAIL;

    coap_address_t dst; coap_address_init(&dst);
    dst.addr.sin6.sin6_family = AF_INET6;
    dst.addr.sin6.sin6_port = htons(BRIDGE_PORT);
    if (inet_pton(AF_INET6, BRIDGE_IPV6, &dst.addr.sin6.sin6_addr) != 1) return ESP_FAIL;

    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) return ESP_FAIL;
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
    coap_register_response_handler(ctx, prov_resp_handler);
    coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!sess) { coap_free_context(ctx); return ESP_FAIL; }

    static unsigned int mid = 300;
    for (int a = 0; a < REGISTER_MAX_RETRIES; a++) {
        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
            ++mid, coap_session_max_pdu_size(sess));
        if (!pdu) continue;
        uint8_t tok = 0xB0; coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8, (const uint8_t*)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
            coap_encode_var_safe(enc, sizeof(enc), COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);
        _prov_ack = false; _prov_resp_len = 0;
        coap_send(sess, pdu);
        TickType_t t0 = xTaskGetTickCount();
        while (!_prov_ack && (xTaskGetTickCount()-t0) < pdMS_TO_TICKS(REGISTER_RETRY_MS))
            coap_io_process(ctx, 100);
        if (_prov_ack && _prov_resp_len > 0) {
            char cmd[16] = {0};
            cbor_get_string(_prov_resp, _prov_resp_len, "cmd", cmd, sizeof(cmd));
            if (!strcmp(cmd, "start")) {
                ESP_LOGI("prov", "OK");
                coap_session_release(sess);
                coap_free_context(ctx);
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    coap_session_release(sess);
    coap_free_context(ctx);
    return ESP_FAIL;
}
