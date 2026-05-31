/*
 * app_main.c — Punto de entrada del firmware Nodo Router T/H
 *
 * Flujo principal:
 *   1. Inicializar NVS, event loop, netif
 *   2. Inicializar gestión de energía (DFS + light sleep)
 *   3. Unirse automáticamente a la red Thread (headless)
 *   4. Verificar registro (el gateway consulta /sys/info)
 *   5. Iniciar servidor CoAP con 4 recursos
 *   6. Loop principal: leer sensor, evaluar umbrales, notificar
 *
 * Sin CLI — el nodo opera de forma completamente autónoma.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "node_config.h"
#include "config.h"
#include "thread_auto_join.h"
#include "power_mgmt.h"
#include "registration.h"
#include "coap_handlers.h"
#include "sensor_sim.h"

static const char *TAG = "app";

void app_main(void) {
    /*
     * En BATTERY_MODE se silencian TODOS los logs (solo WARN/ERROR).
     * En USB se deja todo en INFO para debug.
     */
#if BATTERY_MODE
    esp_log_level_set("*", ESP_LOG_WARN);
#endif
    /*
     * 1. Inicialización base del sistema
     * Eventfd se usa internamente por OpenThread para notificaciones.
     */
    ESP_LOGI(TAG, "=== Nodo Router T/H [%s] ===", NODE_ID);
    ESP_LOGI(TAG, "Zona: %s | Posición: (%.1f, %.1f) | v%s",
             ZONE_ID, POS_X, POS_Y, FW_VERSION);

    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    /*
     * 2. Inicializar gestión de energía
     * Habilita DFS (Dynamic Frequency Scaling) y light sleep si
     * POWER_SAVE_ENABLED está en 1.
     */
    ESP_ERROR_CHECK(power_mgmt_init());

    /*
     * 3. Inicializar sensor (simulado por ahora)
     */
    sensor_init();

    /*
     * 4. Cargar configuración desde NVS (o defaults de fábrica)
     */
    config_init();

    /*
     * 5. Iniciar OpenThread
     */
    esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    ESP_ERROR_CHECK(esp_openthread_start(&config));

    /*
     * 6. Unión automática a red Thread (headless)
     * Configura el dataset activo desde node_config.h si es primera vez,
     * o lo recupera de NVS si ya se había unido antes.
     */
    ESP_LOGI(TAG, "Uniéndose a red Thread...");
    esp_err_t join_ret = thread_auto_join();
    if (join_ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo unir a Thread. "
                 "Verifica que el gateway esté encendido y el canal/panid sean correctos.");
        /* No bloqueamos — el nodo reintenta en background */
    }

    /*
     * 7. Registro: el nodo expone /sys/info para que el gateway lo consulte.
     */
    registration_check();

    /*
     * 8. Iniciar servidor CoAP
     */
    ESP_ERROR_CHECK(coap_server_start());

    /*
     * 9. Loop principal
     * Cada SAMPLE_INTERVAL_MS: leer sensor y evaluar notificaciones.
     * OpenThread y CoAP corren en sus propias tareas.
     */
    ESP_LOGI(TAG, "Loop principal iniciado (sample cada %d ms)", SAMPLE_INTERVAL_MS);

    while (1) {
        /* Leer sensores */
        float   temp = 0;
        uint8_t hum  = 0;

        sensor_lectura_t lectura = sensor_leer();
        temp = lectura.temperatura_c;
        hum  = lectura.humedad_pct;

        /* Evaluar umbrales y notificar si es necesario */
        coap_check_and_notify(temp, hum);

        /* Esperar hasta la próxima lectura */
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}
