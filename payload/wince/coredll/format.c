#include <stdarg.h>
/* Bounded integer/UTF-16 formatting; unsupported conversion specifiers fail. */
static int ce_format(u16 *dest, u32 capacity, const u16 *format, va_list args)
{
    u32 n = 0;
    if (!dest || !capacity || !format)
        return -1;
#define CE_PUT(ch)                                                                                 \
    do {                                                                                           \
        if (n + 1 >= capacity) {                                                                   \
            dest[capacity - 1] = 0;                                                                \
            return -1;                                                                             \
        }                                                                                          \
        dest[n++] = (u16)(ch);                                                                     \
    } while (0)
    while (*format) {
        if (*format != '%') {
            CE_PUT(*format++);
            continue;
        }
        ++format;
        if (*format == '%') {
            CE_PUT(*format++);
            continue;
        }
        int width = 0, zero = 0;
        if (*format == '0') {
            zero = 1;
            ++format;
        }
        while (*format >= '0' && *format <= '9') {
            if (width < 1024)
                width = width * 10 + (*format - '0');
            ++format;
        }
        if (*format == 'l')
            ++format;
        u16 spec = *format++;
        if (spec == 's') {
            const u16 *s = va_arg(args, const u16 *);
            if (s)
                while (*s)
                    CE_PUT(*s++);
            continue;
        }
        if (spec == 'c') {
            CE_PUT(va_arg(args, int));
            continue;
        }
        unsigned base = (spec == 'x' || spec == 'X' || spec == 'p') ? 16 : 10;
        if (spec != 'd' && spec != 'i' && spec != 'u' && spec != 'x' && spec != 'X' &&
            spec != 'p') {
            dest[n] = 0;
            return -1;
        }
        u32 value;
        int minus = 0;
        if (spec == 'd' || spec == 'i') {
            int v = va_arg(args, int);
            minus = v < 0;
            value = minus ? 0u - (u32)v : (u32)v;
        } else if (spec == 'p')
            value = (u32)(uintptr_t)va_arg(args, void *);
        else
            value = va_arg(args, u32);
        char digits[16];
        int count = 0;
        do {
            unsigned digit = value % base;
            digits[count++] =
                (char)(digit < 10 ? '0' + digit : (spec == 'X' ? 'A' : 'a') + digit - 10);
            value /= base;
        } while (value);
        if (minus && zero) {
            CE_PUT('-');
            minus = 0;
            --width;
        }
        for (int pad = count + minus; pad < width; ++pad)
            CE_PUT(zero ? '0' : ' ');
        if (minus)
            CE_PUT('-');
        while (count)
            CE_PUT(digits[--count]);
    }
    dest[n] = 0;
    return (int)n;
#undef CE_PUT
}
int wsprintfW(u16 *dest, const u16 *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = ce_format(dest, 1024, format, args);
    va_end(args);
    return n;
}
int wvsprintfW(u16 *dest, const u16 *format, va_list args)
{
    return ce_format(dest, 1024, format, args);
}
u32 StringCbPrintfW(u16 *dest, u32 bytes, const u16 *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = ce_format(dest, bytes / 2, format, args);
    va_end(args);
    return n < 0 ? 0x8007007au : 0;
}

#ifndef CE_HOST
static int ce_format_words(u16 *dest, u32 capacity, const u16 *format, const u32 *words)
{
    u32 n = 0;
    if (!dest || !capacity || !format)
        return -1;
#define CE_PUT(ch)                                                                                 \
    do {                                                                                           \
        if (n + 1 >= capacity) {                                                                   \
            dest[capacity - 1] = 0;                                                                \
            return -1;                                                                             \
        }                                                                                          \
        dest[n++] = (u16)(ch);                                                                     \
    } while (0)
    while (*format) {
        if (*format != '%') {
            CE_PUT(*format++);
            continue;
        }
        ++format;
        if (*format == '%') {
            CE_PUT(*format++);
            continue;
        }
        int width = 0, zero = 0;
        if (*format == '0') {
            zero = 1;
            ++format;
        }
        while (*format >= '0' && *format <= '9') {
            if (width < 1024)
                width = width * 10 + (*format - '0');
            ++format;
        }
        if (*format == 'l')
            ++format;
        u16 spec = *format++;
        if (spec == 's') {
            const u16 *s = (const u16 *)(uintptr_t)*words++;
            if (s)
                while (*s)
                    CE_PUT(*s++);
            continue;
        }
        if (spec == 'c') {
            CE_PUT((int)*words++);
            continue;
        }
        unsigned base = (spec == 'x' || spec == 'X' || spec == 'p') ? 16 : 10;
        if (spec != 'd' && spec != 'i' && spec != 'u' && spec != 'x' && spec != 'X' &&
            spec != 'p') {
            dest[n] = 0;
            return -1;
        }
        u32 value;
        int minus = 0;
        if (spec == 'd' || spec == 'i') {
            int v = (int)*words++;
            minus = v < 0;
            value = minus ? 0u - (u32)v : (u32)v;
        } else if (spec == 'p')
            value = *words++;
        else
            value = *words++;
        char digits[16];
        int count = 0;
        do {
            unsigned digit = value % base;
            digits[count++] =
                (char)(digit < 10 ? '0' + digit : (spec == 'X' ? 'A' : 'a') + digit - 10);
            value /= base;
        } while (value);
        if (minus && zero) {
            CE_PUT('-');
            minus = 0;
            --width;
        }
        for (int pad = count + minus; pad < width; ++pad)
            CE_PUT(zero ? '0' : ' ');
        if (minus)
            CE_PUT('-');
        while (count)
            CE_PUT(digits[--count]);
    }
    dest[n] = 0;
    return (int)n;
#undef CE_PUT
}

int CeMsvc_wsprintfWWords(const u32 *w)
{
    return ce_format_words((u16 *)w[0], 1024, (const u16 *)w[1], w + 2);
}
u32 CeMsvc_StringCbPrintfWWords(const u32 *w)
{
    return ce_format_words((u16 *)w[0], w[1] / 2, (const u16 *)w[2], w + 3) < 0 ? 0x8007007au : 0;
}
int CeMsvc_wvsprintfW(u16 *dest, const u16 *format, const u32 *words)
{
    return ce_format_words(dest, 1024, format, words);
}

#endif
