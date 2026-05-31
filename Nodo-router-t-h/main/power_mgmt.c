#include "power_mgmt.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "node_config.h"

static const char *TAG = "power";

esp_err_t power_mgmt_init(void) {
    if (!POWER_SAVE_ENABLED) {
        ESP_LOGI(TAG, "Ahorro de energía DESHABILITADO (POWER_SAVE_ENABLED=0)");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando gestión de energía...");

#if CONFIG_PM_DFS_INIT_AUTO
    ESP_LOGI(TAG, "  DFS: CPU 80-160 MHz (automático, configurado por IDF)");
#else
    ESP_LOGI(TAG, "  DFS: NO HABILITADO");
#endif

#if CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP
    ESP_LOGI(TAG, "  Light sleep: HABILITADO (con USB desconectado)");
#else
    ESP_LOGI(TAG, "  Light sleep: NO DISPONIBLE");
#endif

    ESP_LOGI(TAG, "  WiFi/BT: APAGADOS (solo 802.15.4)");
    return ESP_OK;
}
