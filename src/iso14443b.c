/*
 * ISO14443-B: REQB with slot markers on collision, ATTRIB, and the China resident ID card
 * "get UID" APDU (00 36 00 00 08). Only the UID is read.
 */
#include <string.h>

#include "main.h"

uint8_t g_iso14443b_fwi = 4;
uint8_t g_uid_b[10];

/*
 * iso14443b_request 0x08002B1C: REQB/WUPB (APf 0x05, AFI, PARAM = WUPB bit | N slots). Any
 * error other than a timeout counts as a collision; a good answer must be a 96-bit ATQB, whose
 * FWI (byte 11 high nibble) then sets the timer and g_iso14443b_fwi.
 */
nfc_status_t iso14443b_request(uint8_t wupb, uint8_t afi, uint8_t n_slots, uint8_t *atqb)
{
    uint8_t buf[64];
    uint32_t rx_bits;
    nfc_status_t status;

    nfc_set_timeout(5);
    buf[0] = 0x05;
    buf[1] = afi;
    buf[2] = (uint8_t)((wupb & 0x08) | (n_slots & 0x07));
    status = nfc_exchange(PCD_TRANSCEIVE, buf, 3, buf, &rx_bits);
    if (status != NFC_OK && status != NFC_ERR_TIMEOUT)
    {
        status = NFC_ERR_COLLISION;
    }
    if (status == NFC_OK && rx_bits != 96)
    {
        status = NFC_ERR_GENERIC;
    }
    if (status == NFC_OK)
    {
        memcpy(atqb, buf, 16);
        nfc_set_timeout(atqb[11] >> 4);
        g_iso14443b_fwi = atqb[11] >> 4;
    }
    return status;
}

/* iso14443b_slot_marker 0x08002BF8: Slot-MARKER (slot << 4 | 5) for slot 1..15. */
nfc_status_t iso14443b_slot_marker(uint8_t slot, uint8_t *atqb)
{
    uint8_t buf[64];
    uint32_t rx_bits;
    nfc_status_t status;

    nfc_set_timeout(5);
    if (slot == 0 || slot > 15)
    {
        return NFC_ERR_BAD_SLOT;
    }
    buf[0] = (uint8_t)(16 * slot + 5);
    status = nfc_exchange(PCD_TRANSCEIVE, buf, 1, buf, &rx_bits);
    if (status != NFC_OK && status != NFC_ERR_TIMEOUT)
    {
        status = NFC_ERR_COLLISION;
    }
    if (status == NFC_OK && rx_bits != 96)
    {
        status = NFC_ERR_GENERIC;
    }
    if (status == NFC_OK)
    {
        memcpy(atqb, buf, 16);
        nfc_set_timeout(atqb[11] >> 4);
        g_iso14443b_fwi = atqb[11] >> 4;
    }
    return status;
}

/*
 * iso14443b_attrib 0x0800247C: ATTRIB 1D, PUPI, Param1 0, Param2 (dsi_dri << 4) | 8
 * (max frame 256), Param3 1, Param4 CID. fwi is accepted but unused (the timer uses
 * g_iso14443b_fwi). TypeBReg bits 7:6 set first. On success *answer = first answer byte.
 */
nfc_status_t iso14443b_attrib(const uint8_t *pupi, uint8_t dsi_dri, uint8_t fwi, uint8_t cid,
                              uint8_t *answer)
{
    uint8_t buf[64];
    uint32_t rx_bits;
    nfc_status_t status;

    (void)fwi;
    nfc_set_timeout(g_iso14443b_fwi);
    buf[0] = 0x1D;
    memcpy(&buf[1], pupi, 4);
    buf[5] = 0x00;
    buf[6] = (uint8_t)(16 * dsi_dri + 8);
    buf[7] = 0x01;
    buf[8] = cid & 0x0F;
    nfc_set_bits(TypeBReg, 0xC0);
    status = nfc_exchange(PCD_TRANSCEIVE, buf, 9, buf, &rx_bits);
    if (status == NFC_OK)
    {
        *answer = buf[0];
    }
    return status;
}

/* idcard_get_uid 0x08000C4C: APDU 00 36 00 00 08; the first 10 answer bytes are the UID. */
nfc_status_t idcard_get_uid(uint8_t *uid)
{
    uint8_t buf[64];
    uint32_t rx_bits;
    nfc_status_t status;

    buf[0] = 0x00;
    buf[1] = 0x36;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x08;
    status = nfc_exchange(PCD_TRANSCEIVE, buf, 5, buf, &rx_bits);
    if (status == NFC_OK)
    {
        memcpy(uid, buf, 10);
    }
    return status;
}

/*
 * iso14443b_read_idcard 0x080009C8. REQB is sent up to three times. Only a collision ends the
 * loop early, and only if the 2-slot REQB or one of slot markers 1-3 then answers; a clean
 * answer does not stop the loop, so a lone card is asked three times and the last status
 * wins. Then ATTRIB (PUPI = ATQB bytes 1-4, FWI from byte 10). idcard_get_uid's status is
 * ignored, so g_uid_b can be stale when the APDU fails. The answer[0] = 0x50 store is dead
 * in the vendor too.
 */
nfc_status_t iso14443b_read_idcard(void)
{
    uint8_t atqb[16];
    uint8_t retries = 3;
    uint8_t slot;
    nfc_status_t status = NFC_ERR_TIMEOUT;

    nfc_init_protocol('B');
    while (retries-- != 0)
    {
        status = iso14443b_request(0x08, 0, 0, atqb);
        if (status != NFC_ERR_COLLISION)
        {
            continue;
        }
        status = iso14443b_request(0x08, 0, 2, atqb);
        if (status == NFC_OK)
        {
            break;
        }
        for (slot = 1; slot < 4; slot++)
        {
            status = iso14443b_slot_marker(slot, atqb);
            if (status == NFC_OK)
            {
                break;
            }
        }
        if (status == NFC_OK)
        {
            break;
        }
    }
    if (status == NFC_OK)
    {
        status = iso14443b_attrib(&atqb[1], 0, atqb[10] & 0x0F, 0, atqb);
        if (status == NFC_OK)
        {
            atqb[0] = 0x50;
            idcard_get_uid(g_uid_b);
        }
    }
    return status;
}
