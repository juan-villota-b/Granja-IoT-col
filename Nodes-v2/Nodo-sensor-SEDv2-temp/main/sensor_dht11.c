#include "sensor_dht11.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_config.h"

static const char *TAG = "dht11";
static int g_gpio = -1;

#define DHT11_HOST_LOW_US  20000
#define DHT11_HOST_HIGH_US 40
#define DHT11_TIMEOUT_US   200

static int8_t dht11_espera_nivel(gpio_num_t gpio, int nivel, uint32_t timeout_us)
{
    uint32_t t = 0;
    while (gpio_get_level(gpio) != nivel) {
        if (++t > timeout_us) return -1;
        esp_rom_delay_us(1);
    }
    return (int8_t)t;
}

static bool dht11_leer_bits(gpio_num_t gpio, uint8_t datos[5])
{
    for (int i = 0; i < 5; i++) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; b--) {
            if (dht11_espera_nivel(gpio, 0, DHT11_TIMEOUT_US) < 0) return false;
            if (dht11_espera_nivel(gpio, 1, DHT11_TIMEOUT_US) < 0) return false;
            uint32_t t_high = 0;
            while (gpio_get_level(gpio) && t_high < DHT11_TIMEOUT_US) {
                t_high++;
                esp_rom_delay_us(1);
            }
            if (t_high > 40) byte |= (1 << b);
        }
        datos[i] = byte;
    }
    return true;
}

void sensor_dht11_init(void)
{
    g_gpio = DHT11_GPIO;
    if (g_gpio < 0) {
        ESP_LOGE(TAG, "GPIO no configurado (DHT11_GPIO=%d)", g_gpio);
        return;
    }
    gpio_set_direction(g_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(g_gpio, GPIO_PULLUP_ONLY);
    gpio_set_level(g_gpio, 1);
    ESP_LOGI(TAG, "DHT11 en GPIO %d", g_gpio);
}

sensor_temp_t sensor_dht11_leer(void)
{
    sensor_temp_t lectura = { .temperatura_c = TEMP_BASELINE };

    if (g_gpio < 0) return lectura;

    gpio_num_t gpio = (gpio_num_t)g_gpio;

    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(DHT11_HOST_LOW_US);
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(DHT11_HOST_HIGH_US);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    taskDISABLE_INTERRUPTS();
    if (dht11_espera_nivel(gpio, 0, DHT11_TIMEOUT_US) < 0) { taskENABLE_INTERRUPTS(); return lectura; }
    if (dht11_espera_nivel(gpio, 1, DHT11_TIMEOUT_US) < 0) { taskENABLE_INTERRUPTS(); return lectura; }

    uint8_t datos[5];
    if (!dht11_leer_bits(gpio, datos)) { taskENABLE_INTERRUPTS(); return lectura; }
    taskENABLE_INTERRUPTS();

    ESP_LOGI(TAG, "RAW: %02x %02x %02x %02x %02x",
             datos[0], datos[1], datos[2], datos[3], datos[4]);

    if ((uint8_t)(datos[0] + datos[1] + datos[2] + datos[3]) != datos[4]) {
        ESP_LOGW(TAG, "Checksum DHT11 fallo: suma=%02x != cks=%02x",
                 (uint8_t)(datos[0] + datos[1] + datos[2] + datos[3]), datos[4]);
        return lectura;
    }

    lectura.temperatura_c = (float)datos[2] + (float)datos[3] * 0.1f;

    ESP_LOGI(TAG, "T=%.1fC  hum=%d.%d%%",
             (double)lectura.temperatura_c, datos[0], datos[1]);
    return lectura;
}
