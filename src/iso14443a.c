/*
 * ISO14443-A: REQA, bit-oriented anticollision, SELECT, cascade to a 7-byte UID.
 * Only the UID is used; there is no MIFARE authentication anywhere in the firmware.
 */
#include <string.h>

#include "main.h"

uint8_t g_uid_len;
uint8_t g_atqa[2];
uint8_t g_uid_a[10];

/*
 * iso14443a_request 0x08002A94: REQA/WUPA as a 7-bit frame; succeeds only on a 16-bit ATQA.
 * A VersionReg of 0x15 or 0x18 gets a 1 ms pause. Any other value gets TxMode/GsN/CWGsP
 * rewritten first. Which chips 0x15/0x18 identify is not known; the WS1820T reads TBD.
 */
nfc_status_t iso14443a_request(uint8_t req_code, uint8_t *atqa)
{
    uint8_t buf[64];
    uint32_t rx_bits;
    uint8_t version;
    nfc_status_t status;

    nfc_clear_bits(Status2Reg, 0x08);
    nfc_write_reg(BitFramingReg, 0x07);
    nfc_set_bits(TxControlReg, 0x03);
    version = nfc_read_reg(VersionReg);
    if (version == 0x15 || version == 0x18)
    {
        delay_ms(1);
    }
    else
    {
        nfc_write_reg(TxModeReg, 0x30);
        nfc_write_reg(GsNReg, 0x20);
        nfc_write_reg(CWGsPReg, 0x03);
    }
    nfc_set_timeout(4);
    buf[0] = req_code;
    status = nfc_exchange(PCD_TRANSCEIVE, buf, 1, buf, &rx_bits);
    if (status == NFC_OK && rx_bits == 16)
    {
        atqa[0] = buf[0];
        atqa[1] = buf[1];
    }
    else
    {
        status = NFC_ERR_TIMEOUT;
    }
    return status;
}

/*
 * iso14443a_anticoll 0x0800394C: one cascade level. On a collision the known bits are kept,
 * the colliding bit is chosen as 1 and the request repeated; then the 4 UID bytes are checked
 * against the BCC.
 */
nfc_status_t iso14443a_anticoll(uint8_t sel_code, uint8_t known_bits, uint8_t *uid)
{
    uint8_t uid_buf[8];
    uint8_t bit_count = known_bits;
    uint8_t bcc = 0;
    uint32_t last_bits;
    uint32_t tx_bytes;
    uint32_t byte_count;
    uint32_t rx_bytes;
    uint8_t partial_byte;
    uint8_t index;
    uint8_t i;
    int j;
    nfc_status_t status;

    memset(uid_buf, 0, sizeof(uid_buf));
    nfc_write_reg(BitFramingReg, 0x00);
    nfc_clear_bits(TxModeReg, 0x80);
    nfc_clear_bits(RxModeReg, 0x80);
    if ((uint32_t)nfc_read_reg(VersionReg) - 0x13 >= 0x1D)
    {
        nfc_write_reg(TxModeReg, 0x30);
    }
    nfc_set_timeout(4);

    do
    {
        last_bits = bit_count & 7;
        if (last_bits != 0)
        {
            tx_bytes = (bit_count >> 3) + 1;
            nfc_clear_bits(BitFramingReg, 0x77);
            nfc_set_bits(BitFramingReg, (uint8_t)(last_bits | (last_bits << 4)));
        }
        else
        {
            tx_bytes = bit_count >> 3;
        }
        g_nfc_frame.command = PCD_TRANSCEIVE;
        g_nfc_frame.data[0] = sel_code;
        byte_count = bit_count >> 3;
        g_nfc_frame.data[1] = (uint8_t)(16 * byte_count + last_bits + 0x20); /* NVB */
        for (i = 0; i < tx_bytes; i++)
        {
            g_nfc_frame.data[i + 2] = uid_buf[i];
        }
        g_nfc_frame.length = (uint16_t)(tx_bytes + 2);
        status = nfc_transceive(&g_nfc_frame);
        partial_byte = uid_buf[byte_count];

        if (status == NFC_ERR_COLLISION)
        {
            for (j = 0; bit_count <= 47 && (int)(5 - byte_count) > j; j = (uint8_t)(j + 1))
            {
                uid_buf[byte_count + j] = g_nfc_frame.data[j + 1];
            }
            uid_buf[byte_count] |= partial_byte;
            if (g_nfc_frame.data[0] >= last_bits)
            {
                bit_count = (uint8_t)(g_nfc_frame.data[0] - last_bits + bit_count);
            }
            /* Keep the bits below the collision, set the colliding bit to 1. */
            index = bit_count >> 3;
            uid_buf[index] &= (uint8_t)(0xFF >> (8 - (bit_count & 7)));
            uid_buf[index] |= (uint8_t)(1 << (bit_count & 7));
            bit_count++;
        }
        else if (status == NFC_OK)
        {
            rx_bytes = g_nfc_frame.length >> 3;
            for (i = 0; rx_bytes > i && i <= 4; i++)
            {
                uid_buf[4 - i] = g_nfc_frame.data[rx_bytes - i - 1];
            }
            uid_buf[byte_count] |= partial_byte;
        }
    } while (status == NFC_ERR_COLLISION);

    if (status == NFC_OK)
    {
        for (i = 0; i < 4; i++)
        {
            uid[i] = uid_buf[i];
            bcc ^= uid_buf[i];
        }
        if (uid_buf[4] != bcc)
        {
            status = NFC_ERR_GENERIC;
        }
    }
    nfc_write_reg(BitFramingReg, 0x00);
    return status;
}

