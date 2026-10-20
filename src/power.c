/*
 * Supply measurement on PB1 (ADC_IN0), the supply-pulse command channel, and the over-current
 * cut-off for the output MOSFET.
 */
#include <stdio.h>

#include "main.h"
#include "log_strings.h"

ADC_HandleTypeDef AdcHandle;
ADC_ChannelConfTypeDef sConfig;
volatile uint32_t g_vcc;
volatile uint8_t g_pulse_count;
volatile uint8_t g_pulse_event;
volatile uint8_t g_pulse_long;
volatile uint32_t g_pulse_high_ticks;
volatile uint32_t g_pulse_low_samples;
volatile uint32_t g_pulse_settle_ticks;

/* adc_init 0x08000518. */
void adc_init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    AdcHandle.Instance = ADC1;
    AdcHandle.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    AdcHandle.Init.Resolution = ADC_RESOLUTION_12B;
    AdcHandle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    AdcHandle.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
    AdcHandle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    AdcHandle.Init.LowPowerAutoWait = DISABLE;
    AdcHandle.Init.ContinuousConvMode = DISABLE;
    AdcHandle.Init.DiscontinuousConvMode = DISABLE;
    AdcHandle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    AdcHandle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    AdcHandle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    AdcHandle.Init.SamplingTimeCommon = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_Init(&AdcHandle) != HAL_OK)
    {
        APP_ErrorHandler();
    }
    if (HAL_ADC_Calibration_Start(&AdcHandle) != HAL_OK)
    {
        APP_ErrorHandler();
    }
}

/*
 * adc_read_channel 0x08000580: disable the ADC if it is enabled (channel selection needs it
 * off), select the channel, one polled conversion, disable again, deselect the channel.
 */
uint32_t adc_read_channel(uint32_t channel)
{
    uint32_t value;

    if ((AdcHandle.Instance->CR & ADC_CR_ADEN) != 0)
    {
        __HAL_ADC_DISABLE(&AdcHandle);
    }
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    sConfig.Channel = channel;
    if (HAL_ADC_ConfigChannel(&AdcHandle, &sConfig) != HAL_OK)
    {
        APP_ErrorHandler();
    }
    HAL_ADC_Start(&AdcHandle);
    HAL_ADC_PollForConversion(&AdcHandle, 1000000);
    value = (uint16_t)HAL_ADC_GetValue(&AdcHandle);
    __HAL_ADC_DISABLE(&AdcHandle);
    sConfig.Rank = ADC_RANK_NONE;
    sConfig.Channel = channel;
    if (HAL_ADC_ConfigChannel(&AdcHandle, &sConfig) != HAL_OK)
    {
        APP_ErrorHandler();
    }
    return value;
}

/*
 * vcc_measure 0x080038F8: average of three ADC_IN0 samples, scaled to volts. The listing does
 * the arithmetic in exactly this order and these types: unsigned (avg * 5000 / 409500), then a
 * double multiply by 3.2, rounded to float, truncated to unsigned.
 */
int vcc_measure(void)
{
    uint32_t sum = 0;
    int i;
    float volts;

    for (i = 0; i < 3; i++)
    {
        sum += adc_read_channel(ADC_CHANNEL_0);
    }
    volts = (float)((double)(sum / 3 * 5000 / 409500) * 3.2);
    g_vcc = (uint32_t)volts;
    return (int)g_vcc;
}

/* vcc_is_high 0x0800206C. */
int vcc_is_high(void)
{
    return g_vcc > 20;
}

/*
 * vcc_pulse_scan 0x08003648, every 10 ms from app_tick_1ms. A "pulse" is the measured supply
 * high for 20 ms .. 9 s; after 1 s with no new pulse, g_pulse_event reports g_pulse_count.
 */
void vcc_pulse_scan(void)
{
    if (g_pulse_settle_ticks < 500)
    {
        if (vcc_is_high())
        {
            if (!g_output_on && g_pulse_high_ticks == 2)
            {
                beep_start(1, 70);
            }
            g_pulse_high_ticks++;
            g_pulse_settle_ticks = 1;
            if (g_pulse_high_ticks == 900)
            {
                g_pulse_long = 1;
            }
        }
        else
        {
            if (g_pulse_high_ticks - 2 < 898)
            {
                g_pulse_count++;
            }
            g_pulse_high_ticks = 0;
            g_pulse_low_samples++;
        }
    }
    if (g_pulse_count != 0 && g_pulse_settle_ticks != 0)
    {
        if (g_pulse_settle_ticks >= 100)
        {
            g_pulse_event = 1;
            g_pulse_settle_ticks = 0;
        }
        else
        {
            g_pulse_settle_ticks++;
        }
    }
}

/* pulse_command_task 0x080036C8: 4 pulses = factory reset, 8 pulses = toggle protection. */
void pulse_command_task(void)
{
    if (g_pulse_event)
    {
        if (g_pulse_count == 4)
        {
            beep_blocking(2, 500, 500);
            g_protect_enabled = 1;
            card_table_clear();
            printf(STR_FACTORY_RESET);
        }
        if (g_pulse_count == 8)
        {
            if (g_protect_enabled)
            {
                g_protect_enabled = 0;
                beep_blocking(3, 200, 200);
            }
            else
            {
                g_protect_enabled = 1;
                beep_blocking(2, 200, 200);
            }
            printf("BH_FG=%d\r\n", (int)g_protect_enabled);
            config_save();
        }
        g_pulse_event = 0;
        g_pulse_count = 0;
    }
}

/*
 * overcurrent_check 0x080034CC, between every main-loop task: with protection on and the
 * output on, a supply reading under 25 V confirmed 4 ms later switches the output off.
 */
void overcurrent_check(void)
{
    vcc_measure();
    if (g_protect_enabled && g_output_on && g_vcc < 25)
    {
        delay_ms(4);
        vcc_measure();
        if (g_vcc >= 25)
        {
            printf(STR_OVERCURRENT_BAD_SAMPLE);
        }
        else
        {
            g_output_on = 0;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
            beep_start(1, 2000);
            printf("out_vcc=%d\r\n", (int)g_vcc);
            printf(STR_OVERCURRENT);
        }
    }
}
