#ifndef STACK_H
#define STACK_H

#include "types.h"
#include "multiboot.h"

/* ==============================================================================
 * include/stack.h - Outils d'inspection de la pile noyau (kernel stack)
 * ==============================================================================
 */

/* Limites de la pile noyau, definies dans boot/boot.s */
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

/* Nombre de lignes du dump brut affichees par defaut */
#define STACK_DUMP_DEFAULT_LINES 8

/* Copie la table des symboles fournie par GRUB (a appeler AVANT gdt_init) */
void        symbols_init(const multiboot_info_t *mbi);
size_t      symbols_count(void);
const char *symbol_lookup(uint32_t addr, uint32_t *offset);

/* Affiche la pile noyau de facon lisible (resume, backtrace, dump brut) */
void        print_kernel_stack(size_t max_dump_lines);

#endif /* STACK_H */
