#include "thread_auto_join.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "ot_examples_common.h"

#include "openthread/dataset.h"
#include "openthread/error.h"
#include "openthread/instance.h"
#include "openthread/thread.h"

#include "node_config.h"

static const char *TAG = "thread_join";

/*
 * Convierte un string hex a bytes.
 * Ej: "00112233" de 8 chars → bytes[0]=0x00, bytes[1]=0x11, ...
 */
static bool hex_str_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        char byte_str[3] = {hex[2*i], hex[2*i+1], '\0'};
        char *end = NULL;
        long val = strtol(byte_str, &end, 16);
        if (*end != '\0') return false;
        out[i] = (uint8_t)val;
    }
    return true;
}

/*
 * Configura el dataset activo de OpenThread con los valores de node_config.h.
 * Esto permite que el nodo se una a la red correcta sin depender de Kconfig.
 */
static esp_err_t configurar_dataset(otInstance *instance) {
    otOperationalDataset dataset = {0};

    /* Timestamp (necesario para que el dataset sea válido) */
    dataset.mActiveTimestamp.mSeconds = 1;
    dataset.mActiveTimestamp.mTicks   = 0;
    dataset.mComponents.mIsActiveTimestampPresent = true;

    /* Canal */
    dataset.mChannel = THREAD_CHANNEL;
    dataset.mComponents.mIsChannelPresent = true;

    /* Channel mask: solo el canal configurado */
    dataset.mChannelMask = (1ULL << THREAD_CHANNEL);
    dataset.mComponents.mIsChannelMaskPresent = true;

    /* PAN ID */
    dataset.mPanId = THREAD_PANID;
    dataset.mComponents.mIsPanIdPresent = true;

    /* Network Name */
    const char *name = THREAD_NETWORK_NAME;
    size_t name_len = strlen(name);
    if (name_len > OT_NETWORK_NAME_MAX_SIZE) name_len = OT_NETWORK_NAME_MAX_SIZE;
    memcpy(dataset.mNetworkName.m8, name, name_len);
    dataset.mComponents.mIsNetworkNamePresent = true;

    /* Network Key */
    if (!hex_str_to_bytes(THREAD_NETWORK_KEY, dataset.mNetworkKey.m8, OT_NETWORK_KEY_SIZE)) {
        ESP_LOGE(TAG, "THREAD_NETWORK_KEY invalida (16 bytes hex esperados)");
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsNetworkKeyPresent = true;

    /* Extended PAN ID */
    if (!hex_str_to_bytes(THREAD_EXT_PAN_ID, dataset.mExtendedPanId.m8, OT_EXT_PAN_ID_SIZE)) {
        ESP_LOGE(TAG, "THREAD_EXT_PAN_ID invalida (8 bytes hex esperados)");
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;

    /* PSKc */
    if (!hex_str_to_bytes(THREAD_PSKC, dataset.mPskc.m8, OT_PSKC_MAX_SIZE)) {
        ESP_LOGE(TAG, "THREAD_PSKC invalido (16 bytes hex esperados)");
        return ESP_ERR_INVALID_ARG;
    }
    dataset.mComponents.mIsPskcPresent = true;

    /* Aplicar dataset */
    otError error = otDatasetSetActive(instance, &dataset);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "otDatasetSetActive falló: %s", otThreadErrorToString(error));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Dataset configurado: canal=%d, panid=0x%04X, name=%s",
             THREAD_CHANNEL, THREAD_PANID, THREAD_NETWORK_NAME);
    return ESP_OK;
}

esp_err_t thread_auto_join(void) {
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance no disponible");
        return ESP_FAIL;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    /*
     * Verificar si ya hay un dataset activo en NVS (join previo).
     * Si no existe, configurarlo con los valores de node_config.h.
     */
    otOperationalDatasetTlvs dataset_tlvs;
    otError err = otDatasetGetActiveTlvs(instance, &dataset_tlvs);

    if (err != OT_ERROR_NONE) {
        ESP_LOGI(TAG, "No hay dataset en NVS. Configurando desde node_config.h...");
        esp_err_t ret = configurar_dataset(instance);
        if (ret != ESP_OK) {
            esp_openthread_lock_release();
            return ret;
        }
        /* Releer el dataset recién configurado */
        err = otDatasetGetActiveTlvs(instance, &dataset_tlvs);
    } else {
        ESP_LOGI(TAG, "Dataset encontrado en NVS. Usando el existente.");
    }

    esp_openthread_lock_release();

    /* Iniciar el proceso de auto-start: form/join la red */
    ESP_LOGI(TAG, "Iniciando unión a red Thread...");
    esp_err_t ret = esp_openthread_auto_start(
        (err == OT_ERROR_NONE) ? &dataset_tlvs : NULL
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_openthread_auto_start falló");
        return ret;
    }

    /*
     * Esperar a que el nodo alcance estado router o child.
     * Timeout de 60s (tiempo máximo de convergencia Thread).
     */
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();

    int attempts = 60;
    while (role != OT_DEVICE_ROLE_ROUTER && role != OT_DEVICE_ROLE_CHILD && attempts > 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_openthread_lock_acquire(portMAX_DELAY);
        role = otThreadGetDeviceRole(instance);
        esp_openthread_lock_release();
        attempts--;
    }

    if (role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_CHILD) {
        ESP_LOGI(TAG, "Unido a Thread como %s",
                 role == OT_DEVICE_ROLE_ROUTER ? "ROUTER" : "CHILD");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "No se pudo unir a Thread tras 60s");
    return ESP_FAIL;
}
