#include "registration_client.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_openthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"

#include "node_config.h"

static const char *TAG = "reg";

/* ── CBOR encoder minimal para el payload de registro ──
   AB  = map(6)
   62 69 64  = text(2) "id"  → NODE_ID
   62 74 70  = text(2) "tp"  → NODE_TYPE
   61 76     = text(1) "v"   → FW_VERSION
   62 7A 6E  = text(2) "zn"  → ZONE_ID
   63 6C 61 74 = text(3) "lat" → float32
   63 6C 6E 67 = text(3) "lng" → float32                                   */
static size_t encode_register_cbor(float lat, float lng, uint8_t out[120])
{
    size_t p = 0;

    /* map(6) */
    out[p++] = 0xA6;

    /* "id" → NODE_ID */
    out[p++] = 0x62; out[p++] = 'i'; out[p++] = 'd';
    size_t nl = strlen(NODE_ID);
    if (nl < 24) { out[p++] = (uint8_t)(0x60 | nl); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)nl; }
    memcpy(out + p, NODE_ID, nl); p += nl;

    /* "tp" → NODE_TYPE */
    out[p++] = 0x62; out[p++] = 't'; out[p++] = 'p';
    size_t tl = strlen(NODE_TYPE);
    if (tl < 24) { out[p++] = (uint8_t)(0x60 | tl); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)tl; }
    memcpy(out + p, NODE_TYPE, tl); p += tl;

    /* "v" → FW_VERSION */
    out[p++] = 0x61; out[p++] = 'v';
    size_t vl = strlen(FW_VERSION);
    if (vl < 24) { out[p++] = (uint8_t)(0x60 | vl); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)vl; }
    memcpy(out + p, FW_VERSION, vl); p += vl;

    /* "zn" → ZONE_ID */
    out[p++] = 0x62; out[p++] = 'z'; out[p++] = 'n';
    size_t zl = strlen(ZONE_ID);
    if (zl < 24) { out[p++] = (uint8_t)(0x60 | zl); }
    else { out[p++] = 0x78; out[p++] = (uint8_t)zl; }
    memcpy(out + p, ZONE_ID, zl); p += zl;

    /* "lat" → float32 */
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'a'; out[p++] = 't';
    out[p++] = 0xFA;
    uint32_t x; memcpy(&x, &lat, sizeof(x));
    out[p++] = (uint8_t)(x >> 24);
    out[p++] = (uint8_t)((x >> 16) & 0xFF);
    out[p++] = (uint8_t)((x >> 8) & 0xFF);
    out[p++] = (uint8_t)(x & 0xFF);

    /* "lng" → float32 */
    out[p++] = 0x63; out[p++] = 'l'; out[p++] = 'n'; out[p++] = 'g';
    out[p++] = 0xFA;
    memcpy(&x, &lng, sizeof(x));
    out[p++] = (uint8_t)(x >> 24);
    out[p++] = (uint8_t)((x >> 16) & 0xFF);
    out[p++] = (uint8_t)((x >> 8) & 0xFF);
    out[p++] = (uint8_t)(x & 0xFF);

    return p;
}

/* ── Callback de respuesta CON ── */
static bool g_got_ack = false;

static coap_response_t reg_handler(coap_session_t *s, const coap_pdu_t *sent,
                                    const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    (void)s; (void)sent; (void)mid;
    if (rcvd) {
        unsigned int cls = coap_pdu_get_code(rcvd) >> 5;
        if (cls == 2) {   /* 2.xx Success */
            g_got_ack = true;
            ESP_LOGI(TAG, "Registro ACK 2.xx");
        }
    }
    return COAP_RESPONSE_OK;
}

/* ── API principal ── */
esp_err_t registration_send_once(float lat, float lng)
{
    ESP_LOGI(TAG, "Iniciando registro CON /readings → [%s]:%d",
             BRIDGE_IPV6, BRIDGE_PORT);

    /* Construir payload */
    uint8_t payload[120];
    size_t plen = encode_register_cbor(lat, lng, payload);

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

    static unsigned int msg_id = 100;

    for (int attempt = 0; attempt < REGISTER_MAX_RETRIES; attempt++) {
        coap_context_t *ctx = coap_new_context(NULL);
        if (!ctx) continue;
        coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
        coap_register_response_handler(ctx, reg_handler);

        coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
        if (!sess) { coap_free_context(ctx); continue; }

        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
                                         ++msg_id, coap_session_max_pdu_size(sess));
        if (!pdu) { coap_session_release(sess); coap_free_context(ctx); continue; }

        uint8_t tok = 0xA0;
        coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8,
                        (const uint8_t *)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(enc, sizeof(enc),
                                             COAP_MEDIATYPE_APPLICATION_CBOR), enc);
        coap_add_data(pdu, plen, payload);

        g_got_ack = false;
        coap_send(sess, pdu);

        /* Esperar ACK con timeout */
        TickType_t start = xTaskGetTickCount();
        while (!g_got_ack &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(REGISTER_RETRY_MS)) {
            coap_io_process(ctx, 100);
        }

        coap_session_release(sess);
        coap_free_context(ctx);

        if (g_got_ack) {
            ESP_LOGI(TAG, "Registro exitoso en intento %d", attempt + 1);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Registro intento %d/%d sin ACK", attempt + 1,
                 REGISTER_MAX_RETRIES);
    }

    ESP_LOGE(TAG, "Registro fallido tras %d intentos", REGISTER_MAX_RETRIES);
    return ESP_FAIL;
}
