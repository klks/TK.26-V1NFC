/*
 * Buzzer on PB0: TIM1 update interrupt toggles the pin (2.72 kHz tone); beeps are sequenced
 * from the 1 ms tick (app_tick_1ms) or played blocking.
 */
#include "main.h"

TIM_HandleTypeDef TimHandle;
volatile uint8_t g_beep_count;
volatile uint32_t g_beep_on_ms;
volatile uint32_t g_beep_timer;

/* buzzer_timer_init 0x080007FC: 24 MHz / (183 + 1) / (23 + 1) = 5.43 kHz update rate. */
void buzzer_timer_init(void)
{
    TimHandle.Instance = TIM1;
    TimHandle.Init.Period = 23;
    TimHandle.Init.Prescaler = 183;
    TimHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    TimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    TimHandle.Init.RepetitionCounter = 0;
    TimHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&TimHandle) != HAL_OK)
    {
        APP_ErrorHandler();
    }
}

/* buzzer_on 0x0800357C. */
void buzzer_on(void)
{
    HAL_TIM_Base_Start_IT(&TimHandle);
}

/* buzzer_off 0x08003560: stop the timer and leave PB0 low. */
void buzzer_off(void)
{
    HAL_TIM_Base_Stop_IT(&TimHandle);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
}

/* beep_start 0x0800082C: non-blocking; app_tick_1ms plays count beeps of on_ms, 50 ms apart. */
void beep_start(uint8_t count, uint32_t on_ms)
{
    g_beep_on_ms = on_ms;
    g_beep_count = count;
}

/* beep_blocking 0x08000838. */
void beep_blocking(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    uint8_t i;

    for (i = 0; i < count; i++)
    {
        buzzer_on();
        delay_ms(on_ms);
        buzzer_off();
        delay_ms(off_ms);
    }
}

/* HAL_TIM_PeriodElapsedCallback 0x08001F34: one PB0 toggle per TIM1 update. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    (void)htim;
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
}
