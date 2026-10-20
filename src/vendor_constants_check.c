/*
 * Compile-time checks (no code): every HAL/CMSIS macro and struct layout the port relies on
 * must equal the immediate the vendor code uses in ida/firmware.asm. A failing assert names
 * the vendor address to re-read. Added in Phase 7.
 */
#include <stddef.h>

#include "main.h"

#define VENDOR(expr, value, where) _Static_assert((expr) == (value), where)

/* main 0x08003748, option_bytes_apply 0x08000660 */
VENDOR(OB_USER_SWD_NRST_MODE, 0x6000, "main: OPTR << 17 >> 30 (bits 14:13)");
VENDOR(OB_SWD_PB6_GPIO_PC0, 0x4000, "main: CMP R0, #2");
VENDOR(IWDG_PRESCALER_32, 3, "main 0x08003768");
VENDOR(OPTIONBYTE_USER, 8, "option_bytes_apply 0x08000674");
VENDOR(OB_USER_BOR_EN | OB_USER_BOR_LEV | OB_USER_IWDG_SW | OB_USER_SWD_NRST_MODE, 0x7F00,
       "option_bytes_apply 0x08000678");
VENDOR(OB_BOR_DISABLE | OB_BOR_LEVEL_3p1_3p2 | OB_IWDG_SW | OB_SWD_PB6_GPIO_PC0, 0x5E00,
       "option_bytes_apply 0x0800067C");
VENDOR(offsetof(FLASH_OBProgramInitTypeDef, OptionType), 0x00, "option_bytes_apply");
VENDOR(offsetof(FLASH_OBProgramInitTypeDef, USERType), 0x10, "option_bytes_apply");
VENDOR(offsetof(FLASH_OBProgramInitTypeDef, USERConfig), 0x14, "option_bytes_apply");
VENDOR(sizeof(FLASH_OBProgramInitTypeDef), 0x18, "option_bytes_apply memclr");

/* config_erase 0x08000628, config_program 0x0800069C */
VENDOR(FLASH_TYPEERASE_SECTORERASE, 0x800, "config_erase 0x08000640");
VENDOR(offsetof(FLASH_EraseInitTypeDef, TypeErase), 0x00, "config_erase");
VENDOR(offsetof(FLASH_EraseInitTypeDef, SectorAddress), 0x0C, "config_erase");
VENDOR(offsetof(FLASH_EraseInitTypeDef, NbSectors), 0x10, "config_erase");
VENDOR(sizeof(FLASH_EraseInitTypeDef), 0x14, "config_erase memclr");
VENDOR(FLASH_TYPEPROGRAM_PAGE, 1, "config_program 0x080006AC");
VENDOR(sizeof(config_t), 0xC8, "config_verify_erased / config_program bound");

/* SystemClock_Config 0x0800079C */
VENDOR(RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSE |
       RCC_OSCILLATORTYPE_LSI, 0xF, "SystemClock_Config");
VENDOR(RCC_HSI_ON, 0x100, "SystemClock_Config");
VENDOR(RCC_HSI_DIV1, 0, "SystemClock_Config");
VENDOR(RCC_HSE_BYPASS_DISABLE, 0, "SystemClock_Config");
VENDOR(RCC_LSI_OFF, 0, "SystemClock_Config");
VENDOR(RCC_LSE_OFF, 0, "SystemClock_Config");
VENDOR(offsetof(RCC_OscInitTypeDef, OscillatorType), 0x00, "SystemClock_Config");
VENDOR(offsetof(RCC_OscInitTypeDef, HSIState), 0x10, "SystemClock_Config");
VENDOR(offsetof(RCC_OscInitTypeDef, HSIDiv), 0x14, "SystemClock_Config");
VENDOR(offsetof(RCC_OscInitTypeDef, HSICalibrationValue), 0x18, "SystemClock_Config");
VENDOR(sizeof(RCC_OscInitTypeDef), 0x24, "SystemClock_Config memclr");
VENDOR(RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1, 7, "SystemClock_Config");
VENDOR(RCC_SYSCLKSOURCE_HSISYS, 0, "SystemClock_Config");
VENDOR(RCC_SYSCLK_DIV1, 0, "SystemClock_Config");
VENDOR(RCC_HCLK_DIV1, 0, "SystemClock_Config");
VENDOR(FLASH_LATENCY_0, 0, "SystemClock_Config");

