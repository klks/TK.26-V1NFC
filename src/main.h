/*
 * Bike RFID reader firmware (PY32F002B), rebuilt from ida/firmware.asm.
 * Shared declarations for the application files. Each global carries its vendor SRAM address;
 * `volatile` marks the ones shared between the SysTick/USART interrupts and the main loop.
 */
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#include "py32f0xx_hal.h"

/*
 * 1 = main() writes the USER option bytes on a chip whose NRST/SWD mode is not
 * OB_SWD_PB6_GPIO_PC0, exactly as the vendor firmware does (main 0x08003748 ->
 * option_bytes_apply 0x08000660), followed by an option-byte reload (reset).
 * Off by default: it turns PC0/NRST into a GPIO on the board it runs on (CLAUDE.md).
 */
#ifndef BIKE_WRITE_OPTION_BYTES
#define BIKE_WRITE_OPTION_BYTES 0
#endif

/* ---- main.c ---- */
extern IWDG_HandleTypeDef IwdgHandle;
void APP_ErrorHandler(void);

/* ---- timebase.c ---- */
extern volatile uint32_t g_wait_timer_ms;   /* 0x20000020 */
extern volatile uint32_t g_tick_ms;         /* 0x20000024 */
extern volatile uint32_t g_poll_counter;    /* 0x2000001C, never read */
void iwdg_feed(void);
void delay_ms(uint32_t ms);
void delay_loop(uint32_t count);
void app_tick_1ms(void);

/* ---- uart.c ---- */
extern UART_HandleTypeDef UartHandle;       /* 0x20000144 */
extern volatile uint8_t g_uart_rx_byte;     /* 0x20000040 */
extern volatile uint8_t g_uart_rx_len;      /* 0x2000006C */
extern volatile uint8_t g_uart_rx_lock;     /* 0x2000006D, held by uart_rx_idle_poll */
extern volatile uint8_t g_uart_rx_done;     /* 0x2000006E, never consumed */
extern volatile uint8_t g_uart_rx_frame_len; /* 0x2000006F */
extern volatile uint32_t g_uart_rx_idle_ms; /* 0x20000070 */
extern volatile uint8_t g_uart_rx_buf[128]; /* 0x20000074, never parsed */
void uart_init(void);
void uart_putc(uint8_t ch);
void uart_rx_idle_poll(void);

/* ---- buzzer.c ---- */
extern TIM_HandleTypeDef TimHandle;         /* 0x20000188 */
extern volatile uint8_t g_beep_count;       /* 0x20000004 */
extern volatile uint32_t g_beep_on_ms;      /* 0x20000014 */
extern volatile uint32_t g_beep_timer;      /* 0x20000018 */
void buzzer_timer_init(void);
void buzzer_on(void);
void buzzer_off(void);
void beep_start(uint8_t count, uint32_t on_ms);
void beep_blocking(uint8_t count, uint32_t on_ms, uint32_t off_ms);

/* ---- power.c ---- */
extern ADC_HandleTypeDef AdcHandle;         /* 0x200001A8 */
extern ADC_ChannelConfTypeDef sConfig;      /* 0x200001E0 */
extern volatile uint32_t g_vcc;             /* 0x20000010, volts */
extern volatile uint8_t g_pulse_count;      /* 0x20000009 */
extern volatile uint8_t g_pulse_event;      /* 0x2000000A */
extern volatile uint8_t g_pulse_long;       /* 0x2000000B, never read */
extern volatile uint32_t g_pulse_high_ticks;  /* 0x20000034, 10 ms units */
extern volatile uint32_t g_pulse_low_samples; /* 0x20000038, never read */
extern volatile uint32_t g_pulse_settle_ticks; /* 0x2000003C, 10 ms units */
void adc_init(void);
uint32_t adc_read_channel(uint32_t channel);
int vcc_measure(void);
int vcc_is_high(void);
void vcc_pulse_scan(void);
void pulse_command_task(void);
void overcurrent_check(void);

/* ---- nfc.c (WS1820T, MFRC522-compatible, bit-banged SPI) ---- */
/* Front-end registers the firmware uses (MFRC522 names; the WS1820T maps them the same). */
enum
{
    CommandReg = 0x01,
    ComIEnReg = 0x02,
    DivIEnReg = 0x03,
    ComIrqReg = 0x04,
    DivIrqReg = 0x05,
    ErrorReg = 0x06,
    Status1Reg = 0x07,
    Status2Reg = 0x08,
    FIFODataReg = 0x09,
    FIFOLevelReg = 0x0A,
    ControlReg = 0x0C,
    BitFramingReg = 0x0D,
    CollReg = 0x0E,
    ModeReg = 0x11,
    TxModeReg = 0x12,
    RxModeReg = 0x13,
    TxControlReg = 0x14,
    TxASKReg = 0x15,
    RxSelReg = 0x17,
    RxThresholdReg = 0x18,
    TypeBReg = 0x1E,
    RFCfgReg = 0x26,
    GsNReg = 0x27,
    CWGsPReg = 0x28,
    ModGsPReg = 0x29,
    TModeReg = 0x2A,
    TPrescalerReg = 0x2B,
    TReloadRegH = 0x2C,
    TReloadRegL = 0x2D,
    TestSel2Reg = 0x32,
    TestPinEnReg = 0x33,
    TestBusReg = 0x35,
    AutoTestReg = 0x36,
    VersionReg = 0x37,
    TestDAC2Reg = 0x3A,
    TestADCReg = 0x3B,
};

