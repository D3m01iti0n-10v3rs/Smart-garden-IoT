#ifndef SERVO_H
#define SERVO_H

#include "stm32f1xx_hal.h"

void servo_init(TIM_HandleTypeDef* htim, uint32_t channel);
void servo_write(uint32_t channel, uint8_t angle);

#endif
