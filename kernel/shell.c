#include "shell.h"
#include "printk.h"
#include "vga.h"
#include "string.h"
#include "gdt.h"
#include "stack.h"
#include "power.h"

/* ==============================================================================
 * kernel/shell.c - Mini-shell de debug (bonus KFS_2)
 * ==============================================================================
 * Fonctionnement :
 *   1. Chaque touche tapee arrive dans shell_handle_char().
 *   2. Les caracteres sont accumules dans un tampon (la "ligne").
 *   3. Sur Entree, la ligne est decoupee en mots (argc / argv, comme en C),
 *      le premier mot est cherche dans la table des commandes, et la fonction
 *      associee est appelee.
 */

typedef struct {
    const char *name;
    const char *usage;
    const char *help;
    void      (*run)(int argc, char **argv);
} shell_command_t;

static char   line[SHELL_LINE_MAX];
static size_t line_len = 0;

/* ------------------------------------------------------------------ utils */

/* Convertit une chaine decimale en entier ; renvoie false si invalide */
static bool parse_uint(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (!s || !*s)
        return false;
    while (*s) {
        if (*s < '0' || *s > '9')
            return false;
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return true;
}

static void print_error(const char *msg, const char *arg)
{
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    printk("%s%s\n", msg, arg ? arg : "");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* --------------------------------------------------------------- commands */

static void cmd_help(int argc, char **argv);

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        printk("%s%s", argv[i], (i + 1 < argc) ? " " : "");
    printk("\n");
}

static void cmd_stack(int argc, char **argv)
{
    uint32_t lines = STACK_DUMP_DEFAULT_LINES;

    if (argc > 1) {
        if (strcmp(argv[1], "all") == 0)
            lines = 0;
        else if (!parse_uint(argv[1], &lines)) {
            print_error("stack: invalid argument: ", argv[1]);
            return;
        }
    }
    print_kernel_stack(lines);
}

static void cmd_gdt(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    gdt_print();
}

static void print_seg(const char *reg, uint16_t sel)
{
    printk("  %s = 0x%04x  -> GDT[%u] %-12s (RPL %u)\n",
           reg, sel, sel >> 3, gdt_selector_name(sel), sel & 3);
}

static void cmd_regs(int argc, char **argv)
{
    uint16_t cs, ds, es, fs, gs, ss;
    uint32_t esp, ebp, eflags, cr0;
    gdt_ptr_t gdtr;

    (void)argc;
    (void)argv;

    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    __asm__ __volatile__("mov %%ds, %0" : "=r"(ds));
    __asm__ __volatile__("mov %%es, %0" : "=r"(es));
    __asm__ __volatile__("mov %%fs, %0" : "=r"(fs));
    __asm__ __volatile__("mov %%gs, %0" : "=r"(gs));
    __asm__ __volatile__("mov %%ss, %0" : "=r"(ss));
    __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
    __asm__ __volatile__("mov %%ebp, %0" : "=r"(ebp));
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags));
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    __asm__ __volatile__("sgdt %0" : "=m"(gdtr));

    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("Segment registers:\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_seg("CS", cs);
    print_seg("DS", ds);
    print_seg("ES", es);
    print_seg("FS", fs);
    print_seg("GS", gs);
    print_seg("SS", ss);

    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("Other registers:\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    printk("  ESP    = 0x%08x   EBP = 0x%08x\n", esp, ebp);
    printk("  EFLAGS = 0x%08x   (IF=%u : interrupts %s)\n",
           eflags, (eflags >> 9) & 1, ((eflags >> 9) & 1) ? "on" : "off");
    printk("  CR0    = 0x%08x   (PE=%u protected mode, PG=%u paging)\n",
           cr0, cr0 & 1, (cr0 >> 31) & 1);
    printk("  GDTR   = base 0x%08x, limit 0x%04x (%u entries)\n",
           gdtr.base, gdtr.limit, ((uint32_t)gdtr.limit + 1) / 8);
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printk("Rebooting...\n");
    kernel_reboot();
}

static void cmd_halt(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    printk("CPU halted. You can now close the VM.\n");
    kernel_halt();
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printk("Shutting down...\n");
    kernel_shutdown();
}

static const shell_command_t commands[] = {
    { "help",     "help",              "list available commands",               cmd_help     },
    { "clear",    "clear",             "clear the screen",                      cmd_clear    },
    { "echo",     "echo [text...]",    "print text",                            cmd_echo     },
    { "stack",    "stack [N|all]",     "print the kernel stack (N dump lines)", cmd_stack    },
    { "gdt",      "gdt",               "print the loaded GDT",                  cmd_gdt      },
    { "regs",     "regs",              "print CPU / segment registers",         cmd_regs     },
    { "reboot",   "reboot",            "restart the machine",                   cmd_reboot   },
    { "halt",     "halt",              "stop the CPU",                          cmd_halt     },
    { "shutdown", "shutdown",          "power off (QEMU / Bochs / VirtualBox)", cmd_shutdown },
};

#define NB_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("Available commands:\n");
    for (size_t i = 0; i < NB_COMMANDS; i++) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("  %-16s", commands[i].usage);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        printk(" %s\n", commands[i].help);
    }
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    printk("Screens: [F1] boot  [F2] shell  [F3] colors  [F4] GDT & stack\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ------------------------------------------------------------------ core */

/**
 * shell_execute - Decoupe la ligne en mots puis lance la commande
 *
 * Le decoupage se fait "sur place" : chaque espace est remplace par '\0' et
 * argv[i] pointe sur le debut de chaque mot dans le tampon.
 */
static void shell_execute(char *cmdline)
{
    char *argv[SHELL_ARGS_MAX];
    int   argc = 0;
    char *p = cmdline;

    while (*p && argc < SHELL_ARGS_MAX) {
        while (*p == ' ')
            *p++ = '\0';
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
    }

    if (argc == 0)
        return;

    for (size_t i = 0; i < NB_COMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].run(argc, argv);
            return;
        }
    }
    print_error("kfs: command not found: ", argv[0]);
}

void shell_init(void)
{
    line_len = 0;
    print_prompt();
}

/**
 * shell_handle_char - Recoit un caractere tape au clavier
 */
void shell_handle_char(char c)
{
    if (c == '\n') {
        vga_putchar('\n');
        line[line_len] = '\0';
        shell_execute(line);
        line_len = 0;
        print_prompt();
    } else if (c == '\b') {
        /* On n'efface que ce qui a ete tape : le prompt est protege */
        if (line_len > 0) {
            line_len--;
            vga_putchar('\b');
        }
    } else if (c == '\t') {
        c = ' ';
        shell_handle_char(c);
    } else if (c >= 32 && c < 127) {
        if (line_len < SHELL_LINE_MAX - 1) {
            line[line_len++] = c;
            vga_putchar(c);
        }
    }
}
