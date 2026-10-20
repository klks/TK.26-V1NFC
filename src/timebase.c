/*
 * Watchdog feeding and the delays every other module uses.
 */
#include "main.h"

volatile uint32_t g_wait_timer_ms;
volatile uint32_t g_tick_ms;
volatile uint32_t g_poll_counter;

/*
 * app_tick_1ms 0x08003F64, from SysTick_Handler after HAL_IncTick: the free-running wait timer,
 * the 10 ms pulse scan, the 15 s learn-mode timeout, UART idle detection, the 50 ms card-poll
 * pacing and the beep sequencer (on for g_beep_on_ms, then off until on_ms + 50).
 */
void app_tick_1ms(void)
{
    g_wait_timer_ms++;
    g_tick_ms++;
    if (g_tick_ms % 10 == 0)
    {
        vcc_pulse_scan();
    }
    if (g_learn_mode)
    {
        if (g_learn_timer_ms >= 15000)
        {
            g_learn_timer_ms = 0;
            g_learn_mode = 0;
            beep_start(1, 500);
        }
        else
        {
            g_learn_timer_ms++;
        }
    }
    uart_rx_idle_poll();
    if (g_tick_ms % 50 == 0)
    {
        g_card_poll_due = 1;
        g_poll_counter++;
    }
    if (g_beep_count != 0)
    {
        if (g_beep_timer == 0)
        {
            buzzer_on();
        }
        else if (g_beep_timer == g_beep_on_ms)
        {
            buzzer_off();
        }
        if (g_beep_timer >= g_beep_on_ms + 50)
        {
            g_beep_timer = 0;
            g_beep_count--;
        }
        else
        {
            g_beep_timer++;
        }
    }
    else
    {
        g_beep_timer = 0;
    }
}

/* iwdg_feed 0x08003630. */
void iwdg_feed(void)
{
    if (HAL_IWDG_Refresh(&IwdgHandle) != HAL_OK)
    {
        APP_ErrorHandler();
    }
}

/*
 * delay_ms 0x0800358C: one HAL_Delay(1) and one watchdog feed per millisecond, so no delay
 * ever starves the IWDG. The vendor loop decrements first and stops at -1, i.e. ms passes.
 */
void delay_ms(uint32_t ms)
{
    while (ms-- != 0)
    {
        HAL_Delay(1);
        iwdg_feed();
    }
}

/*
 * delay_loop 0x080035A4: (count - 1) outer passes of a 3-iteration inner loop. The vendor
 * kept the loop in a register; the counter is volatile here so GCC does not delete it.
 * Absolute timing differs from the Keil build (different instructions); only the order of
 * magnitude (tens of microseconds for nfc_init_protocol's 500) matters to its caller.
 */
void delay_loop(uint32_t count)
{
    volatile uint32_t inner;

    while (--count != 0)
    {
        inner = 3;
        do
        {
            --inner;
        } while (inner != 0);
    }
}
