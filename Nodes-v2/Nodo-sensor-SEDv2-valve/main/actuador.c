#include "actuador.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

static const char *TAG = "act";
static uint8_t g_state = 0;

void act_init(void)
{
    gpio_set_direction(PUMP_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PUMP_GPIO, 0);
    g_state = 0;

    gpio_set_direction(BTN_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_GPIO, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "Bomba en GPIO %d, estado inicial OFF", PUMP_GPIO);
    ESP_LOGI(TAG, "Boton manual en GPIO %d (pull-up, 0=presionado)", BTN_GPIO);
}

void act_set(uint8_t state)
{
    g_state = state ? 1 : 0;
    gpio_set_level(PUMP_GPIO, g_state);
    ESP_LOGI(TAG, "Bomba %s", g_state ? "ON" : "OFF");
}

uint8_t act_get(void)
{
    return g_state;
}

bool act_boton_presionado(void)
{
    if (gpio_get_level(BTN_GPIO) != 0)
        return false;
    esp_rom_delay_us(50000);
    if (gpio_get_level(BTN_GPIO) != 0)
        return false;
    return true;
}
