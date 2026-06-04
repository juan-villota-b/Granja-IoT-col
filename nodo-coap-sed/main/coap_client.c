#include "coap_client.h"

#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_openthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "coap3/coap.h"
#include "node_config.h"

static const char *TAG = "coap_client";

static unsigned int _g_msg_id = 0;

static bool     g_have_response    = false;
static uint8_t  g_response_buf[64] = {0};
static size_t   g_response_len     = 0;

static coap_response_t _response_handler(coap_session_t *session,
                              const coap_pdu_t *sent,
                              const coap_pdu_t *received,
                              const coap_mid_t mid)
{
    (void)session; (void)sent; (void)mid;
    g_have_response = false;
    g_response_len  = 0;
    size_t len; const uint8_t *data;
    if (coap_get_data(received, &len, &data) && len > 0 && len < sizeof(g_response_buf)) {
        memcpy(g_response_buf, data, len);
        g_response_len = len;
    }
    g_have_response = true;
    return COAP_RESPONSE_OK;
}

/* ── CBOR encoder (minimal, buffer en pila) ──────────────────────── */
static uint8_t  _cbuf[160];
static size_t   _cpos;
static bool     _g_registered = false;  /* se activa tras 1er ACK */

static void _cwr(uint8_t b) { _cbuf[_cpos++] = b; }

static void _cuint(uint32_t v)
{
    if      (v < 24)        { _cwr(v); }
    else if (v < 256)       { _cwr(0x18); _cwr(v); }
    else if (v < 65536)     { _cwr(0x19); _cwr(v>>8); _cwr(v&0xFF); }
    else                    { _cwr(0x1A); _cwr(v>>24); _cwr((v>>16)&0xFF);
                              _cwr((v>>8)&0xFF); _cwr(v&0xFF); }
}

static void _cneg(int32_t v)
{
    if (v >= 0) { _cuint((uint32_t)v); return; }
    uint32_t n = (uint32_t)((-v) - 1);
    if      (n < 24)        { _cwr(0x20 | n); }
    else if (n < 256)       { _cwr(0x38); _cwr(n); }
    else                    { _cwr(0x39); _cwr(n>>8); _cwr(n&0xFF); }
}

static void _ctext(const char *s)
{
    size_t l = strlen(s);
    if (l < 24) { _cwr(0x60 | l); }
    else        { _cwr(0x78); _cwr(l); }
    while (*s) _cwr(*s++);
}

static uint16_t _f32_to_f16(float f)
{
    uint32_t x; memcpy(&x, &f, sizeof(x));
    uint32_t s  = (x >> 16) & 0x8000;
    int32_t  e  = ((int32_t)(x >> 23) & 0xFF) - 127 + 15;
    uint32_t m  = (x >> 13) & 0x3FF;
    if (e <= 0)  return (uint16_t)s;
    if (e >= 31) return (uint16_t)(s | 0x7C00);
    return (uint16_t)(s | ((uint32_t)e << 10) | m);
}

static void _cf16(float f)
{
    uint16_t h = _f32_to_f16(f);
    _cwr(0xF9); _cwr(h >> 8); _cwr(h & 0xFF);
}

static void _cf32(float f)
{
    uint32_t x; memcpy(&x, &f, sizeof(x));
    _cwr(0xFA); _cwr(x >> 24); _cwr((x >> 16) & 0xFF);
    _cwr((x >> 8) & 0xFF); _cwr(x & 0xFF);
}

static size_t _encode_telemetry(float temp, uint8_t hum)
{
    int8_t  rssi_sim   = -65;
    uint16_t batt_sim  = 3100;
    uint32_t uptime_s  = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    _cpos = 0;
    _ctext("id"); _cuint(0); /* placeholder, se sobreescribe */ (void)temp; (void)hum; (void)batt_sim; (void)rssi_sim; (void)uptime_s;

    if (!_g_registered) {
        _cpos = 0;
        _cwr(0xAB);
        _ctext("id"); _ctext(NODE_ID);
        _ctext("t");  _cf16(temp);
        _ctext("h");  _cuint(hum);
        _ctext("b");  _cuint(batt_sim);
        _ctext("r");  _cneg(rssi_sim);
        _ctext("u");  _cuint(uptime_s);
        _ctext("zn"); _ctext(ZONE_ID);
        _ctext("tp"); _ctext(NODE_TYPE);
        _ctext("lat"); _cf32(LAT);
        _ctext("lng"); _cf32(LNG);
        _ctext("v");  _ctext(FW_VERSION);
    } else {
        _cpos = 0;
        _cwr(0xA6);
        _ctext("id"); _ctext(NODE_ID);
        _ctext("t");  _cf16(temp);
        _ctext("h");  _cuint(hum);
        _ctext("b");  _cuint(batt_sim);
        _ctext("r");  _cneg(rssi_sim);
        _ctext("u");  _cuint(uptime_s);
    }
    return _cpos;
}

