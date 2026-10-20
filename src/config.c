/*
 * Settings page at 0x08005C00, in the last 4 KB flash sector (0x08005000-0x08005FFF, outside
 * the 20 KB the code is linked into): magic 0x55, the 20 enrolled card IDs, protection flag.
 */
#include <string.h>

#include "main.h"

volatile uint32_t g_protect_enabled = 1;

/*
 * RAM image of the settings (g_config, 0x200001EC in the vendor). config_program writes two
 * 128-byte flash pages from it, 256 bytes in all, while config_t is 200. The vendor reads the
 * 56 bytes past the struct from whatever follows in RAM. Here they come from this union's tail,
 * which config_save fills with 0xFF (the erased value). Nothing reads them back. See PLAN §8.
 */
static union
{
    config_t cfg;
    uint32_t words[64];
} s_config_image;

/* config_verify_erased 0x08000608: the 50 settings words must read 0xFFFFFFFF. */
static void config_verify_erased(void)
{
    uint32_t offset;

    for (offset = 0; offset < sizeof(config_t); offset += 4)
    {
        if (*(const volatile uint32_t *)(CONFIG_FLASH_ADDR + offset) != 0xFFFFFFFFU)
        {
            APP_ErrorHandler();
        }
    }
}

/* config_erase 0x08000628: erase the 4 KB sector holding 0x08005C00. */
static void config_erase(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageSectorError = 0;

    memset(&EraseInitStruct, 0, sizeof(EraseInitStruct));
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORERASE;
    EraseInitStruct.SectorAddress = CONFIG_FLASH_ADDR;
    EraseInitStruct.NbSectors = 1;
    if (HAL_FLASH_Erase(&EraseInitStruct, &PageSectorError) != HAL_OK)
    {
        APP_ErrorHandler();
    }
}

/*
 * config_program 0x0800069C: program 128-byte pages from 0x08005C00 while the address is below
 * 0x08005CC8, retrying a page until HAL_FLASH_Program succeeds (with no limit; the IWDG
 * ends it).
 */
static void config_program(void)
{
    uint32_t address = CONFIG_FLASH_ADDR;
    uint32_t *src = s_config_image.words;

    while (address < CONFIG_FLASH_ADDR + sizeof(config_t))
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, address, src) == HAL_OK)
        {
            address += 128;
            src += 32;
        }
    }
}

/* config_save 0x08000A6C: build the image, unlock, erase, check blank, program, lock. */
void config_save(void)
{
    int i;

    iwdg_feed();
    memset(&s_config_image.words[sizeof(config_t) / 4], 0xFF,
           sizeof(s_config_image) - sizeof(config_t));
    s_config_image.cfg.magic = CONFIG_MAGIC;
    for (i = 0; i < 20; i++)
    {
        s_config_image.cfg.cards[i] = g_card_table[i];
    }
    s_config_image.cfg.protect_enabled = g_protect_enabled;
    HAL_FLASH_Unlock();
    config_erase();
    config_verify_erased();
    config_program();
    HAL_FLASH_Lock();
    iwdg_feed();
}

/*
 * config_load 0x080035B4: copy the 50 settings words (1 ms apart, as the vendor does). A valid
 * magic loads the card table and protection flag. Otherwise the table is cleared, protection
 * turned on and the page written.
 */
void config_load(void)
{
    uint8_t i;
    int j;

    for (i = 0; i < sizeof(config_t) / 4; i++)
    {
        s_config_image.words[i] = ((const volatile uint32_t *)CONFIG_FLASH_ADDR)[i];
        delay_ms(1);
    }
    if (s_config_image.cfg.magic == CONFIG_MAGIC)
    {
        for (j = 0; j < 20; j++)
        {
            g_card_table[j] = s_config_image.cfg.cards[j];
        }
        g_protect_enabled = s_config_image.cfg.protect_enabled;
    }
    else
    {
        for (i = 0; i < 20; i++)
        {
            g_card_table[i] = 0;
        }
        g_protect_enabled = 1;
        config_save();
    }
}
