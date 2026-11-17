#include "stack.h"
#include "string.h"

/* ==============================================================================
 * kernel/symbols.c - Table des symboles du noyau (adresse -> nom de fonction)
 * ==============================================================================
 * Le binaire kernel.bin est un ELF non "strippe" : il contient une section
 * .symtab (liste des symboles : nom, adresse, taille) et une section .strtab
 * (les noms eux-memes). Ces sections ne sont pas chargees en memoire comme le
 * code, mais GRUB les copie quand meme en RAM et nous donne leur adresse via la
 * structure multiboot_info (champ elf_sec).
 *
 * On recopie les symboles qui nous interessent (ceux du code, .text) dans un
 * tableau du noyau (.bss). Ainsi on ne depend plus de la memoire de GRUB, qui
 * pourrait etre ecrasee plus tard (par exemple par la GDT ecrite en 0x800).
 *
 * Remarque : avec "qemu -kernel" (make run-kernel), QEMU ne fournit pas les
 * sections ELF ; la backtrace affichera alors seulement les adresses.
 */

#define SYMBOLS_MAX     256
#define SYMBOL_NAME_MAX 32

typedef struct {
    uint32_t addr;
    uint32_t size;
    char     name[SYMBOL_NAME_MAX];
} ksymbol_t;

/* Bornes de la section .text (definies dans linker.ld) */
extern uint8_t _text_start[];
extern uint8_t _text_end[];

static ksymbol_t symbols[SYMBOLS_MAX];
static size_t    nb_symbols = 0;

/**
 * symbols_init - Recupere la table des symboles ELF transmise par GRUB
 */
void symbols_init(const multiboot_info_t *mbi)
{
    nb_symbols = 0;

    if (!mbi || !(mbi->flags & MULTIBOOT_INFO_ELF_SHDR))
        return;

    const multiboot_elf_section_t *es = &mbi->elf_sec;
    if (es->addr == 0 || es->size < sizeof(elf32_shdr_t))
        return;

    for (uint32_t i = 0; i < es->num; i++) {
        const elf32_shdr_t *sh = (const elf32_shdr_t *)(es->addr + i * es->size);

        if (sh->sh_type != SHT_SYMTAB || sh->sh_link >= es->num)
            continue;

        /* sh_link de la .symtab = index de la .strtab associee */
        const elf32_shdr_t *str_sh = (const elf32_shdr_t *)(es->addr + sh->sh_link * es->size);
        if (sh->sh_addr == 0 || str_sh->sh_addr == 0)
            continue;

        const elf32_sym_t *syms    = (const elf32_sym_t *)sh->sh_addr;
        uint32_t           count   = sh->sh_size / sizeof(elf32_sym_t);
        const char        *strtab  = (const char *)str_sh->sh_addr;
        uint32_t           strsize = str_sh->sh_size;

        for (uint32_t s = 0; s < count && nb_symbols < SYMBOLS_MAX; s++) {
            const elf32_sym_t *sym = &syms[s];
            uint8_t type = ELF32_ST_TYPE(sym->st_info);

            /* On garde les fonctions (FUNC) et les labels locaux assembleur
             * (NOTYPE + LOCAL, ex: "_start.hang"). Les marqueurs globaux du
             * linker (_text_start...) sont ignores pour ne pas masquer _start. */
            if (type != STT_FUNC
                && !(type == STT_NOTYPE && ELF32_ST_BIND(sym->st_info) == STB_LOCAL))
                continue;
            /* ... qui pointent dans le code du noyau */
            if (sym->st_value < (uint32_t)_text_start || sym->st_value >= (uint32_t)_text_end)
                continue;
            if (sym->st_name == 0 || sym->st_name >= strsize)
                continue;

            ksymbol_t *dst = &symbols[nb_symbols++];
            dst->addr = sym->st_value;
            dst->size = sym->st_size;

            /* Copie bornee du nom (on ne fait pas confiance a la taille) */
            const char *name = strtab + sym->st_name;
            size_t n = 0;
            while (n < SYMBOL_NAME_MAX - 1 && sym->st_name + n < strsize && name[n]) {
                dst->name[n] = name[n];
                n++;
            }
            dst->name[n] = '\0';
        }
        break;
    }
}

size_t symbols_count(void)
{
    return nb_symbols;
}

/**
 * symbol_lookup - Trouve la fonction qui contient une adresse donnee
 *
 * Renvoie le nom du symbole le plus proche situe juste avant 'addr' et, dans
 * *offset, la distance entre le debut de cette fonction et 'addr'.
 * Renvoie NULL si aucun symbole ne correspond.
 */
const char *symbol_lookup(uint32_t addr, uint32_t *offset)
{
    const ksymbol_t *best = NULL;

    for (size_t i = 0; i < nb_symbols; i++) {
        const ksymbol_t *s = &symbols[i];
        if (s->addr <= addr && (!best || s->addr > best->addr))
            best = s;
    }

    /* Si on connait la taille de la fonction, l'adresse doit tomber dedans
     * ('<=' : une adresse de retour peut pointer juste apres un dernier 'call') */
    if (!best || (best->size && addr > best->addr + best->size))
        return NULL;

    if (offset)
        *offset = addr - best->addr;
    return best->name;
}
