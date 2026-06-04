#include "JSN-SR04T.h"

float JSNSR04T_GetDuration(void){
    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 0);
    delay_us(2);
    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 1);
    delay_us(20);
    HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, 0);

    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while(HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 0)
        if(__HAL_TIM_GET_COUNTER(&htim2) > 30000) return 0;

    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while(HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == 1)
        if(__HAL_TIM_GET_COUNTER(&htim2) > 30000) return 0;

    return (float)__HAL_TIM_GET_COUNTER(&htim2);
}

float JSNSR04T_Conv_mm(float duration){
	return (duration / 2) * 0.343;
}

