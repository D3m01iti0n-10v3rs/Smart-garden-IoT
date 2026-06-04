#include "actuators.h"

void SIPO_setup(){
	HAL_GPIO_WritePin(SER_GPIO_Port, SER_Pin, 0);
	HAL_GPIO_WritePin(RCLK_GPIO_Port, RCLK_Pin, 0);
	HAL_GPIO_WritePin(SRCLK_GPIO_Port, SRCLK_Pin, 0);
}

void SIPO_write(uint8_t data){
	for (uint8_t i = 0; i < 8; i++){
		HAL_GPIO_WritePin(SER_GPIO_Port, SER_Pin, (data >> 7) & 1);
		data <<= 1;

		HAL_GPIO_WritePin(SRCLK_GPIO_Port, SRCLK_Pin, 1);
		HAL_GPIO_WritePin(SRCLK_GPIO_Port, SRCLK_Pin, 0);
	}
	HAL_GPIO_WritePin(RCLK_GPIO_Port, RCLK_Pin, 1);
	HAL_GPIO_WritePin(RCLK_GPIO_Port, RCLK_Pin, 0);
}
