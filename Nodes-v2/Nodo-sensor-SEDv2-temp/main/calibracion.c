#include "calibracion.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "cal";
static const char *NVS_NS = "cal";

static float g_offset = 0.0f;
static bool g_first_boot = false;

void cal_init(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        g_first_boot = true;
        ESP_LOGI(TAG, "Primer arranque — se calibrara a 20°C");
        return;
    }

    int32_t raw;
    if (nvs_get_i32(nvs, "offset", &raw) == ESP_OK) {
        g_offset = (float)raw / 100.0f;
        ESP_LOGI(TAG, "Offset cargado: %.2f°C", (double)g_offset);
    } else {
        g_first_boot = true;
        ESP_LOGI(TAG, "Sin offset previo — se calibrara a 20°C");
    }

    nvs_close(nvs);
}

float cal_aplicar(float temp_raw)
{
    if (g_first_boot) {
        g_offset = 20.0f - temp_raw;
        ESP_LOGI(TAG, "Calibracion: raw=%.1fC offset=%.1fC → 20.0°C",
                 (double)temp_raw, (double)g_offset);

        nvs_handle_t nvs;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_i32(nvs, "offset", (int32_t)(g_offset * 100.0f));
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Offset guardado en NVS");
        }
        g_first_boot = false;
    }
    return temp_raw + g_offset;
}

bool cal_es_primera_vez(void)
{
    return g_first_boot;
}
