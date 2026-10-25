#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PUMP_GPIO   7
#define BTN_GPIO    0

void actuator_init(void);
void actuator_set(uint8_t state);
uint8_t actuator_get(void);
bool actuator_button_pressed(void);
