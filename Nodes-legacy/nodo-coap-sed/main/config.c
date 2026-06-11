#include "config.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "node_config.h"

static const char *TAG = "config";
static const char *NVS_NS = "node_cfg";

nodo_config_t g_config = {
    .temp_threshold_c  = TEMP_THRESHOLD_C,
    .hum_threshold_pct = HUM_THRESHOLD_PCT,
    .heartbeat_s       = HEARTBEAT_INTERVAL_S,
};

esp_err_t config_init(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS vacio. Usando defaults de node_config.h");
        return ESP_OK;
    }

    uint32_t u32_val;
    err = nvs_get_u32(nvs, "temp_th", &u32_val);
    if (err == ESP_OK) {
        g_config.temp_threshold_c = (float)u32_val / 100.0f;
    }

    err = nvs_get_u32(nvs, "hum_th", &u32_val);
    if (err == ESP_OK) {
        g_config.hum_threshold_pct = (uint8_t)u32_val;
    }

    uint16_t u16_val;
    err = nvs_get_u16(nvs, "heartbeat", &u16_val);
    if (err == ESP_OK) {
        g_config.heartbeat_s = u16_val;
    }

    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t config_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open fallo: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_u32(nvs, "temp_th", (uint32_t)(g_config.temp_threshold_c * 100.0f));
    nvs_set_u32(nvs, "hum_th", (uint32_t)g_config.hum_threshold_pct);
    nvs_set_u16(nvs, "heartbeat", g_config.heartbeat_s);
    nvs_commit(nvs);
    nvs_close(nvs);

    return ESP_OK;
}
