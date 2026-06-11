#include "config.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "node_config.h"

static const char *TAG    = "cfg";
static const char *NVS_NS = "sensor";

nodo_config_t g_config;

esp_err_t config_init(void)
{
    g_config.temp_threshold_c   = TEMP_THRESHOLD_C_DEFAULT;
    g_config.heartbeat_s        = HEARTBEAT_S_DEFAULT;
    g_config.sample_interval_ms = SAMPLE_INTERVAL_MS;
    g_config.lat                = LAT_DEFAULT;
    g_config.lng                = LNG_DEFAULT;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "NVS vacio, usando defaults");
        return ESP_OK;
    }

    uint32_t u32;
    if (nvs_get_u32(nvs, "temp_th",   &u32) == ESP_OK) g_config.temp_threshold_c = (float)u32 / 100.0f;
    if (nvs_get_u32(nvs, "hb_s",      &u32) == ESP_OK) g_config.heartbeat_s = (uint16_t)u32;
    if (nvs_get_u32(nvs, "sample_ms", &u32) == ESP_OK) g_config.sample_interval_ms = u32;

    int32_t i32;
    if (nvs_get_i32(nvs, "lat_i", &i32) == ESP_OK) g_config.lat = (float)i32 / 1000000.0f;
    if (nvs_get_i32(nvs, "lng_i", &i32) == ESP_OK) g_config.lng = (float)i32 / 1000000.0f;

    nvs_close(nvs);
    ESP_LOGI(TAG, "Config: T>%.1fC HB=%ds SI=%lums", (double)g_config.temp_threshold_c,
             g_config.heartbeat_s, (unsigned long)g_config.sample_interval_ms);
    return ESP_OK;
}

esp_err_t config_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) { ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err)); return err; }
    nvs_set_u32(nvs, "temp_th",   (uint32_t)(g_config.temp_threshold_c * 100.0f));
    nvs_set_u32(nvs, "hb_s",      (uint32_t)g_config.heartbeat_s);
    nvs_set_u32(nvs, "sample_ms", (uint32_t)g_config.sample_interval_ms);
    nvs_set_i32(nvs, "lat_i",     (int32_t)(g_config.lat * 1000000.0f));
    nvs_set_i32(nvs, "lng_i",     (int32_t)(g_config.lng * 1000000.0f));
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Config guardada");
    return ESP_OK;
}
