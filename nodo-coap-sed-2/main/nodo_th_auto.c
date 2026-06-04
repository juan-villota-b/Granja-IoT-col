/*
 * Nodo Thread SED — CoAP Client ultra bajo consumo
 *
 * SED puro (Sleepy End Device): Thread MTD + auto light sleep.
 * Solo envía POST /readings cuando hay cambios > umbrales o heartbeat.
 * Arquitectura: nodo = CoAP client → Bridge CoAP server :5685
 * Downlink: piggyback en respuesta ACK al POST
 */

#include <string.h>

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
#include "coap_client.h"

#define TAG "nodo"

/* Active Operational Dataset — red OpenThread-5eac — 106 bytes TLV */
static const uint8_t THREAD_DATASET_TLVS[] = {
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

    otInstance *instance = esp_openthread_get_instance();

    otOperationalDatasetTlvs dataset;
    dataset.mLength = sizeof(THREAD_DATASET_TLVS);
    memcpy(dataset.mTlvs, THREAD_DATASET_TLVS, dataset.mLength);

    otError error = otDatasetSetActiveTlvs(instance, &dataset);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Error al configurar dataset activo: %d", error);
        esp_openthread_lock_release();
        return;
    }
    ESP_LOGI(TAG, "Dataset activo configurado");

    error = otIp6SetEnabled(instance, true);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Error al activar interfaz IPv6: %d", error);
        esp_openthread_lock_release();
        return;
    }
    ESP_LOGI(TAG, "Interfaz IPv6 activada (ifconfig up)");

    error = otThreadSetEnabled(instance, true);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Error al iniciar Thread: %d", error);
        esp_openthread_lock_release();
        return;
    }
    ESP_LOGI(TAG, "Thread iniciado (thread start)");

    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Nodo uniendo a red Granja-iot...");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Nodo SED [%s] ===", NODE_ID);
    ESP_LOGI(TAG, "Zona: %s | Bridge: %s:%d | v%s | HB=%ds | Umbral T>%.1f°C H>%d%%",
             ZONE_ID, BRIDGE_IPV6, COAP_SERVER_PORT, FW_VERSION,
             HEARTBEAT_INTERVAL_S, (double)TEMP_THRESHOLD_C, HUM_THRESHOLD_PCT);

    esp_vfs_eventfd_config_t eventfd_config = { .max_fds = 3 };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    /* ── Power management: tickless idle + light sleep automático ── */
    ESP_LOGI(TAG, "PM: CPU %d MHz + tickless idle", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    sensor_init();
    config_init();

    static esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));

    join_thread_network();

    /* Esperar a que la red Thread esté lista */
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Loop: leer sensor → umbral? → POST /readings → sleep");

    while (1) {
        sensor_lectura_t lectura = sensor_leer();
        if (coap_should_send(lectura.temperatura_c, lectura.humedad_pct)) {
            coap_send_telemetry(lectura.temperatura_c, lectura.humedad_pct);
        }
        /* vTaskDelay + tickless idle = CPU light sleep hasta el próximo tick */
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}
