#ifndef INC_JSN_SR04T_H_
#define INC_JSN_SR04T_H_

#include "main.h"

extern TIM_HandleTypeDef htim2;

void delay_us(uint16_t us);

float JSNSR04T_GetDuration(void);
float JSNSR04T_Conv_mm(float duration);

#endif /* INC_JSN_SR04T_H_ */
