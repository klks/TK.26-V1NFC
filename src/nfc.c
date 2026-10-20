/*
 * WS1820T NFC front end (MFRC522-compatible register set) over bit-banged SPI:
 * SCK PB2, MOSI PB3, MISO PB4, NSS PC1, NRSTPD PB5, IRQ PB7 (polled, never used as interrupt).
 */
#include <string.h>

#include "main.h"

nfc_frame_t g_nfc_frame;
uint8_t g_nfc_reg37_saved;

/* spi_write_byte 0x08002CE4: MSB first; MOSI set, then one SCK high/low pulse per bit. */
void spi_write_byte(uint8_t value)
{
    uint8_t bit;

    for (bit = 0; bit < 8; bit++)
    {
        if ((value & 0x80) != 0)
        {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
        value <<= 1;
    }
}

/* spi_read_byte 0x08002CA4: MSB first; SCK high, sample MISO, SCK low. */
uint8_t spi_read_byte(void)
{
    uint8_t value = 0;
    uint8_t bit;

    for (bit = 0; bit < 8; bit++)
    {
        value <<= 1;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) != GPIO_PIN_RESET)
        {
            value |= 1;
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
    }
    return value;
}

/* nfc_write_reg 0x08002FBC: address byte (reg << 1) & 0x7E, then the value. */
void nfc_write_reg(uint8_t reg, uint8_t value)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_RESET);
    spi_write_byte((uint8_t)((reg << 1) & 0x7E));
    spi_write_byte(value);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET);
}

/* nfc_read_reg 0x08002C70: address byte ((reg << 1) & 0x7E) | 0x80. */
uint8_t nfc_read_reg(uint8_t reg)
{
    uint8_t value;

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_RESET);
    spi_write_byte((uint8_t)(((reg << 1) & 0x7E) | 0x80));
    value = spi_read_byte();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET);
    return value;
}

/* nfc_set_bits 0x08002D2E. */
void nfc_set_bits(uint8_t reg, uint8_t mask)
{
    nfc_write_reg(reg, nfc_read_reg(reg) | mask);
}

/* nfc_clear_bits 0x08000936. */
void nfc_clear_bits(uint8_t reg, uint8_t mask)
{
    nfc_write_reg(reg, nfc_read_reg(reg) & (uint8_t)~mask);
}

