#include "power.h"
#include "io.h"
#include "types.h"

/* ==============================================================================
 * kernel/power.c - reboot / halt / shutdown
 * ==============================================================================
 */

#define KBD_CONTROLLER_PORT  0x64
#define KBD_CMD_PULSE_RESET  0xFE   /* Commande 8042 : "tire" la ligne RESET du CPU */
#define KBD_STATUS_INPUT_FULL 0x02  /* Bit 1 : le controleur n'a pas encore lu la commande precedente */

/**
 * kernel_halt - Arrete definitivement le processeur
 *
 * 'cli' coupe les interruptions, 'hlt' endort le CPU jusqu'a la prochaine
 * interruption. Comme elles sont coupees, il ne se reveille jamais (sauf NMI,
 * d'ou la boucle).
 */
void kernel_halt(void)
{
    for (;;)
        __asm__ __volatile__("cli; hlt");
}

/**
 * kernel_reboot - Redemarre la machine
 *
 * 1. Methode classique : demander au controleur clavier 8042 de redemarrer le
 *    CPU (commande 0xFE). C'est un heritage de l'IBM PC/AT, toujours supporte.
 * 2. Secours : "triple fault". On charge une IDT vide puis on declenche une
 *    interruption : le CPU ne trouve pas de gestionnaire -> faute -> double
 *    faute -> triple faute -> le processeur se reinitialise tout seul.
 */
void kernel_reboot(void)
{
    __asm__ __volatile__("cli");

    /* Attendre que le controleur soit pret a recevoir une commande */
    for (uint32_t timeout = 0; timeout < 0x100000; timeout++) {
        if (!(inb(KBD_CONTROLLER_PORT) & KBD_STATUS_INPUT_FULL))
            break;
    }
    outb(KBD_CONTROLLER_PORT, KBD_CMD_PULSE_RESET);

    /* Si on est toujours la, triple faute volontaire */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } null_idt = { 0, 0 };

    __asm__ __volatile__("lidt %0; int $3" : : "m"(null_idt));

    kernel_halt();
}

/**
 * kernel_shutdown - Eteint la machine virtuelle
 *
 * Sans pilote ACPI complet, on utilise les ports "magiques" des emulateurs :
 *   - QEMU (machine pc recente) : port 0x604,  valeur 0x2000
 *   - Bochs / ancien QEMU       : port 0xB004, valeur 0x2000
 *   - VirtualBox                : port 0x4004, valeur 0x3400
 * Si rien ne marche (vraie machine), on se contente d'arreter le CPU.
 */
void kernel_shutdown(void)
{
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    kernel_halt();
}
