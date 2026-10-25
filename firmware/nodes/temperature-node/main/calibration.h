#pragma once

#include <stdbool.h>

void calibration_init(void);
float calibration_apply(float temp_raw);
bool calibration_is_first_run(void);
