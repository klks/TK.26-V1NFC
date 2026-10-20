/*
 * HAL MSP callbacks (py32f002b_hal_msp.c in the vendor project).
 */
#include <string.h>

#include "main.h"

/* HAL_MspInit 0x0800178C: SYSCFG and PWR clocks (each with the HAL clock-enable read-back). */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

/*
 * HAL_TIM_Base_MspInit 0x08001D44: TIM1 clock, then the CMSIS inlines directly (the vendor does
 * not call HAL_NVIC_*): enable the update interrupt first, then give it priority 0.
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    (void)htim;
    __HAL_RCC_TIM1_CLK_ENABLE();
    NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, 0);
}

/*
 * HAL_UART_MspInit 0x08001FB4: USART1 on PA4 then PA3, AF1, push-pull with pull-up; the
 * second init reuses the struct and only changes Pin (and rewrites Alternate). USART1 IRQ at
 * priority 0 (sub-priority argument 1, ignored on Cortex-M0+), enabled.
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    (void)huart;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART1_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}
