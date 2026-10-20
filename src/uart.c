/*
 * USART1 debug log, 115200 8N1 on PA3/PA4. Output only: the receive interrupt buffers bytes and
 * an idle timer closes a frame, but nothing in the firmware reads the buffer. printf (printf.c)
 * writes through uart_putc, as the vendor's fputc retarget 0x08003620 does.
 */
#include "main.h"

UART_HandleTypeDef UartHandle;
volatile uint8_t g_uart_rx_byte;
volatile uint8_t g_uart_rx_len;
volatile uint8_t g_uart_rx_lock;
volatile uint8_t g_uart_rx_done;
volatile uint8_t g_uart_rx_frame_len;
volatile uint32_t g_uart_rx_idle_ms;
volatile uint8_t g_uart_rx_buf[128];

/* uart_init 0x08004018. */
void uart_init(void)
{
    UartHandle.Instance = USART1;
    UartHandle.Init.BaudRate = 115200;
    UartHandle.Init.WordLength = UART_WORDLENGTH_8B;
    UartHandle.Init.StopBits = UART_STOPBITS_1;
    UartHandle.Init.Parity = UART_PARITY_NONE;
    UartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    UartHandle.Init.Mode = UART_MODE_TX_RX;
    UartHandle.Init.OverSampling = UART_OVERSAMPLING_16;
    UartHandle.AdvancedInit.AdvFeatureInit = 0;
    HAL_UART_Init(&UartHandle);
    __HAL_UART_ENABLE_IT(&UartHandle, UART_IT_RXNE);
}

/* uart_putc 0x08004004: wait for TXE, write DR. */
void uart_putc(uint8_t ch)
{
    while ((UartHandle.Instance->SR & USART_SR_TXE) == 0)
    {
    }
    UartHandle.Instance->DR = ch;
}

/*
 * uart_rx_idle_poll 0x08002E64, every 1 ms: after 2 ms without a byte, close the frame.
 * g_uart_rx_lock is held while the frame is closed (0x08002E7C) so USART1_IRQHandler does
 * not append to it meanwhile.
 */
void uart_rx_idle_poll(void)
{
    if (g_uart_rx_idle_ms >= 2)
    {
        if (g_uart_rx_len != 0)
        {
            g_uart_rx_lock = 1;
            g_uart_rx_buf[g_uart_rx_len] = 0;
            g_uart_rx_done = 1;
            g_uart_rx_frame_len = g_uart_rx_len;
            g_uart_rx_len = 0;
            g_uart_rx_idle_ms = 0;
            g_uart_rx_lock = 0;
        }
    }
    else
    {
        g_uart_rx_idle_ms++;
    }
}
