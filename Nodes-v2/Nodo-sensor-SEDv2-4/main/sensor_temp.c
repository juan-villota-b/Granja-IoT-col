#include "sensor_temp.h"

#include <math.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_config.h"

void sensor_temp_init(void)
{
}

sensor_temp_t sensor_temp_leer(void)
{
    sensor_temp_t lectura;
    float t_s = (float)(xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000.0f;
    float ruido = ((float)(esp_random() % 1000) / 1000.0f - 0.5f) * 0.2f;
    lectura.temperatura_c = TEMP_BASELINE
                          + 2.0f * sinf(2.0f * (float)M_PI * t_s / 1200.0f)
                          + ruido;
    return lectura;
}
