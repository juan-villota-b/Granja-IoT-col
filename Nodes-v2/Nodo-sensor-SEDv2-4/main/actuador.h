#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PUMP_GPIO   7
#define BTN_GPIO    0

void act_init(void);
void act_set(uint8_t state);
uint8_t act_get(void);
bool act_boton_presionado(void);
