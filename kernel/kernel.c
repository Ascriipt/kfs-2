#include "types.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"
#include "printk.h"
#include "multiboot.h"
#include "gdt.h"
#include "stack.h"
#include "shell.h"

/* ==============================================================================
 * kernel/kernel.c - Point d'entree C du noyau KFS_2
 * ==============================================================================
 * Ecrans virtuels :
 *   [F1] TTY 0 : ecran de boot + "42"
 *   [F2] TTY 1 : mini-shell de debug (bonus)
 *   [F3] TTY 2 : palette de couleurs VGA
 *   [F4] TTY 3 : GDT chargee + pile noyau (partie obligatoire KFS_2)
 */

#define TTY_BOOT   0
#define TTY_SHELL  1
#define TTY_COLORS 2
#define TTY_DEBUG  3

static void init_tty_shell(void)
{
    vga_switch_tty(TTY_SHELL);
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("================================================================================");
    printk("                  TTY 1 - KFS_2 debug shell\n");
    printk("================================================================================\n");
    printk("Type 'help' to list the commands (stack, gdt, regs, reboot, halt...)\n");
    printk("Screens : [F1] Boot '42' | [F2] Shell | [F3] Colors | [F4] GDT & Stack\n\n");
    shell_init();
}

static void init_tty_colors(void)
{
    vga_switch_tty(TTY_COLORS);
    vga_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
    printk("================================================================================");
    printk("                  TTY 2 - Palette VGA & Infos Noyau\n");
    printk("================================================================================\n");

    printk("Test des 16 couleurs du controleur VGA :\n\n");
    for (int i = 0; i < 16; i++) {
        vga_set_color((vga_color_t)i, VGA_COLOR_BLACK);
        printk(" [Couleur %2d] ", i);
        if ((i + 1) % 4 == 0) {
            printk("\n");
        }
    }

    printk("\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("Architecture : x86 32-bit (i386 Protected Mode)\n");
    printk("GDT          : %u descripteurs a 0x%08x\n", GDT_ENTRIES, GDT_ADDRESS);
    printk("Pile noyau   : %u Ko dans .bss (0x%08x - 0x%08x)\n",
           (uint32_t)(stack_top - stack_bottom) / 1024,
           (uint32_t)stack_bottom, (uint32_t)stack_top);
    printk("Pilotes      : Multiboot 1, VGA Text Mode (80x25), Clavier PS/2 (Set 1)\n");
}

/*
 * TTY 3 : demonstration de la partie obligatoire sans passer par le shell :
 * la GDT reellement chargee (relue via sgdt) et la pile noyau.
 */
static void init_tty_debug(void)
{
    vga_switch_tty(TTY_DEBUG);
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("TTY 3 - Global Descriptor Table & Kernel Stack (snapshot taken at boot)\n");
    gdt_print();
    print_kernel_stack(2);
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    printk("Live version: type 'stack' / 'gdt' in the shell [F2]");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    /* 1. Initialisation des pilotes d'affichage et du clavier */
    vga_init();
    keyboard_init();

    /* 2. Verification du Magic Multiboot */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[ERREUR] Magic Multiboot invalide : 0x%x (attendu : 0x%x)\n",
               magic, MULTIBOOT_BOOTLOADER_MAGIC);
        while (1) { }
    }

    /* 3. On recopie les symboles ELF fournis par GRUB AVANT d'ecrire la GDT :
     *    la memoire ou GRUB les a ranges pourrait se trouver en zone basse. */
    symbols_init(mbi);

    /* 4. Creation de la GDT a 0x00000800 et chargement dans le CPU (lgdt) */
    gdt_init();

    /* 5. Ecran de boot (TTY 0) */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("================================================================================");
    printk("                   KFS_2 (Kernel From Scratch) - GDT & Stack\n");
    printk("================================================================================\n");

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] Boot Multiboot valide    : magic = 0x%x\n", magic);
    printk("[OK] Table Multiboot info     : %p\n", mbi);
    printk("[OK] GDT chargee              : %u descripteurs a 0x%08x\n", GDT_ENTRIES, GDT_ADDRESS);
    printk("[OK] Segments recharges       : CS=0x%02x DS=0x%02x SS=0x%02x\n",
           GDT_KERNEL_CODE_SEL, GDT_KERNEL_DATA_SEL, GDT_KERNEL_STACK_SEL);
    if (symbols_count() > 0)
        printk("[OK] Symboles ELF             : %u fonctions\n", symbols_count());
    else
        printk("[--] Symboles ELF             : indisponibles (boot sans GRUB)\n");
    printk("[OK] Pilotes VGA, PS/2 & Libk charges, 4 terminaux virtuels\n\n");

    /* Affichage obligatoire du sujet KFS_1 : "42" */
    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLUE);
    printk("  ********************  \n");
    printk("  *                  *  \n");
    printk("  *        42        *  \n");
    printk("  *                  *  \n");
    printk("  ********************  \n\n");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Ecrans : [F1] Boot '42' | [F2] Shell | [F3] Couleurs | [F4] GDT & Pile\n");

    /* 6. Pre-remplissage des autres terminaux virtuels */
    init_tty_shell();
    init_tty_colors();
    init_tty_debug();

    /* 7. Retour sur l'ecran de boot */
    vga_switch_tty(TTY_BOOT);

    /* 8. Boucle interactive principale (scrutation / polling du clavier) */
    while (1) {
        if (!keyboard_has_key())
            continue;

        uint8_t scancode = keyboard_get_scancode();

        /* Raccourcis pour changer d'ecran virtuel */
        if (scancode == SCANCODE_F1) {
            vga_switch_tty(TTY_BOOT);
        } else if (scancode == SCANCODE_F2) {
            vga_switch_tty(TTY_SHELL);
        } else if (scancode == SCANCODE_F3) {
            vga_switch_tty(TTY_COLORS);
        } else if (scancode == SCANCODE_F4) {
            vga_switch_tty(TTY_DEBUG);
        } else {
            char c = keyboard_scancode_to_char(scancode);
            if (!c)
                continue;
            if (vga_get_current_tty() == TTY_SHELL)
                shell_handle_char(c);      /* Le shell gere sa ligne de commande */
            else
                vga_putchar(c);            /* Ecrans "libres" : simple echo */
        }
    }
}
