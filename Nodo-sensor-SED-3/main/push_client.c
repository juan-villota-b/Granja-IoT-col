#include "push_client.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_openthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"

#include "node_config.h"

static const char *TAG = "push";

static uint32_t f32_to_uint32(float f)
{
    uint32_t x; memcpy(&x, &f, sizeof(x)); return x;
}

/* CBOR: map(5) — t(float16), h(uint8), b(uint16), r(negint8), u(uint32) */
static size_t encode_telemetry_cbor(float t, uint8_t h,
                                     uint16_t batt, int8_t rssi,
                                     uint32_t up, uint8_t out[32])
{
    size_t p = 0;
    out[p++] = 0xA5;                         // map(5)

    /* "t": float16 */
    out[p++] = 0x61; out[p++] = 't';
    out[p++] = 0xF9;
    float temp = t;
    uint32_t x = f32_to_uint32(temp);
    uint16_t sign = (x >> 16) & 0x8000;
    int16_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t mant = (x >> 13) & 0x3FF;
    if (exp <= 0) { out[p++] = (uint8_t)sign; out[p++] = 0; }
    else if (exp >= 31) { out[p++] = (uint8_t)(sign | 0x7C); out[p++] = 0; }
    else { uint16_t v = sign | ((uint16_t)exp << 10) | mant;
           out[p++] = (uint8_t)(v >> 8); out[p++] = (uint8_t)(v & 0xFF); }

    /* "h": uint8 */
    out[p++] = 0x61; out[p++] = 'h';
    if (h < 24) out[p++] = h;
    else { out[p++] = 0x18; out[p++] = h; }

    /* "b": uint16 */
    out[p++] = 0x61; out[p++] = 'b';
    out[p++] = 0x19;
    out[p++] = (uint8_t)(batt >> 8);
    out[p++] = (uint8_t)(batt & 0xFF);

    /* "r": negint8 */
    out[p++] = 0x61; out[p++] = 'r';
    out[p++] = 0x38;
    out[p++] = (uint8_t)((-(int16_t)rssi) - 1);

    /* "u": uint32 */
    out[p++] = 0x61; out[p++] = 'u';
    out[p++] = 0x1A;
    out[p++] = (uint8_t)(up >> 24);
    out[p++] = (uint8_t)((up >> 16) & 0xFF);
    out[p++] = (uint8_t)((up >> 8) & 0xFF);
    out[p++] = (uint8_t)(up & 0xFF);

    return p;
}

esp_err_t push_telemetry(float temp_c, uint8_t hum_pct,
                          uint16_t batt_mv, int8_t rssi, uint32_t uptime_s)
{
    uint8_t payload[32];
    size_t plen = encode_telemetry_cbor(temp_c, hum_pct, batt_mv, rssi,
                                         uptime_s, payload);

    otInstance *ot = esp_openthread_get_instance();
    if (!ot) return ESP_FAIL;

    coap_address_t dst;
    coap_address_init(&dst);
    dst.addr.sin6.sin6_family = AF_INET6;
    dst.addr.sin6.sin6_port   = htons(BRIDGE_PORT);
    if (inet_pton(AF_INET6, BRIDGE_IPV6, &dst.addr.sin6.sin6_addr) != 1) {
        return ESP_FAIL;
    }

    static unsigned int msg_id = 200;

    for (int attempt = 0; attempt < 2; attempt++) {
        coap_context_t *ctx = coap_new_context(NULL);
        if (!ctx) continue;
        coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

        coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst,
                                                         COAP_PROTO_UDP);
        if (!sess) { coap_free_context(ctx); continue; }

        coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
                                         ++msg_id,
                                         coap_session_max_pdu_size(sess));
        if (!pdu) {
            coap_session_release(sess);
            coap_free_context(ctx);
            continue;
        }

        uint8_t tok = 0xA1;
        coap_add_token(pdu, 1, &tok);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 8,
                        (const uint8_t *)"readings");
        unsigned char enc[4];
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(enc, sizeof(enc),
                                             COAP_MEDIATYPE_APPLICATION_CBOR),
                        enc);
        coap_add_data(pdu, plen, payload);

        coap_send(sess, pdu);

        TickType_t start = xTaskGetTickCount();
        bool got_ack = false;
        while (!got_ack &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(3000)) {
            int result = coap_io_process(ctx, 100);
            if (result >= 0) {
                /* Intenta leer respuesta - si hay ACK, coap_io_process lo maneja */
                coap_io_process(ctx, 0);
            }
            got_ack = true; /* CON no espera ACK explicito en push */
        }

        coap_session_release(sess);
        coap_free_context(ctx);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Push fallido tras reintentos");
    return ESP_FAIL;
}