/* nfc_reset 0x08002B94: NRSTPD low/high, then SoftReset. */
void nfc_reset(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    delay_ms(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    delay_ms(1);
    nfc_write_reg(CommandReg, PCD_SOFTRESET);
    delay_ms(1);
}

/* nfc_antenna_off 0x08002454: TxControlReg Tx1RFEn/Tx2RFEn off. */
void nfc_antenna_off(void)
{
    nfc_write_reg(TxControlReg, nfc_read_reg(TxControlReg) & 0xFC);
}

/* nfc_antenna_on 0x08002468. */
void nfc_antenna_on(void)
{
    nfc_write_reg(TxControlReg, nfc_read_reg(TxControlReg) | 0x03);
}

/*
 * nfc_init_protocol 0x0800288C: 'A' (0x41) or 'B' (0x42). Reset pulse, antenna off, mode
 * registers for the protocol, then a vendor sequence: writing keys 0x5E/0xAE/0xA5 to
 * VersionReg (0x37) appears to page in hidden analog registers behind 0x26-0x3B. The values are
 * WS1820T-specific and reproduced exactly; reg 0x37 is restored afterwards.
 */
void nfc_init_protocol(uint8_t type)
{
    uint8_t saved;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    delay_ms(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    delay_ms(1);
    nfc_antenna_off();
    delay_loop(500);
    if (type == 'A')
    {
        nfc_clear_bits(Status2Reg, 0x08);
        nfc_clear_bits(ComIEnReg, 0x80);
        nfc_write_reg(ModeReg, 0x3D);
        nfc_write_reg(RxSelReg, 0x86);
        nfc_write_reg(RFCfgReg, 0x58);
        nfc_write_reg(TxASKReg, 0x40);
        nfc_write_reg(TxModeReg, 0x00);
        nfc_write_reg(RxModeReg, 0x00);
        nfc_write_reg(ControlReg, 0x00);
        g_nfc_reg37_saved = nfc_read_reg(VersionReg);
        nfc_write_reg(VersionReg, 0x00);
        nfc_write_reg(VersionReg, 0x5E);
        nfc_write_reg(RFCfgReg, 0x48);
        nfc_write_reg(RxSelReg, 0x88);
        nfc_write_reg(ModGsPReg, 0x0F);
        nfc_write_reg(TestBusReg, 0xED);
        nfc_write_reg(TestADCReg, 0xA5);
        nfc_write_reg(VersionReg, 0xAE);
        nfc_write_reg(TestADCReg, 0x72);
        nfc_write_reg(GsNReg, 0x88);
        nfc_write_reg(CWGsPReg, 0x20);
        nfc_write_reg(VersionReg, 0xA5);
        nfc_write_reg(TestSel2Reg, 0xC9);
        nfc_write_reg(TestPinEnReg, 0x24);
        nfc_write_reg(VersionReg, 0xAE);
        nfc_write_reg(TestPinEnReg, 0xDF);
        nfc_write_reg(VersionReg, 0x5E);
        nfc_write_reg(TestDAC2Reg, 0x10);
        nfc_write_reg(VersionReg, g_nfc_reg37_saved);
    }
    else if (type == 'B')
    {
        nfc_write_reg(Status2Reg, 0x00);
        nfc_clear_bits(ComIEnReg, 0x80);
        nfc_write_reg(ModeReg, 0x3F);
        nfc_write_reg(RxSelReg, 0x85);
        nfc_write_reg(RFCfgReg, 0x58);
        nfc_write_reg(GsNReg, 0xF8);
        nfc_write_reg(CWGsPReg, 0x3F);
        nfc_write_reg(ModGsPReg, 0x12);
        nfc_write_reg(AutoTestReg, 0x00);
        nfc_write_reg(TxASKReg, 0x00);
        nfc_write_reg(TypeBReg, 0x13);
        nfc_write_reg(TxModeReg, 0x83);
        nfc_write_reg(RxModeReg, 0x83);
        nfc_write_reg(BitFramingReg, 0x00);
        /* Saved in a local here, not in g_nfc_reg37_saved as the 'A' branch does. */
        saved = nfc_read_reg(VersionReg);
        nfc_write_reg(VersionReg, 0x00);
        nfc_write_reg(VersionReg, 0x5E);
        nfc_write_reg(RFCfgReg, 0x48);
        nfc_write_reg(RxSelReg, 0x88);
        nfc_write_reg(ModGsPReg, 0x12);
        nfc_write_reg(TestBusReg, 0xED);
        nfc_write_reg(TestADCReg, 0xA5);
        nfc_write_reg(VersionReg, 0xAE);
        nfc_write_reg(TestADCReg, 0x72);
        nfc_write_reg(VersionReg, saved);
    }
    nfc_antenna_on();
    delay_ms(8);
}

/* nfc_set_timeout 0x08002BCC: TAuto, prescaler 0, reload 1 << exponent. */
void nfc_set_timeout(uint8_t exponent)
{
    nfc_write_reg(TPrescalerReg, 0x00);
    nfc_write_reg(TModeReg, 0x88);
    nfc_write_reg(TReloadRegL, (uint8_t)(1 << exponent));
    nfc_write_reg(TReloadRegH, (uint8_t)((uint16_t)(1 << exponent) >> 8));
}

/* nfc_wait_irq 0x0800091C: poll Status1Reg.IRq, at most 200 reads. */
void nfc_wait_irq(void)
{
    int tries = 200;
    int irq;

    do
    {
        irq = nfc_read_reg(Status1Reg) & 0x10;
        tries--;
    } while (tries != 0 && irq == 0);
}

/*
 * nfc_transceive 0x08003B34: run a PCD command on frame. Sends frame->length bytes from
 * frame->data (64 into the FIFO, then 48 at a time on HiAlert), collects the answer 48 bytes
 * at a time on HiAlert, and decodes ComIrqReg/ErrorReg into nfc_status_t. On return
 * frame->length holds the number of bits received.
 */
nfc_status_t nfc_transceive(nfc_frame_t *frame)
{
    uint16_t fifo_pos = 0;
    int read_fifo = 0;
    uint8_t irq_en = 0;
    uint8_t wait_for = 0;
    uint16_t length;
    uint8_t first_chunk;
    uint8_t chunk;
    uint8_t i;
    uint8_t irq;
    uint32_t irq_final;
    uint8_t error;
    uint32_t fifo_level;
    uint32_t last_bits;
    uint8_t cmd;
    nfc_status_t status;

    switch (frame->command)
    {
    case PCD_RECEIVE:
        irq_en = 0x30;
        wait_for = 0x20;
        read_fifo = 1;
        break;
    case PCD_TRANSCEIVE:
        irq_en = 0x71;
        wait_for = 0x20;
        read_fifo = 1;
        break;
    case PCD_MFAUTHENT:
        irq_en = 0x11;
        wait_for = 0x10;
        break;
    case PCD_TRANSMIT:
        irq_en = 0x50;
        wait_for = 0x40;
        break;
    case PCD_IDLE:
        break;
    default:
        frame->command = PCD_INVALID;
        break;
    }
    cmd = frame->command;
    if (cmd == PCD_INVALID || ((cmd == PCD_TRANSCEIVE || cmd == PCD_TRANSMIT) && frame->length == 0))
    {
        return NFC_ERR_BAD_COMMAND;
    }

    nfc_write_reg(CommandReg, PCD_IDLE);
    nfc_write_reg(ComIEnReg, (uint8_t)((nfc_read_reg(ComIEnReg) & 0x80) | irq_en | 0x01));
    nfc_write_reg(ComIrqReg, 0x7F);
    nfc_write_reg(DivIrqReg, 0x7F);
    nfc_set_bits(FIFOLevelReg, 0x80);

    cmd = frame->command;
    if (cmd == PCD_TRANSCEIVE || cmd == PCD_TRANSMIT || cmd == PCD_MFAUTHENT)
    {
        length = frame->length;
        first_chunk = (length < 64) ? (uint8_t)length : 64;
        for (i = 0; i < first_chunk; i++)
        {
            nfc_write_reg(FIFODataReg, frame->data[i]);
        }
        fifo_pos = (uint16_t)(length - first_chunk);
        if (length != first_chunk)
        {
            nfc_write_reg(ComIrqReg, 0x04);
            nfc_set_bits(ComIEnReg, 0x04);
        }
        nfc_write_reg(CommandReg, frame->command);
        if (frame->command == PCD_TRANSCEIVE)
        {
            nfc_set_bits(BitFramingReg, 0x80);
        }
        while (fifo_pos != 0)
        {
            nfc_wait_irq();
            chunk = (fifo_pos <= 48) ? (uint8_t)fifo_pos : 48;
            for (i = 0; i < chunk; i++)
            {
                nfc_write_reg(FIFODataReg, frame->data[frame->length - fifo_pos + i]);
            }
            nfc_write_reg(ComIrqReg, 0x04);
            fifo_pos = (uint16_t)(fifo_pos - chunk);
            if (fifo_pos == 0)
            {
                nfc_clear_bits(ComIEnReg, 0x04);
            }
        }
        nfc_wait_irq();
        if ((nfc_read_reg(ComIrqReg) & 0x40) != 0)
        {
            nfc_write_reg(ComIrqReg, 0x40);
            nfc_write_reg(RxThresholdReg, 0x14);
            nfc_write_reg(RFCfgReg, 0x00);
        }
    }
    if (frame->command == PCD_RECEIVE)
    {
        nfc_write_reg(CommandReg, PCD_RECEIVE);
        nfc_clear_bits(TModeReg, 0x80);
        nfc_set_bits(ControlReg, 0x40);
    }

    /* Drain on HiAlert, 48 bytes at a time, until no HiAlert or RxIRq. */
    nfc_write_reg(ComIrqReg, 0x08);
    nfc_set_bits(ComIEnReg, 0x08);
    for (;;)
    {
        nfc_wait_irq();
        irq = nfc_read_reg(ComIrqReg);
        if ((irq & 0x08) == 0 || (irq & 0x20) != 0)
        {
            nfc_clear_bits(ComIEnReg, 0x08);
            break;
        }
        if (fifo_pos + 48 > 255)
        {
            break;
        }
        for (i = 0; i < 48; i++)
        {
            frame->data[fifo_pos + i] = nfc_read_reg(FIFODataReg);
        }
        nfc_write_reg(ComIrqReg, 0x08);
        fifo_pos = (uint16_t)(fifo_pos + 48);
    }

    irq_final = nfc_read_reg(ComIrqReg);
    nfc_write_reg(ComIrqReg, (uint8_t)irq_final);
    if ((irq_final & 0x01) != 0)
    {
        status = NFC_ERR_TIMEOUT;
    }
    else
    {
        error = nfc_read_reg(ErrorReg);
        status = NFC_ERR_GENERIC;
        if ((irq_final & wait_for) != 0 && (irq_final & irq_en) != 0)
        {
            if ((irq_final & 0x02) == 0)
            {
                /* No ErrIRq: the answer is good. */
                status = NFC_OK;
                if (read_fifo)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            frame->length = (uint16_t)(8 * (fifo_level - 1) + last_bits);
                        }
                        else
                        {
                            frame->length = (uint16_t)(8 * fifo_level);
                        }
                        frame->length += (uint16_t)(8 * fifo_pos);
                        if (fifo_level == 0)
                        {
                            fifo_level = 1;
                        }
                        for (i = 0; i < fifo_level; i++)
                        {
                            frame->data[fifo_pos + i] = nfc_read_reg(FIFODataReg);
                        }
                    }
                    else
                    {
                        status = NFC_ERR_GENERIC;
                    }
                }
            }
            else if ((error & 0x08) != 0 && (nfc_read_reg(CollReg) & 0x20) == 0)
            {
                /* CollErr with a valid position: data after data[0], data[0] = bit position. */
                status = NFC_ERR_COLLISION;
                if (read_fifo)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            frame->length = (uint16_t)(8 * (fifo_level - 1) + last_bits);
                        }
                        else
                        {
                            frame->length = (uint16_t)(8 * fifo_level);
                        }
                        frame->length += (uint16_t)(8 * fifo_pos);
                        if (fifo_level == 0)
                        {
                            fifo_level = 1;
                        }
                        for (i = 0; i < fifo_level; i++)
                        {
                            frame->data[fifo_pos + 1 + i] = nfc_read_reg(FIFODataReg);
                        }
                    }
                }
                frame->data[0] = nfc_read_reg(CollReg) & 0x1F;
                if (frame->data[0] == 0)
                {
                    frame->data[0] = 32;
                }
                frame->data[0]--;
            }
            /* The vendor reads CollReg a second time here. */
            else if ((error & 0x08) == 0 || (nfc_read_reg(CollReg) & 0x20) == 0)
            {
                if ((error & 0x01) != 0)
                {
                    status = NFC_ERR_PROTOCOL;
                }
                else if ((error & 0x06) != 0)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            frame->length = (uint16_t)(8 * (fifo_level - 1) + last_bits);
                        }
                        else
                        {
                            frame->length = (uint16_t)(8 * fifo_level);
                        }
                        frame->length += (uint16_t)(8 * fifo_pos);
                    }
                    status = NFC_ERR_PARITY_CRC;
                }
                else
                {
                    status = NFC_ERR_PARITY_CRC;
                }
            }
        }
    }

    nfc_set_bits(ControlReg, 0x80);
    nfc_write_reg(ComIrqReg, 0x7F);
    nfc_write_reg(DivIrqReg, 0x7F);
    nfc_clear_bits(ComIEnReg, 0x7F);
    nfc_clear_bits(DivIEnReg, 0x7F);
    nfc_write_reg(CommandReg, PCD_IDLE);
    return status;
}

