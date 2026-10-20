/*
 * Bike RFID reader firmware (PY32F002B), rebuilt from ida/firmware.asm.
 *
 * main() and the board bring-up it calls. Each function cites the vendor address it was
 * checked against, instruction by instruction, in the listing.
 *
 * The whole vendor main is ported (Phase 6).
 */
#include <stdio.h>
#include <string.h>

#include "main.h"

/* 0x20000060. main: Instance IWDG, Prescaler /32, Reload 2048 (~2 s at LSI 32.768 kHz). */
IWDG_HandleTypeDef IwdgHandle;

static void SystemClock_Config(void);
static void gpio_init(void);
#if BIKE_WRITE_OPTION_BYTES
static void option_bytes_apply(void);
#endif

/* main 0x08003748. */
int main(void)
{
    int i;
    int vcc;

    HAL_Init();
    SystemClock_Config();

    /* OPTR[14:13] (NRST_MODE, SWD_MODE) must read OB_SWD_PB6_GPIO_PC0; the vendor
       tests it as (OPTR << 17 >> 30) != 2. */
    if ((FLASH->OPTR & OB_USER_SWD_NRST_MODE) != OB_SWD_PB6_GPIO_PC0)
    {
#if BIKE_WRITE_OPTION_BYTES
        option_bytes_apply();
#endif
    }

    IwdgHandle.Instance = IWDG;
    IwdgHandle.Init.Prescaler = IWDG_PRESCALER_32;
    IwdgHandle.Init.Reload = 2048;
    if (HAL_IWDG_Init(&IwdgHandle) != HAL_OK)
    {
        APP_ErrorHandler();
    }
    iwdg_feed();

    gpio_init();
    /* Output MOSFET (PA1) off, PA0 low. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    adc_init();
    buzzer_timer_init();
    buzzer_off();
    uart_init();
    printf("UART_OK_V2.0\r\n");
    nfc_irq_config(0);
    config_load();
    printf("id_num=%d\r\n", card_table_count());

    /* Power-on supply check: settle the ADC with 20 readings, then one reading with the
       output on for 4 ms. Under 24 V -> 5 beeps; else 1 beep, then 1 more (protection on)
       or 2 (off). beep_start only sets the count, so the second call replaces the first. */
    for (i = 0; i < 20; i++)
    {
        vcc_measure();
    }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    delay_ms(4);
    vcc_measure();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    vcc = (int)g_vcc;
    printf("power_VCC=%d\r\n", (int)g_vcc);
    if (vcc >= 24)
    {
        beep_start(1, 180);
        delay_ms(90);
        if (g_protect_enabled)
        {
            beep_start(1, 180);
        }
        else
        {
            beep_start(2, 180);
        }
    }
    else
    {
        beep_start(5, 180);
    }
    printf("BU_FG=%d\r\n", (int)g_protect_enabled);
    iwdg_feed();
    delay_ms(300);
    iwdg_feed();
    nfc_reset();
    iwdg_feed();

    /* Every task is bracketed by a watchdog feed and the over-current check. */
    for (;;)
    {
        iwdg_feed();
        overcurrent_check();
        pulse_command_task();
        iwdg_feed();
        overcurrent_check();
        iwdg_feed();
        card_poll_task();
        overcurrent_check();
        iwdg_feed();
        card_learn_task();
        overcurrent_check();
        iwdg_feed();
        card_normal_task();
    }
}

/*
 * SystemClock_Config 0x0800079C. HSI on at 24 MHz with the factory trim, /1; SYSCLK = HSISYS,
 * AHB and APB /1, flash latency 0. HSE, LSI and LSE are all named in OscillatorType and
 * switched off. LSICalibrationValue is left at 0 (memset), as in the vendor code.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;

    memset(&RCC_OscInitStruct, 0, sizeof(RCC_OscInitStruct));
    memset(&RCC_ClkInitStruct, 0, sizeof(RCC_ClkInitStruct));

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI |
                                       RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_LSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS_DISABLE;
    RCC_OscInitStruct.LSIState = RCC_LSI_OFF;
    RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        APP_ErrorHandler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        APP_ErrorHandler();
    }
}

/*
 * gpio_init 0x080006C8. Clocks B, A, C in that order (each __HAL_RCC_GPIOx_CLK_ENABLE has the
 * read-back the pseudocode hides), then eight HAL_GPIO_Init calls reusing one struct. The PB1
 * call sets only Pin/Mode/Pull, so its Speed stays GPIO_SPEED_FREQ_HIGH from the call before.
 */
static void gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA1: output MOSFET gate. */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA0: output, held low. */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB0 buzzer, PB3 NFC MOSI, PB5 NFC NRSTPD. */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_3 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB2: NFC SCK. */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB4: NFC MISO. */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB7: NFC IRQ (left as a plain input; nfc_irq_config(0) in main). */
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PB1: ADC_IN0, supply sense. Speed deliberately not set (see above). */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PC1: NFC NSS (chip select). */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

#if BIKE_WRITE_OPTION_BYTES
/*
 * option_bytes_apply 0x08000660. USER = 0x5E00 under mask 0x7F00: BOR off at level 3.1/3.2 V,
 * software IWDG, PC0 as GPIO with SWD kept on PB6/PA2. Then OB_Launch, which resets the chip.
 * WRP, SDK and RDP are never touched.
 */
static void option_bytes_apply(void)
{
    FLASH_OBProgramInitTypeDef OBInitCfg;

    memset(&OBInitCfg, 0, sizeof(OBInitCfg));
    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();
    OBInitCfg.OptionType = OPTIONBYTE_USER;
    OBInitCfg.USERType = OB_USER_BOR_EN | OB_USER_BOR_LEV | OB_USER_IWDG_SW | OB_USER_SWD_NRST_MODE;
    OBInitCfg.USERConfig = OB_BOR_DISABLE | OB_BOR_LEVEL_3p1_3p2 | OB_IWDG_SW | OB_SWD_PB6_GPIO_PC0;
    HAL_FLASH_OBProgram(&OBInitCfg);
    HAL_FLASH_Lock();
    HAL_FLASH_OB_Lock();
    HAL_FLASH_OB_Launch();
}
#endif

/* APP_ErrorHandler 0x08000604: B . (callers' HAL status in R0 is ignored). */
void APP_ErrorHandler(void)
{
    for (;;)
    {
    }
}
