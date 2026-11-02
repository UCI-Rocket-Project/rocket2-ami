#include "main.h"
#include "cpp_main.h"
#include "stm32f1xx_hal_tim.h"
#include <math.h>

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

extern UART_HandleTypeDef huart4;

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

void set_red_brightness(float duty_cycle) {
    if (duty_cycle > 1) duty_cycle = 1;
    if (duty_cycle < 0) duty_cycle = 0;
    uint32_t pulse = 36000U * (1.0f - duty_cycle) / 1.0f;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pulse);
}

void set_green_brightness(float duty_cycle) {
    if (duty_cycle > 1) duty_cycle = 1;
    if (duty_cycle < 0) duty_cycle = 0;
    uint32_t pulse = 36000U * (1.0f - duty_cycle) / 1.0f;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, pulse);
}

void set_blue_brightness(float duty_cycle) {
    if (duty_cycle > 100) duty_cycle = 100;
    uint32_t pulse = 36000U * (100U - duty_cycle) / 100U;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, pulse);
}

void cpp_main(void) {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4); // Red
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3); // Green
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4); // Blue

    float ms = 0;

    while (1) {
        float phase = 2.0f * 3.141592f * ms / 2000.0f;
        float green_wave = 0.5f * sinf(phase + 2.0f) + 0.5f;  // 0 to +1
        set_red_brightness(0);
        set_green_brightness(green_wave * 0.2f);
        set_blue_brightness(0);

        ms++;
        HAL_Delay(1);
    }
}