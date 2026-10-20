/*
 * Card handling: the 20-entry enrolled-card table, card reading (Type A UID, else China ID
 * card UID), and the learn / normal tasks that switch the output MOSFET on PA1.
 * Access control is by UID alone.
 */
#include <stdio.h>

#include "main.h"
#include "log_strings.h"

volatile uint8_t g_learn_mode;
volatile uint8_t g_card_poll_due = 1;
volatile uint8_t g_card_valid;
volatile uint8_t g_output_on;
volatile uint8_t g_card_miss_once;
volatile uint8_t g_card_debounced = 1;
volatile uint32_t g_learn_timer_ms;
uint32_t g_card_id;
uint32_t g_card_table[20];

/* card_table_count 0x08000C2C: non-zero entries. */
uint8_t card_table_count(void)
{
    uint8_t count = 0;
    int i;

    for (i = 0; i < 20; i++)
    {
        if (g_card_table[i] != 0)
        {
            count++;
        }
    }
    return count;
}

/* card_table_contains 0x08004054: 1 if id is a non-zero entry. */
int card_table_contains(uint32_t id)
{
    int i;

    for (i = 0; i < 20; i++)
    {
        if (g_card_table[i] == id && g_card_table[i] != 0)
        {
            return 1;
        }
    }
    return 0;
}

/* card_table_add 0x08002D9C: 2 = already present, 1 = added in the first free slot, 0 = full. */
int card_table_add(uint32_t id)
{
    int i;

    for (i = 0; i < 20; i++)
    {
        if (g_card_table[i] == id)
        {
            return 2;
        }
    }
    for (i = 0; g_card_table[i] != 0; )
    {
        if (++i >= 20)
        {
            return 0;
        }
    }
    g_card_table[i] = id;
    return 1;
}

/* card_table_clear 0x08002028. */
void card_table_clear(void)
{
    int i;

    for (i = 0; i < 20; i++)
    {
        g_card_table[i] = 0;
    }
    printf("nrf_eep_clear\r\n");
    beep_start(2, 500);
    config_save();
    delay_ms(10);
}

/* card_add_and_save 0x08003F44: save unless the table was full (also when already present). */
void card_add_and_save(void)
{
    if (card_table_add(g_card_id) != 0)
    {
        config_save();
        delay_ms(10);
    }
}

static uint32_t uid_to_id(const uint8_t *uid)
{
    return ((uint32_t)uid[0] << 24) | ((uint32_t)uid[1] << 16) | ((uint32_t)uid[2] << 8) | uid[3];
}

/*
 * card_read 0x08000868: Type A first; on its timeout status, the Type B ID card. g_card_id is
 * the first 4 UID bytes big-endian. The Type A UID is printed (all 10 bytes) for status 0, 4
 * or 18; iso14443a_read_uid never returns 4 or 18, but the vendor tests them.
 */
void card_read(void)
{
    nfc_status_t status;
    uint8_t i;

    status = iso14443a_read_uid();
    if (status == NFC_ERR_TIMEOUT)
    {
        if (iso14443b_read_idcard() == NFC_OK)
        {
            beep_start(1, 100);
            g_card_id = uid_to_id(g_uid_b);
            g_card_valid = 1;
        }
    }
    else if (status == NFC_OK || (int)status == 4 || (int)status == 18)
    {
        printf(STR_CARD_A_NO);
        for (i = 0; i < 10; i++)
        {
            printf("%02X", g_uid_a[i]);
        }
        printf("\r\n");
        if (status == NFC_OK)
        {
            g_card_id = uid_to_id(g_uid_a);
            g_card_valid = 1;
        }
    }
}

/* card_poll_task 0x080038D0: every 50 ms; the front end is held in reset (PB5 low) between polls. */
void card_poll_task(void)
{
    if (g_card_poll_due)
    {
        card_read();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
        g_card_poll_due = 0;
    }
}

/* card_presence_poll 0x08003EF8: read again; a card counts as gone after two misses in a row. */
int card_presence_poll(void)
{
    g_card_valid = 0;
    card_read();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    if (g_card_valid)
    {
        g_card_miss_once = 0;
        g_card_debounced = 1;
        return 1;
    }
    if (!g_card_debounced)
    {
        g_card_miss_once = 0;
        return 0;
    }
    if (g_card_miss_once)
    {
        g_card_debounced = 0;
        return 0;
    }
    g_card_miss_once = 1;
    return 1;
}

