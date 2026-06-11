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
#include "sensor_dht11.h"
#include "calibracion.h"
#include "push_client.h"

#include "openthread/thread.h"
#include "openthread/link.h"

#define TAG "nodo"

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

    /* Forzar modo SED antes de habilitar Thread.
       Si el flag rx-on-when-idle persiste de NVS, el radio nunca duerme.
       Limpiamos mRxOnWhenIdle y mDeviceType (MTD).          */
    otLinkModeConfig sed_mode;
    memset(&sed_mode, 0, sizeof(sed_mode));
    sed_mode.mNetworkData = 1;
    otThreadSetLinkMode(ot, sed_mode);

    otOperationalDatasetTlvs ds;
    ds.mLength = sizeof(THREAD_DATASET);
    memcpy(ds.mTlvs, THREAD_DATASET, ds.mLength);
    otDatasetSetActiveTlvs(ot, &ds);
    otIp6SetEnabled(ot, true);
    otThreadSetEnabled(ot, true);
    esp_openthread_lock_release();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Nodo SEDv2 [%s] %s ===", NODE_ID, FW_VERSION);
    ESP_LOGI(TAG, "Zona: %s Bridge: [%s]:%d", ZONE_ID, BRIDGE_IPV6, BRIDGE_PORT);
    ESP_LOGI(TAG, "Power-on / reset — primer arranque");

    esp_vfs_eventfd_config_t efd_cfg = { .max_fds = 3 };
    nvs_flash_init();
    esp_event_loop_create_default();
    esp_netif_init();
    esp_vfs_eventfd_register(&efd_cfg);

    config_init();
    sensor_dht11_init();
    cal_init();

    static esp_openthread_config_t ot_cfg = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    esp_openthread_start(&ot_cfg);
    join_thread_network();

    ESP_LOGI(TAG, "Esperando red Thread...");
    vTaskDelay(pdMS_TO_TICKS(4000));

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ot = esp_openthread_get_instance();
    otLinkModeConfig mode = otThreadGetLinkMode(ot);
    if (mode.mRxOnWhenIdle) {
        ESP_LOGW(TAG, "SED: rx-on-when-idle=1 → forzando a 0");
        mode.mRxOnWhenIdle = false;
        mode.mDeviceType = false;
        otThreadSetLinkMode(ot, mode);
    }
    mode = otThreadGetLinkMode(ot);
    ESP_LOGI(TAG, "SED mode: rx=%d devtype=%d netdata=%d",
             mode.mRxOnWhenIdle, mode.mDeviceType, mode.mNetworkData);
    otLinkSetPollPeriod(ot, 30000);
    esp_openthread_lock_release();

    sensor_temp_t lectura = sensor_dht11_leer();
    lectura.temperatura_c = cal_aplicar(lectura.temperatura_c);
    uint32_t uptime = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    ESP_LOGI(TAG, "1er push (registro + telemetria)");
    push_telemetry(&lectura, 0, uptime, true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    float last_temp = lectura.temperatura_c;
    uint32_t last_push_tick = uptime;

    while (1) {
        lectura = sensor_dht11_leer();
        lectura.temperatura_c = cal_aplicar(lectura.temperatura_c);
        uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

        float delta = fabsf(lectura.temperatura_c - last_temp);
        bool debe_push = false;

        if (delta >= g_config.temp_threshold_c)
            debe_push = true;
        else if ((uptime - last_push_tick) >= g_config.heartbeat_s)
            debe_push = true;

        if (debe_push) {
            push_telemetry(&lectura, 0, uptime, false);
            last_temp = lectura.temperatura_c;
            last_push_tick = uptime;
        }

        ESP_LOGD(TAG, "⏰ Idle %lu ms...", (unsigned long)g_config.sample_interval_ms);
        vTaskDelay(pdMS_TO_TICKS(g_config.sample_interval_ms));
    }
}
