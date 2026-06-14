#include "sensor_ldr.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "node_config.h"

static const char *TAG = "ldr";
static adc_oneshot_unit_handle_t g_adc_handle = NULL;

void sensor_ldr_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &g_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, ADC_CHANNEL_4, &chan_cfg));

    ESP_LOGI(TAG, "LDR en GPIO %d (ADC1_CH4)", LDR_GPIO);
}

sensor_data_t sensor_ldr_leer(void)
{
    sensor_data_t lectura = { .porcentaje_luz = LUZ_BASELINE };

    if (!g_adc_handle) return lectura;

    int adc_raw = 0;
    esp_err_t err = adc_oneshot_read(g_adc_handle, ADC_CHANNEL_4, &adc_raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error lectura ADC: %s", esp_err_to_name(err));
        return lectura;
    }

    lectura.porcentaje_luz = ((float)adc_raw / 4095.0f) * 100.0f;

    ESP_LOGD(TAG, "LDR: raw=%d luz=%.1f%%", adc_raw, (double)lectura.porcentaje_luz);
    return lectura;
}
