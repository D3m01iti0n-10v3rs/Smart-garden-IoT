#ifndef INC_SOIL_HUMIDITY_H_
#define INC_SOIL_HUMIDITY_H_

#include "stm32f1xx_hal.h"

void soil_init(ADC_HandleTypeDef *hadc);
uint16_t soil_getRaw(uint32_t channel);
float soil_humConv(uint16_t adc);

#endif
