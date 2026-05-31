#pragma once

#include "esp_err.h"

/**
 * Inicializa las estrategias de ahorro de energía:
 *   - DFS (Dynamic Frequency Scaling): reduce CPU en idle
 *   - Light sleep: CPU duerme entre ráfagas de tráfico Thread
 *
 * Si POWER_SAVE_ENABLED=0 en node_config.h, no hace nada.
 */
esp_err_t power_mgmt_init(void);