/* PCD commands (CommandReg). PCD_INVALID is what nfc_transceive writes back for an unknown one. */
enum
{
    PCD_IDLE = 0x00,
    PCD_TRANSMIT = 0x04,
    PCD_RECEIVE = 0x08,
    PCD_TRANSCEIVE = 0x0C,
    PCD_MFAUTHENT = 0x0E,
    PCD_SOFTRESET = 0x0F,
    PCD_INVALID = 0x17
};

/* PCD command frame: in, command + bytes to send; out, bits received + response. */
typedef struct
{
    uint8_t command;
    uint8_t reserved;
    uint16_t length;
    uint8_t data[260];
} nfc_frame_t;

/* Result codes, named after the front-end bit that produces each (see p06 TYPE_NOTES). */
typedef enum
{
    NFC_OK = 0,
    NFC_ERR_TIMEOUT = 2,
    NFC_ERR_SELECT_LEN = 11,
    NFC_ERR_PROTOCOL = 21,
    NFC_ERR_COLLISION = 24,
    NFC_ERR_PARITY_CRC = 35,
    NFC_ERR_BAD_SLOT = 60,
    NFC_ERR_GENERIC = 125,
    NFC_ERR_BAD_COMMAND = 127
} nfc_status_t;

extern nfc_frame_t g_nfc_frame;             /* 0x200002F0 */
extern uint8_t g_nfc_reg37_saved;           /* 0x20000052 */
void spi_write_byte(uint8_t value);
uint8_t spi_read_byte(void);
void nfc_write_reg(uint8_t reg, uint8_t value);
uint8_t nfc_read_reg(uint8_t reg);
void nfc_set_bits(uint8_t reg, uint8_t mask);
void nfc_clear_bits(uint8_t reg, uint8_t mask);
void nfc_reset(void);
void nfc_antenna_off(void);
void nfc_antenna_on(void);
void nfc_init_protocol(uint8_t type);
void nfc_set_timeout(uint8_t exponent);
void nfc_wait_irq(void);
nfc_status_t nfc_transceive(nfc_frame_t *frame);
nfc_status_t nfc_exchange(uint8_t command, const uint8_t *tx, uint32_t tx_len, uint8_t *rx,
                          uint32_t *rx_bits);
void nfc_irq_config(int enable);

/* ---- iso14443a.c ---- */
extern uint8_t g_uid_len;                   /* 0x20000051 */
extern uint8_t g_atqa[2];                   /* 0x20000053 */
extern uint8_t g_uid_a[10];                 /* 0x200002CC */
nfc_status_t iso14443a_request(uint8_t req_code, uint8_t *atqa);
nfc_status_t iso14443a_anticoll(uint8_t sel_code, uint8_t known_bits, uint8_t *uid);
nfc_status_t iso14443a_select(uint8_t sel_code, const uint8_t *uid, uint8_t *sak);
nfc_status_t iso14443a_read_uid(void);

/* ---- iso14443b.c ---- */
extern uint8_t g_iso14443b_fwi;             /* 0x20000050 */
extern uint8_t g_uid_b[10];                 /* 0x200002D6 */
nfc_status_t iso14443b_request(uint8_t wupb, uint8_t afi, uint8_t n_slots, uint8_t *atqb);
nfc_status_t iso14443b_slot_marker(uint8_t slot, uint8_t *atqb);
nfc_status_t iso14443b_attrib(const uint8_t *pupi, uint8_t dsi_dri, uint8_t fwi, uint8_t cid,
                              uint8_t *answer);
nfc_status_t idcard_get_uid(uint8_t *uid);
nfc_status_t iso14443b_read_idcard(void);

/* ---- card.c ---- */
extern volatile uint8_t g_learn_mode;       /* 0x20000005 */
extern volatile uint8_t g_card_poll_due;    /* 0x20000006 */
extern volatile uint8_t g_card_valid;       /* 0x20000007 */
extern volatile uint8_t g_output_on;        /* 0x20000008, output MOSFET (PA1) */
extern volatile uint8_t g_card_miss_once;   /* 0x2000000C */
extern volatile uint8_t g_card_debounced;   /* 0x2000000D */
extern volatile uint32_t g_learn_timer_ms;  /* 0x20000028 */
extern uint32_t g_card_id;                  /* 0x2000002C */
extern uint32_t g_card_table[20];           /* 0x200000F4 */
uint8_t card_table_count(void);
int card_table_contains(uint32_t id);
int card_table_add(uint32_t id);
void card_table_clear(void);
void card_add_and_save(void);
void card_read(void);
void card_poll_task(void);
int card_presence_poll(void);
void card_normal_task(void);
void card_learn_task(void);

/* ---- config.c ---- */
/* Settings page at flash 0x08005C00 (config_flash): 50 words. */
typedef struct
{
    uint32_t magic;          /* 0x55 = valid */
    uint32_t cards[20];      /* enrolled card IDs, 0 = empty */
    uint32_t protect_enabled; /* "BH_FG" */
    uint32_t reserved[28];
} config_t;

#define CONFIG_FLASH_ADDR 0x08005C00U
#define CONFIG_MAGIC 0x55U

extern volatile uint32_t g_protect_enabled; /* 0x20000030 */
void config_load(void);
void config_save(void);

#endif /* MAIN_H */
