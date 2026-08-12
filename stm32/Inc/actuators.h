#ifndef ACTUATORS_H
#define ACTUATORS_H

#include "stm32f1xx_hal.h"
#include "main.h"

void SIPO_setup();
void SIPO_write(uint8_t data);

#endif
