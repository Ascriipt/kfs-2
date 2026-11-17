#include "printk.h"
#include "vga.h"
#include "string.h"

/* ==============================================================================
 * kernel/printk.c - Implementation de printk (formatage et affichage ecran)
 * ==============================================================================
 * Conversions supportees : %c %s %d %i %u %x %X %p %%
 * Options supportees     : largeur minimale et drapeaux '-' et '0'
 *   %08x  -> nombre hexa complete a gauche par des zeros sur 8 caracteres
 *   %-12s -> chaine alignee a gauche sur 12 caracteres (espaces a droite)
 *   %4u   -> nombre aligne a droite sur 4 caracteres
 */

static const char hex_digits_lower[] = "0123456789abcdef";
static const char hex_digits_upper[] = "0123456789ABCDEF";

/* Options de formatage lues entre le '%' et la lettre de conversion */
typedef struct {
    bool left;      /* '-' : alignement a gauche  */
    char pad;       /* '0' ou ' ' : caractere de remplissage */
    int  width;     /* largeur minimale */
} fmt_opts_t;

static int put_padding(char c, int n)
{
    int count = 0;
    while (n-- > 0) {
        vga_putchar(c);
        count++;
    }
    return count;
}

/**
 * Convertit un entier en chaine dans une base donnee (10, 16, etc.)
 * Gere correctement les entiers non signes et signes sans overflow,
 * ainsi que la largeur minimale et le remplissage.
 */
static int print_number(uint32_t val, int base, bool is_signed, bool upper,
                        const fmt_opts_t *opts)
{
    char buf[32];
    int count = 0;
    int i = 0;
    bool negative = false;
    const char *digits = upper ? hex_digits_upper : hex_digits_lower;

    if (is_signed && ((int32_t)val < 0))
    {
        negative = true;
        val = (uint32_t)(-(int32_t)val);
    }

    if (val == 0)
        buf[i++] = '0';

    while (val > 0)
    {
        buf[i++] = digits[val % base];
        val /= base;
    }

    int len = i + (negative ? 1 : 0);
    int padding = (opts->width > len) ? opts->width - len : 0;

    if (!opts->left && opts->pad == ' ')
        count += put_padding(' ', padding);

    if (negative)
    {
        vga_putchar('-');
        count++;
    }

    if (!opts->left && opts->pad == '0')
        count += put_padding('0', padding);

    while (i > 0)
    {
        vga_putchar(buf[--i]);
        count++;
    }

    if (opts->left)
        count += put_padding(' ', padding);

    return count;
}

static int print_string(const char *str, const fmt_opts_t *opts)
{
    int count = 0;
    int len;

    if (!str)
        str = "(null)";

    len = (int)strlen(str);

    if (!opts->left && opts->width > len)
        count += put_padding(' ', opts->width - len);

    while (*str) {
        vga_putchar(*str++);
        count++;
    }

    if (opts->left && opts->width > len)
        count += put_padding(' ', opts->width - len);

    return count;
}

int vprintk(const char *fmt, va_list args)
{
    if (!fmt) return 0;

    int printed_chars = 0;

    for (size_t i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] != '%')
        {
            vga_putchar(fmt[i]);
            printed_chars++;
            continue;
        }

        i++;

        /* Lecture des drapeaux et de la largeur : %[-0][largeur]conversion */
        fmt_opts_t opts = { false, ' ', 0 };
        while (fmt[i] == '-' || fmt[i] == '0')
        {
            if (fmt[i] == '-')
                opts.left = true;
            else
                opts.pad = '0';
            i++;
        }
        while (fmt[i] >= '0' && fmt[i] <= '9')
        {
            opts.width = opts.width * 10 + (fmt[i] - '0');
            i++;
        }
        /* '-' l'emporte sur '0' (comme dans la libc) */
        if (opts.left)
            opts.pad = ' ';

        if (fmt[i] == '\0') break;

        switch (fmt[i])
        {
            case '%':
                vga_putchar('%');
                printed_chars++;
                break;

            case 'c':
            {
                char c = (char)va_arg(args, int);
                if (!opts.left)
                    printed_chars += put_padding(' ', opts.width - 1);
                vga_putchar(c);
                printed_chars++;
                if (opts.left)
                    printed_chars += put_padding(' ', opts.width - 1);
                break;
            }

            case 's':
            {
                const char *str = va_arg(args, const char *);
                printed_chars += print_string(str, &opts);
                break;
            }

            case 'd':
            case 'i':
            {
                int32_t d = va_arg(args, int32_t);
                printed_chars += print_number((uint32_t)d, 10, true, false, &opts);
                break;
            }

            case 'u':
            {
                uint32_t u = va_arg(args, uint32_t);
                printed_chars += print_number(u, 10, false, false, &opts);
                break;
            }

            case 'x':
            {
                uint32_t x = va_arg(args, uint32_t);
                printed_chars += print_number(x, 16, false, false, &opts);
                break;
            }

            case 'X':
            {
                uint32_t x = va_arg(args, uint32_t);
                printed_chars += print_number(x, 16, false, true, &opts);
                break;
            }

            case 'p':
            {
                /* Pointeur : toujours "0x" + 8 chiffres hexa, plus lisible */
                uint32_t p = (uint32_t)va_arg(args, void *);
                fmt_opts_t ptr_opts = { false, '0', 8 };
                vga_puts("0x");
                printed_chars += 2;
                printed_chars += print_number(p, 16, false, false, &ptr_opts);
                break;
            }

            default:
                vga_putchar('%');
                vga_putchar(fmt[i]);
                printed_chars += 2;
                break;
        }
    }

    return printed_chars;
}

int printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vprintk(fmt, args);
    va_end(args);
    return ret;
}
