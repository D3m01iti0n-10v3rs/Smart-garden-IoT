#ifndef BME280_H
#define BME280_H

#include "stm32f1xx_hal.h"

#define BME280_ADDR_LOW  (0x76 << 1)
#define BME280_ADDR_HIGH (0x77 << 1)

typedef struct {
    float temperature;   /* °C   */
    float pressure;      /* hPa  */
    float humidity;      /* %RH  */
} BME280_Data;

void     BME280_Init(I2C_HandleTypeDef *hi2c, uint8_t addr);
BME280_Data BME280_Read(void);

#endif
