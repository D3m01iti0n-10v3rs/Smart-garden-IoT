#include "soil_humidity.h"

static ADC_HandleTypeDef *adc;

static ADC_ChannelConfTypeDef sConfig = {0};

void soil_init(ADC_HandleTypeDef *hadc){
    adc = hadc;
}

uint16_t soil_getRaw(uint32_t channel){
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    HAL_ADC_ConfigChannel(adc, &sConfig);

    HAL_ADC_Start(adc);
    HAL_ADC_PollForConversion(adc, HAL_MAX_DELAY);

    uint16_t val = HAL_ADC_GetValue(adc);

    HAL_ADC_Stop(adc);

    return val;
}

float soil_humConv(uint16_t adc){
    float pct = 100.0f * (float)(ADC_DRY - adc) / (float)(ADC_DRY - ADC_WET);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

