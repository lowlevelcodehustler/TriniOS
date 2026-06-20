// TriniOS - Minimal freestanding printf/snprintf implementation (x86_64)
// No libc dependency — uses raw syscalls via syscall.h

#include "stdio.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

static inline int my_strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (int)(p - s);
}

static inline long my_atoi(const char *s, int *consumed)
{
    long result = 0;
    int sign = 1;
    int pos = 0;

    if (s[pos] == '-') { sign = -1; pos++; }
    else if (s[pos] == '+') { pos++; }

    while (s[pos] >= '0' && s[pos] <= '9') {
        result = result * 10 + (s[pos] - '0');
        pos++;
    }

    if (consumed) *consumed = pos;
    return sign * result;
}

/* Convert integer to string in given base (2-36). Returns digits written. */
static int itoa_unsigned(unsigned long n, char *buf, int base, int uppercase)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[64];
    int i = 0;

    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (n > 0) {
        unsigned long rem = n % base;
        tmp[i++] = uppercase ? (char)(digits[rem] & ~0x20) : digits[rem];
        n /= base;
    }

    /* Reverse */
    for (int j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[i] = '\0';
    return i;
}

/* ── Internal buffer writer ─────────────────────────────────────────── */

typedef struct {
    char *buf;
    int pos;
    int size;       /* max size (including NUL) */
    int written;    /* total chars that would be written (if buf==NULL) */
} fmt_buf_t;

static void fb_putc(fmt_buf_t *fb, char c)
{
    if (fb->buf && fb->pos < fb->size - 1) {
        fb->buf[fb->pos++] = c;
    }
    fb->written++;
}

static void fb_puts(fmt_buf_t *fb, const char *s)
{
    while (*s) fb_putc(fb, *s++);
}

/* ── printf / snprintf core ─────────────────────────────────────────── */

static void format_string(fmt_buf_t *fb, const char *fmt, va_list args)
{
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == '%') {
                fb_putc(fb, '%');
                fmt++;
                continue;
            }

            /* Parse flags */
            int left_align = 0;
            int zero_pad = 0;
            int width = 0;
            char *pad_char = NULL;

            if (*fmt == '-') { left_align = 1; fmt++; }
            else if (*fmt == '0') { zero_pad = 1; fmt++; }

            /* Parse width */
            if (*fmt >= '1' && *fmt <= '9') {
                int consumed;
                width = (int)my_atoi(fmt, &consumed);
                fmt += consumed;
            } else if (*fmt == '*') {
                width = va_arg(args, int);
                fmt++;
            }

            /* Pad char */
            if (zero_pad && !left_align) pad_char = "0";
            else if (!left_align) pad_char = " ";

            /* Length modifier */
            int is_long = 0;
            if (*fmt == 'l') { is_long = 1; fmt++; }

            /* Format specifier */
            switch (*fmt) {
            case 's': {
                const char *str = va_arg(args, const char *);
                if (!str) str = "(null)";
                int slen = my_strlen(str);
                int pad = width > slen ? width - slen : 0;

                if (left_align) {
                    fb_puts(fb, str);
                    for (int i = 0; i < pad; i++) fb_putc(fb, ' ');
                } else {
                    for (int i = 0; i < pad; i++) fb_puts(fb, pad_char ? pad_char : " ");
                    fb_puts(fb, str);
                }
                break;
            }

            case 'd':
            case 'i': {
                long val = is_long ? va_arg(args, long) : va_arg(args, int);
                char tmp[32];
                int neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                itoa_unsigned((unsigned long)val, tmp, 10, 0);
                if (neg) { tmp[0] = '-'; /* prepend: shift */
                    int len = my_strlen(tmp);
                    for (int i = len + 1; i > 0; i--) tmp[i] = tmp[i-1];
                    tmp[0] = '-';
                }
                int slen = my_strlen(tmp);
                int pad2 = width > slen ? width - slen : 0;

                if (left_align) {
                    fb_puts(fb, neg && val != -(long)val ? "" : tmp); /* skip minus for LONG_MIN */
                    /* Actually handle properly: */
                    char out[32];
                    if (neg) {
                        out[0] = '-';
                        __builtin_memcpy(out + 1, tmp, slen + 1);
                        fb_puts(fb, out);
                    } else {
                        fb_puts(fb, tmp);
                    }
                    for (int i = 0; i < pad2; i++) fb_putc(fb, ' ');
                } else {
                    if (neg) fb_putc(fb, '-');
                    for (int i = 0; i < pad2; i++) fb_puts(fb, pad_char ? pad_char : " ");
                    /* Re-print with minus handling */
                    char out[32];
                    if (neg) {
                        out[0] = '-';
                        __builtin_memcpy(out + 1, tmp, slen + 1);
                        fb_puts(fb, out);
                    } else {
                        fb_puts(fb, tmp);
                    }
                }
                break;
            }

            case 'u': {
                unsigned long val = is_long ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                char tmp[32];
                itoa_unsigned(val, tmp, 10, 0);
                int slen = my_strlen(tmp);
                int pad2 = width > slen ? width - slen : 0;

                if (left_align) {
                    fb_puts(fb, tmp);
                    for (int i = 0; i < pad2; i++) fb_putc(fb, ' ');
                } else {
                    for (int i = 0; i < pad2; i++) fb_puts(fb, " ");
                    fb_puts(fb, tmp);
                }
                break;
            }

            case 'x': {
                unsigned long val = is_long ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                char tmp[32];
                itoa_unsigned(val, tmp, 16, 0);
                int slen = my_strlen(tmp);
                int pad2 = width > slen ? width - slen : 0;

                if (left_align) {
                    fb_puts(fb, tmp);
                    for (int i = 0; i < pad2; i++) fb_putc(fb, ' ');
                } else {
                    for (int i = 0; i < pad2; i++) fb_puts(fb, " ");
                    fb_puts(fb, tmp);
                }
                break;
            }

            case 'p': {
                unsigned long val = (unsigned long)va_arg(args, void *);
                fb_puts(fb, "0x");
                char tmp[32];
                itoa_unsigned(val, tmp, 16, 0);
                fb_puts(fb, tmp);
                break;
            }

            case 'c': {
                char c = (char)va_arg(args, int);
                fb_putc(fb, c);
                break;
            }

            default:
                fb_putc(fb, '%');
                fb_putc(fb, *fmt);
                break;
            }
        } else {
            fb_putc(fb, *fmt);
        }
        fmt++;
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fmt_buf_t fb;
    fb.buf = buf;
    fb.pos = 0;
    fb.size = (int)size;
    fb.written = 0;

    format_string(&fb, fmt, args);
    va_end(args);

    /* NUL terminate */
    if (buf && fb.pos < fb.size) {
        buf[fb.pos] = '\0';
    } else if (buf && size > 0) {
        buf[size - 1] = '\0';
    }

    return fb.written;
}

void printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* First, compute the output length */
    fmt_buf_t fb;
    fb.buf = NULL;
    fb.pos = 0;
    fb.size = 0;
    fb.written = 0;

    format_string(&fb, fmt, args);
    va_end(args);

    /* Now, actually format into a stack buffer (max 4096) */
    char out[4096];
    va_start(args, fmt);
    fb.buf = out;
    fb.pos = 0;
    fb.size = sizeof(out);
    format_string(&fb, fmt, args);
    va_end(args);

    if (fb.pos > 0) {
        write(STDOUT_FILENO, out, (size_t)fb.pos);
    }
}