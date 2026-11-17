#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/* ==============================================================================
 * include/shell.h - Mini-shell de debug (bonus KFS_2)
 * ==============================================================================
 */

#define SHELL_LINE_MAX 256
#define SHELL_ARGS_MAX 16

void shell_init(void);
void shell_handle_char(char c);

#endif /* SHELL_H */
