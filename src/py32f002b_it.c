/*
 * Exception and interrupt handlers (py32f002b_it.c in the vendor project).
 */
#include <stdio.h>

#include "main.h"

/* NMI_Handler 0x08002080: empty. */
void NMI_Handler(void)
{
}

/* HardFault_Handler 0x08002024: B . */
void HardFault_Handler(void)
{
    for (;;)
    {
    }
}

/* SVC_Handler 0x08002D2C: empty. */
void SVC_Handler(void)
{
}

/* PendSV_Handler 0x08002C6C: empty. */
void PendSV_Handler(void)
{
}

/* SysTick_Handler 0x08002D46. */
void SysTick_Handler(void)
{
    HAL_IncTick();
    app_tick_1ms();
}

/*
 * EXTI4_15_IRQHandler 0x08000AB8: logs and clears every EXTI line. Unused at runtime - the
 * NFC IRQ pin is configured without its interrupt (nfc_irq_config(0) in main).
 */
void EXTI4_15_IRQHandler(void)
{
    printf("INT_NFC\r\n");
    HAL_GPIO_EXTI_IRQHandler(0xFFFF);
}

/* TIM1_BRK_UP_TRG_COM_IRQHandler 0x08002DD4: the buzzer timer. */
void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&TimHandle);
}

/*
 * USART1_IRQHandler 0x08002F64 (not the HAL handler): RXNE with its interrupt enabled ->
 * append the byte (up to 128) and restart the idle timer; then clear an overrun by reading
 * SR and DR. The pseudocode hides that last step; the listing has it (0x08002FA0).
 */
void USART1_IRQHandler(void)
{
    uint8_t ch;

    if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_RXNE) &&
        (UartHandle.Instance->CR1 & USART_CR1_RXNEIE) != 0)
    {
        ch = (uint8_t)UartHandle.Instance->DR;
        g_uart_rx_byte = ch;
        g_uart_rx_idle_ms = 0;
        if (!g_uart_rx_lock && g_uart_rx_len < 128)
        {
            g_uart_rx_buf[g_uart_rx_len] = ch;
            g_uart_rx_len++;
        }
    }
    if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_ORE))
    {
        __HAL_UART_CLEAR_OREFLAG(&UartHandle);
    }
}