/*
 * nfc_exchange 0x080024E4: the same state machine as nfc_transceive with separate tx/rx
 * buffers and *rx_bits for the bit count. Differences kept from the listing: the FIFO position
 * is 8-bit, an unknown command is not written back anywhere, and PCD_RECEIVE only sets
 * ControlReg.StartSend (no CommandReg write, TModeReg untouched).
 */
nfc_status_t nfc_exchange(uint8_t command, const uint8_t *tx, uint32_t tx_len, uint8_t *rx,
                          uint32_t *rx_bits)
{
    uint8_t cmd = command;
    int read_fifo = 0;
    uint8_t fifo_pos = 0;
    uint8_t irq_en = 0;
    uint8_t wait_for = 0;
    uint32_t first_chunk;
    uint32_t chunk;
    uint32_t i;
    uint8_t irq;
    uint32_t irq_final;
    uint8_t error;
    uint32_t fifo_level;
    uint32_t last_bits;
    nfc_status_t status;

    switch (command)
    {
    case PCD_RECEIVE:
        irq_en = 0x30;
        wait_for = 0x20;
        read_fifo = 1;
        break;
    case PCD_TRANSCEIVE:
        irq_en = 0x71;
        wait_for = 0x20;
        read_fifo = 1;
        break;
    case PCD_MFAUTHENT:
        irq_en = 0x11;
        wait_for = 0x10;
        break;
    case PCD_TRANSMIT:
        irq_en = 0x50;
        wait_for = 0x40;
        break;
    case PCD_IDLE:
        break;
    default:
        cmd = PCD_INVALID;
        break;
    }
    if (cmd == PCD_INVALID || ((cmd == PCD_TRANSCEIVE || cmd == PCD_TRANSMIT) && tx_len == 0))
    {
        return NFC_ERR_BAD_COMMAND;
    }

    nfc_write_reg(CommandReg, PCD_IDLE);
    nfc_write_reg(ComIEnReg, (uint8_t)((nfc_read_reg(ComIEnReg) & 0x80) | irq_en | 0x01));
    nfc_write_reg(ComIrqReg, 0x7F);
    nfc_write_reg(DivIrqReg, 0x7F);
    nfc_set_bits(FIFOLevelReg, 0x80);

    if (cmd == PCD_TRANSCEIVE || cmd == PCD_TRANSMIT || cmd == PCD_MFAUTHENT)
    {
        first_chunk = (tx_len < 64) ? tx_len : 64;
        for (i = 0; i < first_chunk; i++)
        {
            nfc_write_reg(FIFODataReg, tx[i]);
        }
        fifo_pos = (uint8_t)(tx_len - first_chunk);
        if ((uint8_t)tx_len != (uint8_t)first_chunk)
        {
            nfc_write_reg(ComIrqReg, 0x04);
            nfc_set_bits(ComIEnReg, 0x04);
        }
        nfc_write_reg(CommandReg, cmd);
        if (cmd == PCD_TRANSCEIVE)
        {
            nfc_set_bits(BitFramingReg, 0x80);
        }
        while (fifo_pos != 0)
        {
            nfc_wait_irq();
            chunk = (fifo_pos <= 48) ? fifo_pos : 48;
            for (i = 0; i < chunk; i++)
            {
                nfc_write_reg(FIFODataReg, tx[tx_len - fifo_pos + i]);
            }
            nfc_write_reg(ComIrqReg, 0x04);
            fifo_pos = (uint8_t)(fifo_pos - chunk);
            if (fifo_pos == 0)
            {
                nfc_clear_bits(ComIEnReg, 0x04);
            }
        }
        nfc_wait_irq();
        if ((nfc_read_reg(ComIrqReg) & 0x40) != 0)
        {
            nfc_write_reg(ComIrqReg, 0x40);
            nfc_write_reg(RxThresholdReg, 0x14);
            nfc_write_reg(RFCfgReg, 0x00);
        }
    }
    if (cmd == PCD_RECEIVE)
    {
        nfc_set_bits(ControlReg, 0x40);
    }

    nfc_write_reg(ComIrqReg, 0x08);
    nfc_set_bits(ComIEnReg, 0x08);
    for (;;)
    {
        nfc_wait_irq();
        irq = nfc_read_reg(ComIrqReg);
        if ((irq & 0x08) == 0 || (irq & 0x20) != 0)
        {
            nfc_clear_bits(ComIEnReg, 0x08);
            break;
        }
        if (fifo_pos + 48U > 255)
        {
            break;
        }
        for (i = 0; i < 48; i++)
        {
            rx[fifo_pos + i] = nfc_read_reg(FIFODataReg);
        }
        nfc_write_reg(ComIrqReg, 0x08);
        fifo_pos = (uint8_t)(fifo_pos + 48);
    }

    irq_final = nfc_read_reg(ComIrqReg);
    nfc_write_reg(ComIrqReg, (uint8_t)irq_final);
    if ((irq_final & 0x01) != 0)
    {
        status = NFC_ERR_TIMEOUT;
    }
    else
    {
        error = nfc_read_reg(ErrorReg);
        status = NFC_ERR_GENERIC;
        if ((irq_final & wait_for) != 0 && (irq_final & irq_en) != 0)
        {
            if ((irq_final & 0x02) == 0)
            {
                status = NFC_OK;
                if (read_fifo)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            *rx_bits = 8 * (fifo_level - 1) + last_bits;
                        }
                        else
                        {
                            *rx_bits = 8 * fifo_level;
                        }
                        *rx_bits += 8U * fifo_pos;
                        if (fifo_level == 0)
                        {
                            fifo_level = 1;
                        }
                        for (i = 0; i < fifo_level; i++)
                        {
                            rx[fifo_pos + i] = nfc_read_reg(FIFODataReg);
                        }
                    }
                    else
                    {
                        status = NFC_ERR_GENERIC;
                    }
                }
            }
            else if ((error & 0x08) != 0 && (nfc_read_reg(CollReg) & 0x20) == 0)
            {
                status = NFC_ERR_COLLISION;
                if (read_fifo)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            *rx_bits = 8 * (fifo_level - 1) + last_bits;
                        }
                        else
                        {
                            *rx_bits = 8 * fifo_level;
                        }
                        *rx_bits += 8U * fifo_pos;
                        if (fifo_level == 0)
                        {
                            fifo_level = 1;
                        }
                        for (i = 0; i < fifo_level; i++)
                        {
                            rx[fifo_pos + 1 + i] = nfc_read_reg(FIFODataReg);
                        }
                    }
                }
                rx[0] = nfc_read_reg(CollReg) & 0x1F;
                if (rx[0] == 0)
                {
                    rx[0] = 32;
                }
                rx[0]--;
            }
            else if ((error & 0x08) == 0 || (nfc_read_reg(CollReg) & 0x20) == 0)
            {
                if ((error & 0x01) != 0)
                {
                    status = NFC_ERR_PROTOCOL;
                }
                else if ((error & 0x06) != 0)
                {
                    fifo_level = nfc_read_reg(FIFOLevelReg) & 0x7F;
                    last_bits = nfc_read_reg(ControlReg) & 0x07;
                    if (fifo_pos + fifo_level <= 255)
                    {
                        if (last_bits != 0 && fifo_level != 0)
                        {
                            *rx_bits = 8 * (fifo_level - 1) + last_bits;
                        }
                        else
                        {
                            *rx_bits = 8 * fifo_level;
                        }
                        *rx_bits += 8U * fifo_pos;
                    }
                    status = NFC_ERR_PARITY_CRC;
                }
                else
                {
                    status = NFC_ERR_PARITY_CRC;
                }
            }
        }
    }

    nfc_set_bits(ControlReg, 0x80);
    nfc_write_reg(ComIrqReg, 0x7F);
    nfc_write_reg(DivIrqReg, 0x7F);
    nfc_clear_bits(ComIEnReg, 0x7F);
    nfc_clear_bits(DivIEnReg, 0x7F);
    nfc_write_reg(CommandReg, PCD_IDLE);
    return status;
}

/*
 * nfc_irq_config 0x08000AD8: PB7 as EXTI falling edge with the EXTI4_15 interrupt, or as a
 * plain input. The vendor enables the GPIOA clock here (IOPENR bit 0), not GPIOB; kept.
 * NVIC: enable first, then priority 0.
 */
void nfc_irq_config(int enable)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (enable)
    {
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Pin = GPIO_PIN_7;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
        HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
    }
    else
    {
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Pin = GPIO_PIN_7;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
}
