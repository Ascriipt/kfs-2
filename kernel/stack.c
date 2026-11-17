#include "stack.h"
#include "gdt.h"
#include "printk.h"
#include "vga.h"

/* ==============================================================================
 * kernel/stack.c - Affichage "human-friendly" de la pile noyau
 * ==============================================================================
 * Rappel : sur x86 la pile grandit vers le BAS.
 *   - stack_top    : adresse la plus haute, la pile commence ici (vide)
 *   - ESP          : sommet actuel de la pile (derniere valeur empilee)
 *   - stack_bottom : limite basse, si ESP descend sous cette adresse = overflow
 *
 * Chaque appel de fonction C (compile avec -fno-omit-frame-pointer) cree un
 * "cadre de pile" (stack frame) :
 *
 *        adresses hautes
 *   |  arguments de la fonction  |
 *   |  adresse de retour         |  <- [EBP + 4]  (ou revenir dans l'appelant)
 *   |  EBP de l'appelant         |  <- [EBP]      (lien vers le cadre precedent)
 *   |  variables locales         |
 *   |  ...                       |  <- ESP
 *        adresses basses
 *
 * En suivant la chaine des EBP sauvegardes, on remonte toute la pile d'appels
 * (backtrace) jusqu'a _start, qui a mis EBP a 0 pour marquer la fin.
 */

#define STACK_MAX_FRAMES 16
#define STACK_BAR_WIDTH  30

typedef struct {
    uint32_t ebp;   /* adresse du cadre (ou est sauvegarde l'EBP precedent) */
    uint32_t ret;   /* adresse de retour stockee en [ebp + 4]               */
} stack_frame_t;

/* Statique (dans .bss) pour ne pas gonfler la pile qu'on est en train d'afficher */
static stack_frame_t frames[STACK_MAX_FRAMES];

/**
 * collect_frames - Remonte la chaine des EBP et memorise chaque cadre
 *
 * Par securite, on s'arrete des qu'un EBP sort de la pile noyau, n'est pas
 * aligne, ou ne "remonte" pas (ce qui signifierait une pile corrompue).
 */
static size_t collect_frames(uint32_t ebp)
{
    uint32_t low  = (uint32_t)stack_bottom;
    uint32_t high = (uint32_t)stack_top;
    size_t   n    = 0;

    while (n < STACK_MAX_FRAMES && ebp >= low && ebp + 8 <= high && (ebp & 3) == 0) {
        const uint32_t *frame = (const uint32_t *)ebp;

        frames[n].ebp = ebp;
        frames[n].ret = frame[1];
        n++;

        if (frame[0] != 0 && frame[0] <= ebp)
            break;
        ebp = frame[0];
    }
    return n;
}

/**
 * caller_lookup - Nom de la fonction qui contient l'instruction 'call'
 *
 * Une adresse de retour pointe sur l'instruction qui SUIT le 'call'. Si le
 * 'call' est la derniere instruction d'une fonction, l'adresse de retour tombe
 * deja dans le symbole suivant. On cherche donc (ret - 1), qui est toujours
 * dans l'instruction 'call' elle-meme, puis on corrige le decalage.
 */
static const char *caller_lookup(uint32_t ret, uint32_t *offset)
{
    const char *name = symbol_lookup(ret - 1, offset);

    if (name && offset)
        *offset += 1;
    return name;
}

/**
 * print_symbol - Affiche "fonction+0xdecalage" ou l'adresse brute si inconnue
 */
static void print_symbol(uint32_t ret)
{
    uint32_t offset = 0;
    const char *name = caller_lookup(ret, &offset);

    if (name)
        printk("%s+0x%x", name, offset);
    else
        printk("0x%08x", ret);
}

/**
 * print_usage_bar - Petite jauge visuelle du remplissage de la pile
 */
static void print_usage_bar(uint32_t used, uint32_t size)
{
    uint32_t filled = (used * STACK_BAR_WIDTH) / size;

    if (used > 0 && filled == 0)
        filled = 1;

    vga_putchar('[');
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < filled; i++)
        vga_putchar('#');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    for (uint32_t i = filled; i < STACK_BAR_WIDTH; i++)
        vga_putchar('-');
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar(']');
}

/**
 * word_kind - Indique si un mot de la pile est un EBP sauvegarde (1),
 * une adresse de retour (2) ou une donnee quelconque (0).
 */
