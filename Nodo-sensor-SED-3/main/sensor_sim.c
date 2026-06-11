#include "sensor_sim.h"

#include <math.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_config.h"

void sensor_init(void)
{
}

sensor_lectura_t sensor_leer(void)
{
    sensor_lectura_t lectura;

    float t_s = (float)(xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000.0f;
    float ruido_temp = ((float)(esp_random() % 1000) / 1000.0f - 0.5f) * 0.2f;
    lectura.temperatura_c = TEMP_BASELINE
                          + 2.0f * sinf(2.0f * (float)M_PI * t_s / 1200.0f)
                          + ruido_temp;

    float ruido_hum = (float)(esp_random() % 100) - 50.0f;
    ruido_hum /= 50.0f;
    lectura.humedad_pct = (uint8_t)(HUM_BASELINE
                          - 10.0f * sinf(2.0f * (float)M_PI * t_s / 1200.0f)
                          + ruido_hum);

    if (lectura.humedad_pct > 100) lectura.humedad_pct = 100;

    return lectura;
}