/*
 * card_normal_task 0x080020C8 (outside learn mode):
 *   empty table                  -> enroll the card as the first one
 *   one card, this is not it     -> enroll it as the second
 *   enrolled card                -> toggle the output, with supply checks when protection is on;
 *                                   with the output off, holding the card 6 s clears the table
 *                                   and enters learn mode
 *   unknown card                 -> 3 beeps
 * then wait for the card to be removed.
 */
void card_normal_task(void)
{
    int vcc;
    int sample;
    int vcc_after;

    if (!g_card_valid || g_learn_mode)
    {
        return;
    }

    if (card_table_count() == 0)
    {
        beep_start(2, 70);
        card_add_and_save();
        printf(STR_ADD_FIRST_CARD);
    }
    else if (card_table_count() == 1 && !card_table_contains(g_card_id))
    {
        beep_start(2, 70);
        card_add_and_save();
        printf(STR_ADD_SECOND_CARD);
    }
    else if (card_table_count() != 0 && card_table_count() <= 20 && card_table_contains(g_card_id))
    {
        /* Supply 24 V or more with the output off and protection on: the MOSFET may be
           conducting. Re-measure; if it is still high, report MOS_HHH and leave it off. */
        vcc = (int)g_vcc;
        if (vcc < 24 || g_output_on || !g_protect_enabled)
        {
            beep_start(1, 120);
            g_output_on = !g_output_on;
        }
        else
        {
            delay_ms(10);
            vcc = vcc_measure();
            if (vcc >= 24 && !g_output_on)
            {
                printf("MOS_HHH\r\n");
                beep_start(2, 120);
                g_output_on = 0;
            }
        }

        /* Switching on with protection: 100 samples with the output on; under 10 V -> off. */
        if (g_output_on && g_protect_enabled)
        {
            sample = 0;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
            while (sample < 100)
            {
                delay_ms(2);
                vcc_measure();
                vcc_measure();
                vcc = vcc_measure();
                printf("time2=%d,VCC=%d\r\n", sample++, vcc);
            }
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
            if (vcc < 10)
            {
                g_output_on = 0;
                beep_start(5, 180);
            }
        }

        printf("JDQ1=%d\r\n", (int)g_output_on);
        if (g_output_on)
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
        }

        /* Second-level over-current check 5 ms after switching on. */
        if (g_output_on && g_protect_enabled)
        {
            delay_ms(5);
            vcc_after = vcc_measure();
            if (vcc_after < 15)
            {
                printf(STR_OVERCURRENT_LEVEL2_VCC, vcc_after);
                delay_ms(100);
                g_output_on = 0;
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
                beep_start(8, 180);
            }
        }

        delay_ms(100);
        card_presence_poll();
        if (!g_output_on)
        {
            g_wait_timer_ms = 0;
            card_presence_poll();
            while (g_wait_timer_ms < 6000 && card_presence_poll())
            {
                delay_ms(100);
                printf(STR_WAIT_REMOVAL_TIMER, (int)g_wait_timer_ms);
            }
            if (g_wait_timer_ms >= 6000)
            {
                beep_start(4, 70);
                delay_ms(1000);
                beep_start(4, 70);
                delay_ms(1000);
                card_table_clear();
                g_learn_mode = 1;
            }
        }
    }
    else if (!card_table_contains(g_card_id))
    {
        beep_start(3, 120);
        printf(STR_CARD_NOT_ENROLLED);
    }

    for (;;)
    {
        delay_ms(100);
        if (!card_presence_poll())
        {
            break;
        }
        printf(STR_WAIT_CARD_REMOVAL_1);
    }
    g_card_valid = 0;
}

/* card_learn_task 0x0800238C: beeps 2 = added, 3 = already known, 5 = table full. */
void card_learn_task(void)
{
    if (!g_card_valid || !g_learn_mode)
    {
        return;
    }

    printf("ID= %08X\r\n", (unsigned)g_card_id);
    switch (card_table_add(g_card_id))
    {
    case 1:
        config_save();
        delay_ms(10);
        beep_start(2, 70);
        g_learn_timer_ms = 0;
        printf(STR_ADD_OK);
        break;
    case 2:
        beep_start(3, 70);
        printf(STR_NOT_NEW_CARD);
        break;
    case 0:
        beep_start(5, 70);
        printf(STR_TABLE_FULL);
        break;
    default:
        break;
    }

    for (;;)
    {
        delay_ms(10);
        if (!card_presence_poll())
        {
            break;
        }
        printf(STR_WAIT_CARD_REMOVAL_2);
    }
    g_card_valid = 0;
}
