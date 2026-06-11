#include "sensor_dht22.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_config.h"

static const char *TAG = "dht22";
static int g_gpio = -1;

/* ── Tiempos DHT22 (µs) ─────────────────────────────
   Host start:  LOW ≥ 18ms, HIGH 20-40µs
   DHT resp:    LOW 80µs, HIGH 80µs
   Bit "0":     LOW 50µs, HIGH 26-28µs
   Bit "1":     LOW 50µs, HIGH 70µs                     */

#define DHT22_HOST_LOW_US  20000
#define DHT22_HOST_HIGH_US 40
#define DHT22_TIMEOUT_US   200

static int8_t dht22_espera_nivel(gpio_num_t gpio, int nivel, uint32_t timeout_us)
{
    uint32_t t = 0;
    while (gpio_get_level(gpio) != nivel) {
        if (++t > timeout_us) return -1;
        esp_rom_delay_us(1);
    }
    return (int8_t)t;
}

static bool dht22_leer_bits(gpio_num_t gpio, uint8_t datos[5])
{
    for (int i = 0; i < 5; i++) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; b--) {
            if (dht22_espera_nivel(gpio, 0, DHT22_TIMEOUT_US) < 0) return false;
            if (dht22_espera_nivel(gpio, 1, DHT22_TIMEOUT_US) < 0) return false;
            uint32_t t_high = 0;
            while (gpio_get_level(gpio) && t_high < DHT22_TIMEOUT_US) {
                t_high++;
                esp_rom_delay_us(1);
            }
            if (t_high > 40) byte |= (1 << b);
        }
        datos[i] = byte;
    }
    return true;
}

void sensor_dht22_init(void)
{
    g_gpio = DHT22_GPIO;
    if (g_gpio < 0) {
        ESP_LOGE(TAG, "GPIO no configurado (DHT22_GPIO=%d)", g_gpio);
        return;
    }
    gpio_set_direction(g_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(g_gpio, GPIO_PULLUP_ONLY);
    gpio_set_level(g_gpio, 1);
    ESP_LOGI(TAG, "DHT22 en GPIO %d", g_gpio);
}

sensor_temp_t sensor_dht22_leer(void)
{
    sensor_temp_t lectura = { .temperatura_c = TEMP_BASELINE };

    if (g_gpio < 0) return lectura;

    gpio_num_t gpio = (gpio_num_t)g_gpio;

    /* ── Start signal ── */
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(DHT22_HOST_LOW_US);
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(DHT22_HOST_HIGH_US);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    /* ── Esperar respuesta ── */
    taskDISABLE_INTERRUPTS();
    if (dht22_espera_nivel(gpio, 0, DHT22_TIMEOUT_US) < 0) { taskENABLE_INTERRUPTS(); return lectura; }
    if (dht22_espera_nivel(gpio, 1, DHT22_TIMEOUT_US) < 0) { taskENABLE_INTERRUPTS(); return lectura; }

    /* ── Leer 40 bits ── */
    uint8_t datos[5];
    if (!dht22_leer_bits(gpio, datos)) { taskENABLE_INTERRUPTS(); return lectura; }
    taskENABLE_INTERRUPTS();

    /* ── Checksum ── */
    if ((uint8_t)(datos[0] + datos[1] + datos[2] + datos[3]) != datos[4]) {
        ESP_LOGW(TAG, "Checksum DHT22 fallo");
        return lectura;
    }

    /* ── Decodificar temperatura ──
       Formato: bits[2..3] = temperatura entero + decimal
       Si bit[3] MSB = 1 → temperatura negativa                */
    int16_t temp_raw = ((int16_t)(datos[2] & 0x7F)) << 8 | datos[3];
    if (datos[2] & 0x80) temp_raw = -temp_raw;
    lectura.temperatura_c = (float)temp_raw / 10.0f;

    ESP_LOGD(TAG, "DHT22: T=%.1f°C hum=%d.%d%%",
             (double)lectura.temperatura_c, datos[0], datos[1]);
    return lectura;
}
