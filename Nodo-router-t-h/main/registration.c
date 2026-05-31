#include "registration.h"

#include "esp_log.h"

static const char *TAG = "registration";

esp_err_t registration_check(void) {
    /*
     * El nodo expone /sys/info como recurso GET.
     * El gateway (backend) es responsable de:
     *   1. Detectar nuevos dispositivos Thread (via OTBR diagnostics)
     *   2. Consultar GET coap://[ipv6]/sys/info
     *   3. Almacenar id, zone, type, x, y, ver en su base de datos
     *   4. Subscribirse a /env/temp y /env/hum con Observe
     *
     * El nodo no mantiene estado de registro — siempre sirve datos.
     */
    ESP_LOGI(TAG, "Nodo listo. Esperando consulta del gateway via /sys/info");
    return ESP_OK;
}
