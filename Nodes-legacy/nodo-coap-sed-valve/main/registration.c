#include "registration.h"

#include "esp_log.h"

static const char *TAG = "registration";

esp_err_t registration_check(void)
{
    ESP_LOGI(TAG, "Nodo listo. Esperando consulta del gateway via /sys/info");
    return ESP_OK;
}
