#include "sensor_hw390.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "hw390";

static adc_oneshot_unit_handle_t g_adc_handle;
static adc_cali_handle_t        g_cali_handle;
static bool                     g_calibrado = false;

void sensor_hw390_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &g_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, ADC_CHANNEL_4, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_4,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali_handle) == ESP_OK) {
        g_calibrado = true;
        ESP_LOGI(TAG, "Calibracion ADC por curve-fitting habilitada");
    } else {
        ESP_LOGW(TAG, "Sin calibracion ADC, usando lecturas raw");
    }

    ESP_LOGI(TAG, "HW-390 en GPIO %d (ADC1_CH4), rango %d-%d mV",
             HW390_GPIO, HW390_WATER_MV, HW390_AIR_MV);
}

sensor_data_t sensor_hw390_leer(void)
{
    sensor_data_t lectura = { .humedad = 50.0f };

    int raw = 0;
    if (adc_oneshot_read(g_adc_handle, ADC_CHANNEL_4, &raw) != ESP_OK) {
        ESP_LOGW(TAG, "Fallo lectura ADC");
        return lectura;
    }

    int mv = 0;
    if (g_calibrado) {
        adc_cali_raw_to_voltage(g_cali_handle, raw, &mv);
    } else {
        mv = (raw * 3100) / 4096;
    }

    if (mv >= HW390_AIR_MV) {
        lectura.humedad = 0.0f;
    } else if (mv <= HW390_WATER_MV) {
        lectura.humedad = 100.0f;
    } else {
        lectura.humedad = 100.0f * (float)(HW390_AIR_MV - mv)
                        / (float)(HW390_AIR_MV - HW390_WATER_MV);
    }

    ESP_LOGD(TAG, "raw=%d mV=%d humedad=%.1f", raw, mv, (double)lectura.humedad);
    return lectura;
}
