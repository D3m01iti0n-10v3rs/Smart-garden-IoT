#include "servo.h"

static TIM_HandleTypeDef* _htim;

void servo_init(TIM_HandleTypeDef* htim, uint32_t channel) {
    _htim = htim;
    HAL_TIM_PWM_Start(_htim, channel);
}

void servo_write(uint32_t channel, uint8_t angle) {
    if (angle > 180) angle = 180;
    uint32_t pulse = 1000 + (angle * 1000 / 180);
    __HAL_TIM_SET_COMPARE(_htim, channel, pulse);
}
