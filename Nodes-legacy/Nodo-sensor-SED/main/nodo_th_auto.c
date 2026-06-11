#include <string.h>
#include <math.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_config.h"
#include "config.h"
#include "sensor_sim.h"
#include "coap_server.h"
#include "registration_client.h"
#include "push_client.h"

#define TAG "nodo"

/* ── Active Operational Dataset (red OpenThread-5eac, 106 bytes TLV) ── */
static const uint8_t THREAD_DATASET[] = {
    0x0e, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x11, 0x4a,
    0x03, 0x00, 0x00, 0x0f, 0x35, 0x06, 0x00, 0x04,
    0x00, 0x1f, 0xff, 0xe0, 0x02, 0x08, 0x6e, 0xb7,
    0x79, 0x82, 0x99, 0x99, 0xc7, 0x3a, 0x07, 0x08,
    0xfd, 0x29, 0xc5, 0x1e, 0xa8, 0x7a, 0xe5, 0xe5,
    0x05, 0x10, 0x16, 0x0f, 0xe4, 0xf6, 0xd2, 0x01,
    0x11, 0x5a, 0x74, 0x6d, 0x08, 0x02, 0x33, 0x2f,
    0x6e, 0x77, 0x03, 0x0f, 0x4f, 0x70, 0x65, 0x6e,
    0x54, 0x68, 0x72, 0x65, 0x61, 0x64, 0x2d, 0x35,
    0x65, 0x61, 0x63, 0x01, 0x02, 0x5e, 0xac, 0x04,
    0x10, 0xb0, 0xae, 0xeb, 0x93, 0xa6, 0x94, 0x51,
    0x43, 0x9e, 0x7c, 0xb7, 0x41, 0x66, 0xe6, 0xd4,
    0xe6, 0x0c, 0x04, 0x02, 0xa0, 0xf7, 0xf8,
};

static void join_thread_network(void)
{
    esp_openthread_lock_acquire(portMAX_DELAY);

    otInstance *ot = esp_openthread_get_instance();

    otOperationalDatasetTlvs ds;
    ds.mLength = sizeof(THREAD_DATASET);
    memcpy(ds.mTlvs, THREAD_DATASET, ds.mLength);

    if (otDatasetSetActiveTlvs(ot, &ds) != OT_ERROR_NONE)
        ESP_LOGE(TAG, "Error dataset activo");
    else
        ESP_LOGI(TAG, "Dataset configurado");

    if (otIp6SetEnabled(ot, true) != OT_ERROR_NONE)
        ESP_LOGE(TAG, "Error ifconfig up");
    else
        ESP_LOGI(TAG, "Interfaz IPv6 up");

    if (otThreadSetEnabled(ot, true) != OT_ERROR_NONE)
        ESP_LOGE(TAG, "Error thread start");
    else
        ESP_LOGI(TAG, "Thread iniciado, uniendo a red...");

    esp_openthread_lock_release();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Nodo SED [%s] v%s ===", NODE_ID, FW_VERSION);
    ESP_LOGI(TAG, "Zona: %s | Bridge: [%s]:%d", ZONE_ID, BRIDGE_IPV6, BRIDGE_PORT);

    /* ── Init plataforma ── */
    esp_vfs_eventfd_config_t efd_cfg = { .max_fds = 3 };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&efd_cfg));

    /* ── Cargar configuracion persistente (NVS) ── */
    config_init();
    sensor_init();

    /* ── Arrancar OpenThread ── */
    static esp_openthread_config_t ot_cfg = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    ESP_ERROR_CHECK(esp_openthread_start(&ot_cfg));
    join_thread_network();

    /* ── Esperar a que la red Thread este lista (MLEID != 0) ── */
    ESP_LOGI(TAG, "Esperando red Thread...");
    vTaskDelay(pdMS_TO_TICKS(4000));

    /* ── Arrancar servidor CoAP (single-task, sin tarea aparte) ── */
    coap_context_t *coap_ctx = coap_server_init();
    if (!coap_ctx) {
        ESP_LOGE(TAG, "Fallo al iniciar servidor CoAP");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* ── Registro CON una sola vez ── */
    ESP_LOGI(TAG, "Enviando registro al Bridge...");
    registration_send_once(g_config.lat, g_config.lng);

    /* ── Loop: leer → notificar (Observe) → io_process → dormir ── */
    ESP_LOGI(TAG, "Loop: leer cada %lums → notify observers → io → sleep",
             (unsigned long)g_config.sample_interval_ms);

    while (1) {
        sensor_lectura_t lectura = sensor_leer();

        /* Notificar observadores via Observe (RFC 7641).
           Misma tarea que coap_server_io_process → sin race condition.
           NOTIFY_NON_ALWAYS evita que libcoap elimine observers. */
        coap_server_notify(lectura.temperatura_c, lectura.humedad_pct);

        /* Procesar paquetes CoAP (ACKs, notificaciones pendientes) */
        coap_server_io_process(coap_ctx);

        vTaskDelay(pdMS_TO_TICKS(g_config.sample_interval_ms));
    }
}