static int word_kind(uint32_t addr, size_t nb_frames)
{
    for (size_t i = 0; i < nb_frames; i++) {
        if (addr == frames[i].ebp)
            return 1;
        if (addr == frames[i].ebp + 4)
            return 2;
    }
    return 0;
}

static void print_ascii(const uint8_t *bytes, size_t n)
{
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    for (size_t i = 0; i < n; i++)
        vga_putchar((bytes[i] >= 32 && bytes[i] < 127) ? (char)bytes[i] : '.');
}

/**
 * print_kernel_stack - Affiche l'etat de la pile noyau de facon lisible
 *
 * 1. Resume   : segment SS, limites, ESP/EBP, taux d'utilisation
 * 2. Backtrace: la liste des fonctions en cours d'execution (la plus recente
 *               en premier) avec qui les a appelees
 * 3. Dump     : le contenu brut de la pile, de ESP vers le haut, avec les EBP
 *               sauvegardes et les adresses de retour colores
 *
 * max_dump_lines : nombre maximum de lignes du dump (0 = tout afficher)
 */
void print_kernel_stack(size_t max_dump_lines)
{
    uint32_t esp, ebp;
    uint16_t ss;

    __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
    __asm__ __volatile__("mov %%ebp, %0" : "=r"(ebp));
    __asm__ __volatile__("mov %%ss, %0"  : "=r"(ss));

    uint32_t bottom = (uint32_t)stack_bottom;
    uint32_t top    = (uint32_t)stack_top;
    uint32_t size   = top - bottom;
    uint32_t used   = (esp <= top) ? top - esp : 0;

    /* ---------------------------------------------------------------- 1. Resume */
    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("============================ KERNEL STACK ============================\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk(" Segment : SS = 0x%02x (%s)\n", ss, gdt_selector_name(ss));
    printk(" Region  : 0x%08x (bottom) -> 0x%08x (top) = %u bytes\n", bottom, top, size);
    printk(" ESP     : 0x%08x   EBP : 0x%08x   (grows DOWN from top)\n", esp, ebp);
    printk(" Usage   : %u / %u bytes (%u%%) ", used, size, (used * 100) / size);
    print_usage_bar(used, size);
    printk("\n");

    if (esp < bottom || esp > top) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk(" /!\\ ESP is OUTSIDE the kernel stack (overflow?)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /* ------------------------------------------------------------- 2. Backtrace */
    size_t nb_frames = collect_frames(ebp);
    bool   have_syms = symbols_count() > 0;

    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk(" Call trace (newest first):\n");

    for (size_t i = 0; i < nb_frames; i++) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        printk("  #%-2u [0x%08x] ", i, frames[i].ebp);

        if (have_syms) {
            /* La fonction qui possede le cadre i est celle ou "revient" le cadre i-1 */
            uint32_t off = 0;
            const char *owner = (i == 0) ? __func__ : caller_lookup(frames[i - 1].ret, &off);
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("%-22s", owner ? owner : "???");
            vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            printk(" <- called by ");
            vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            print_symbol(frames[i].ret);
        } else {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            printk("returns to 0x%08x", frames[i].ret);
        }
        printk("\n");
    }
    if (!have_syms) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        printk("  (no symbol table: boot the GRUB ISO to see function names)\n");
    }

    /* ----------------------------------------------------------- 3. Dump brut */
    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk(" Raw dump ESP -> top  (");
    vga_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
    printk("saved EBP");
    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk(", ");
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    printk("return address");
    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("):\n");

    size_t lines = 0;
    uint32_t addr = esp & ~(uint32_t)3;

    while (addr < top) {
        if (max_dump_lines && lines >= max_dump_lines) {
            vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            printk("  ... %u more bytes up to stack_top (type 'stack all')\n", top - addr);
            break;
        }

        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("  0x%08x +0x%03x: ", addr, addr - (esp & ~(uint32_t)3));

        size_t words = 0;
        for (; words < 4 && addr + words * 4 < top; words++) {
            uint32_t waddr = addr + words * 4;
            int kind = word_kind(waddr, nb_frames);

            if (kind == 1)
                vga_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
            else if (kind == 2)
                vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            else
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            printk("%08x ", *(const uint32_t *)waddr);
        }
        for (size_t pad = words; pad < 4; pad++)
            printk("         ");

        printk(" ");
        print_ascii((const uint8_t *)addr, words * 4);
        printk("\n");

        addr += 16;
        lines++;
    }

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
