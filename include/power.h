#ifndef POWER_H
#define POWER_H

/* ==============================================================================
 * include/power.h - Redemarrage, arret et mise en pause du processeur
 * ==============================================================================
 */

void kernel_reboot(void)   __attribute__((noreturn));
void kernel_halt(void)     __attribute__((noreturn));
void kernel_shutdown(void) __attribute__((noreturn));

#endif /* POWER_H */