/* iso14443a_select 0x08003AC4: SELECT (NVB 0x70) with BCC, CRC on; the SAK must be 8 bits. */
nfc_status_t iso14443a_select(uint8_t sel_code, const uint8_t *uid, uint8_t *sak)
{
    uint8_t bcc = 0;
    uint8_t i;
    nfc_status_t status;

    nfc_write_reg(BitFramingReg, 0x00);
    nfc_set_bits(TxModeReg, 0x80);
    nfc_set_bits(RxModeReg, 0x80);
    nfc_set_timeout(4);
    g_nfc_frame.command = PCD_TRANSCEIVE;
    g_nfc_frame.length = 7;
    g_nfc_frame.data[0] = sel_code;
    g_nfc_frame.data[1] = 0x70;
    for (i = 0; i < 4; i++)
    {
        bcc ^= uid[i];
        g_nfc_frame.data[i + 2] = uid[i];
    }
    g_nfc_frame.data[6] = bcc;
    status = nfc_transceive(&g_nfc_frame);
    if (status == NFC_OK)
    {
        if (g_nfc_frame.length == 8)
        {
            *sak = g_nfc_frame.data[0];
        }
        else
        {
            status = NFC_ERR_SELECT_LEN;
        }
    }
    return status;
}

/*
 * iso14443a_read_uid 0x08000950: REQA, cascade level 1; SAK bit 2 (UID not complete) ->
 * cascade level 2 into g_uid_a[4..]. For a 7-byte UID g_uid_a holds CT + 3 bytes, then 4.
 */
nfc_status_t iso14443a_read_uid(void)
{
    uint8_t sak;
    nfc_status_t status;

    nfc_init_protocol('A');
    if (iso14443a_request(0x26, g_atqa) != NFC_OK)
    {
        return NFC_ERR_TIMEOUT;
    }
    if (iso14443a_anticoll(0x93, 0, g_uid_a) != NFC_OK)
    {
        return NFC_ERR_TIMEOUT;
    }
    g_uid_len = 4;
    status = iso14443a_select(0x93, g_uid_a, &sak);
    if (status == NFC_OK && (sak & 0x04) != 0)
    {
        status = iso14443a_anticoll(0x95, 0, &g_uid_a[4]);
        if (status == NFC_OK)
        {
            g_uid_len = 7;
            return iso14443a_select(0x95, &g_uid_a[4], &sak);
        }
    }
    return status;
}