/* gpio_init 0x080006C8, HAL_UART_MspInit 0x08001FB4, nfc_irq_config 0x08000AD8 */
VENDOR(GPIO_MODE_INPUT, 0, "gpio_init");
VENDOR(GPIO_MODE_OUTPUT_PP, 1, "gpio_init");
VENDOR(GPIO_MODE_AF_PP, 2, "HAL_UART_MspInit");
VENDOR(GPIO_MODE_ANALOG, 3, "gpio_init PB1");
VENDOR(GPIO_MODE_IT_FALLING, 0x10210000, "nfc_irq_config");
VENDOR(GPIO_NOPULL, 0, "gpio_init");
VENDOR(GPIO_PULLUP, 1, "gpio_init");
VENDOR(GPIO_SPEED_FREQ_HIGH, 2, "gpio_init");
VENDOR(GPIO_AF1_USART1, 1, "HAL_UART_MspInit");
VENDOR(offsetof(GPIO_InitTypeDef, Pin), 0x00, "gpio_init");
VENDOR(offsetof(GPIO_InitTypeDef, Mode), 0x04, "gpio_init");
VENDOR(offsetof(GPIO_InitTypeDef, Pull), 0x08, "gpio_init");
VENDOR(offsetof(GPIO_InitTypeDef, Speed), 0x0C, "gpio_init");
VENDOR(offsetof(GPIO_InitTypeDef, Alternate), 0x10, "HAL_UART_MspInit");
VENDOR(sizeof(GPIO_InitTypeDef), 0x14, "gpio_init memclr");
VENDOR(GPIOA_BASE, 0x50000000, "MOVS R0, #5; LSLS R0, #0x1C");

/* adc_init 0x08000518, adc_read_channel 0x08000580, vcc_measure 0x080038F8 */
VENDOR(ADC_CLOCK_SYNC_PCLK_DIV4, 0x20000000, "adc_init: ADC1 << 19");
VENDOR(ADC_RESOLUTION_12B, 0, "adc_init");
VENDOR(ADC_DATAALIGN_RIGHT, 0, "adc_init");
VENDOR(ADC_SCAN_DIRECTION_FORWARD, 1, "adc_init");
VENDOR(ADC_EOC_SINGLE_CONV, 4, "adc_init");
VENDOR(ADC_SOFTWARE_START, 0x1C1, "adc_init");
VENDOR(ADC_EXTERNALTRIGCONVEDGE_NONE, 0, "adc_init");
VENDOR(ADC_OVR_DATA_OVERWRITTEN, 0, "adc_init");
VENDOR(ADC_SAMPLETIME_239CYCLES_5, 7, "adc_init");
VENDOR(ADC_RANK_CHANNEL_NUMBER, 0x1000, "adc_read_channel");
VENDOR(ADC_RANK_NONE, 0x1001, "adc_read_channel");
VENDOR(ADC_CHANNEL_0, 0, "vcc_measure: MOVS R0, #0");
VENDOR(ADC_CR_ADEN, 1, "adc_read_channel: CR << 31");
VENDOR(ADC_CR_ADDIS, 2, "adc_read_channel: CR |= 2");
VENDOR(ADC_FLAG_EOSMP, 2, "adc_read_channel: ISR = 2");
VENDOR(RCC_APBENR2_ADCEN, 0x100000, "adc_init clock");

/* buzzer_timer_init 0x080007FC, HAL_TIM_Base_MspInit 0x08001D44 */
VENDOR(TIM_CLOCKDIVISION_DIV1, 0, "buzzer_timer_init");
VENDOR(TIM_COUNTERMODE_UP, 0, "buzzer_timer_init");
VENDOR(TIM_AUTORELOAD_PRELOAD_DISABLE, 0, "buzzer_timer_init");
VENDOR(RCC_APBENR2_TIM1EN, 0x800, "HAL_TIM_Base_MspInit");
VENDOR(TIM1_BRK_UP_TRG_COM_IRQn, 13, "HAL_TIM_Base_MspInit: ISER = 0x2000");

/* uart_init 0x08004018, uart_putc 0x08004004, USART1_IRQHandler 0x08002F64 */
VENDOR(UART_WORDLENGTH_8B, 0, "uart_init");
VENDOR(UART_STOPBITS_1, 0, "uart_init");
VENDOR(UART_PARITY_NONE, 0, "uart_init");
VENDOR(UART_HWCONTROL_NONE, 0, "uart_init");
VENDOR(UART_MODE_TX_RX, 0xC, "uart_init");
VENDOR(UART_OVERSAMPLING_16, 0, "uart_init");
VENDOR(USART_CR1_RXNEIE, 0x20, "uart_init: CR1 |= 0x20");
VENDOR(USART_SR_TXE, 0x80, "uart_putc: ~SR << 24");
VENDOR(UART_FLAG_RXNE, 0x20, "USART1_IRQHandler: ~SR << 26");
VENDOR(UART_FLAG_ORE, 0x08, "USART1_IRQHandler: ~SR << 28");
VENDOR(RCC_APBENR2_USART1EN, 0x4000, "HAL_UART_MspInit");
VENDOR(USART1_IRQn, 0x1B, "HAL_UART_MspInit");
VENDOR(EXTI4_15_IRQn, 7, "nfc_irq_config");

/* HAL_MspInit 0x0800178C */
VENDOR(RCC_APBENR2_SYSCFGEN, 1, "HAL_MspInit");
VENDOR(RCC_APBENR1_PWREN, 0x10000000, "HAL_MspInit: RCC << 16");