/* ── Mini parser para comandos en respuesta CBOR ────────────────── */
static void _parse_downlink(void)
{
    if (g_response_len < 6) return;

    /* cmd = "set_valve" + "v" */
    const uint8_t *p = g_response_buf;
    const uint8_t *end = p + g_response_len;

    for (const uint8_t *cp = p; cp < end - 3; cp++) {
        if (cp[0] == 0x01 && cp[1] == 0x01) { /* {"v":1} → abrir */
            ESP_LOGI(TAG, "DOWNLINK: ABRIR valvula");
            return;
        }
        if (cp[0] == 0x01 && cp[1] == 0x00) { /* {"v":0} → cerrar */
            ESP_LOGI(TAG, "DOWNLINK: CERRAR valvula");
            return;
        }
        /* {"tt":2.0,"ht":5,"hb":60} → thresholds */
        if (cp[0] == 0x62 && cp[1] == 0x74 && cp[2] == 0x74) { /* "tt" */
            ESP_LOGI(TAG, "DOWNLINK: set_thresholds recibido");
            return;
        }
    }
    (void)p;
}

/* ── Filtro de umbrales + heartbeat ────────────────────────────── */
static float    _last_temp = TEMP_BASELINE;
static uint8_t  _last_hum  = HUM_BASELINE;
static TickType_t _last_send = 0;

bool coap_should_send(float temp, uint8_t hum)
{
    TickType_t now = xTaskGetTickCount();

    float   dt = fabsf(temp - _last_temp);
    int     dh = abs((int)hum - (int)_last_hum);

    bool    thresh = (dt > TEMP_THRESHOLD_C) || (dh > HUM_THRESHOLD_PCT);
    bool    hb     = (now - _last_send) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL_S * 1000);

    if (thresh || hb) {
        _last_temp = temp;
        _last_hum  = hum;
        _last_send = now;
        return true;
    }
    return false;
}

/* ── API principal ───────────────────────────────────────────────── */
esp_err_t coap_send_telemetry(float temp, uint8_t hum)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) { ESP_LOGW(TAG, "sin instancia OT"); return ESP_FAIL; }

    size_t payload_len = _encode_telemetry(temp, hum);

    /* ── Context + session ── */
    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) { ESP_LOGE(TAG, "coap_new_context"); return ESP_FAIL; }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
    coap_register_response_handler(ctx, _response_handler);

    coap_address_t dst;
    coap_address_init(&dst);
    dst.addr.sin6.sin6_family = AF_INET6;
    dst.addr.sin6.sin6_port   = htons(COAP_SERVER_PORT);
    if (inet_pton(AF_INET6, BRIDGE_IPV6, &dst.addr.sin6.sin6_addr) != 1) {
        ESP_LOGE(TAG, "inet_pton: %s", BRIDGE_IPV6);
        coap_free_context(ctx); return ESP_FAIL;
    }

    coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
    if (!sess) { ESP_LOGE(TAG, "client_session"); coap_free_context(ctx); return ESP_FAIL; }

    /* ── Construir PDU ── */
    coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_POST,
                                     ++_g_msg_id, coap_session_max_pdu_size(sess));
    if (!pdu) { coap_session_release(sess); coap_free_context(ctx); return ESP_FAIL; }

    uint8_t tok = 0x01;
    coap_add_token(pdu, 1, &tok);

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 8,
                    (const uint8_t *)"readings");

    unsigned char enc[4];
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(enc, sizeof(enc),
                                         COAP_MEDIATYPE_APPLICATION_CBOR), enc);

    coap_add_data(pdu, payload_len, _cbuf);

    g_have_response = false;
    coap_send(sess, pdu);

    if (!_g_registered) {
        ESP_LOGI(TAG, "POST /readings → %s (%d B + attrs) t=%.2f h=%u",
                 BRIDGE_IPV6, (int)payload_len, (double)temp, hum);
    } else {
        ESP_LOGI(TAG, "POST /readings → %s (%d B) t=%.2f h=%u",
                 BRIDGE_IPV6, (int)payload_len, (double)temp, hum);
    }

    /* ── Esperar respuesta ── */
    TickType_t start = xTaskGetTickCount();
    while (!g_have_response &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(COAP_TIMEOUT_MS)) {
        coap_io_process(ctx, 200);
    }

    if (g_have_response) {
        if (!_g_registered) {
            _g_registered = true;
            ESP_LOGI(TAG, "REGISTRO OK — BRIDGE CONFIRMÓ ATRIBUTOS");
        }
        if (g_response_len > 0) {
            _parse_downlink();
        }
    }

    coap_session_release(sess);
    coap_free_context(ctx);
    return ESP_OK;
}
