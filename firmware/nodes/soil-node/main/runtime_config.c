#include "runtime_config.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "node_config.h"

static const char *TAG    = "runtime_config";
static const char *NVS_NS = "sensor";

runtime_config_t g_runtime_config;

esp_err_t runtime_config_init(void)
{
    g_runtime_config.hum_threshold      = HUM_THRESHOLD_DEFAULT;
    g_runtime_config.heartbeat_s        = HEARTBEAT_S_DEFAULT;
    g_runtime_config.sample_interval_ms = SAMPLE_INTERVAL_MS;
    g_runtime_config.lat                = LAT_DEFAULT;
    g_runtime_config.lng                = LNG_DEFAULT;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "NVS vacio, usando defaults");
        return ESP_OK;
    }

    uint32_t u32;
    if (nvs_get_u32(nvs, "hum_th",    &u32) == ESP_OK) g_runtime_config.hum_threshold = (float)u32 / 100.0f;
    if (nvs_get_u32(nvs, "hb_s",      &u32) == ESP_OK) g_runtime_config.heartbeat_s = (uint16_t)u32;
    if (nvs_get_u32(nvs, "sample_ms", &u32) == ESP_OK) g_runtime_config.sample_interval_ms = u32;

    int32_t i32;
    if (nvs_get_i32(nvs, "lat_i", &i32) == ESP_OK) g_runtime_config.lat = (float)i32 / 1000000.0f;
    if (nvs_get_i32(nvs, "lng_i", &i32) == ESP_OK) g_runtime_config.lng = (float)i32 / 1000000.0f;

    nvs_close(nvs);
    ESP_LOGI(TAG, "Config: H>%.1f%% HB=%ds SI=%lums", (double)g_runtime_config.hum_threshold,
             g_runtime_config.heartbeat_s, (unsigned long)g_runtime_config.sample_interval_ms);
    return ESP_OK;
}

esp_err_t runtime_config_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) { ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err)); return err; }
    nvs_set_u32(nvs, "hum_th",    (uint32_t)(g_runtime_config.hum_threshold * 100.0f));
    nvs_set_u32(nvs, "hb_s",      (uint32_t)g_runtime_config.heartbeat_s);
    nvs_set_u32(nvs, "sample_ms", (uint32_t)g_runtime_config.sample_interval_ms);
    nvs_set_i32(nvs, "lat_i",     (int32_t)(g_runtime_config.lat * 1000000.0f));
    nvs_set_i32(nvs, "lng_i",     (int32_t)(g_runtime_config.lng * 1000000.0f));
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Config guardada");
    return ESP_OK;
}
