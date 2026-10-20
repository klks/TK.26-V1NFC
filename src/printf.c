/*
 * printf for the debug log. The vendor linked Keil microlib, whose printf formats into fputc
 * (0x08003620 -> uart_putc) one character at a time. newlib's printf adds about 3 KB of stdio
 * (FILE buffers, malloc, setvbuf) that does not fit beside the rest in 20 KB, so this
 * replaces it. It handles what the firmware's format strings use - %d, %02X, %08X - plus
 * %u %x %s %c %% and the '0' flag and width, all unbuffered straight to the UART.
 * Built with -fno-builtin-printf so GCC does not turn constant printf calls into puts.
 */
#include <stdarg.h>
#include <stdio.h>

#include "main.h"

static int put_number(uint32_t value, unsigned base, int upper, int negative, int width, char pad)
{
    char digits[11];
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    int count = 0;

    do
    {
        digits[n++] = set[value % base];
        value /= base;
    } while (value != 0);

    if (negative)
    {
        width--;
        if (pad == '0')
        {
            uart_putc('-');
            count++;
        }
    }
    while (width > n)
    {
        uart_putc((uint8_t)pad);
        width--;
        count++;
    }
    if (negative && pad != '0')
    {
        uart_putc('-');
        count++;
    }
    while (n > 0)
    {
        uart_putc((uint8_t)digits[--n]);
        count++;
    }
    return count;
}

int printf(const char *format, ...)
{
    va_list args;
    int count = 0;
    char pad;
    int width;
    int value;
    const char *s;

    va_start(args, format);
    while (*format != '\0')
    {
        if (*format != '%')
        {
            uart_putc((uint8_t)*format++);
            count++;
            continue;
        }
        format++;
        pad = ' ';
        width = 0;
        if (*format == '0')
        {
            pad = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9')
        {
            width = width * 10 + (*format++ - '0');
        }
        switch (*format)
        {
        case 'd':
        case 'i':
            value = va_arg(args, int);
            if (value < 0)
            {
                count += put_number(0U - (uint32_t)value, 10, 0, 1, width, pad);
            }
            else
            {
                count += put_number((uint32_t)value, 10, 0, 0, width, pad);
            }
            break;
        case 'u':
            count += put_number(va_arg(args, unsigned), 10, 0, 0, width, pad);
            break;
        case 'x':
            count += put_number(va_arg(args, unsigned), 16, 0, 0, width, pad);
            break;
        case 'X':
            count += put_number(va_arg(args, unsigned), 16, 1, 0, width, pad);
            break;
        case 'c':
            uart_putc((uint8_t)va_arg(args, int));
            count++;
            break;
        case 's':
            s = va_arg(args, const char *);
            while (*s != '\0')
            {
                uart_putc((uint8_t)*s++);
                count++;
            }
            break;
        case '%':
            uart_putc('%');
            count++;
            break;
        default:
            /* Unsupported conversion: stop, as there is no safe way to skip its argument. */
            va_end(args);
            return count;
        }
        format++;
    }
    va_end(args);
    return count;
}
