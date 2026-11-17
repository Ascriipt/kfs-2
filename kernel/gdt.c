#include "gdt.h"
#include "printk.h"
#include "vga.h"

/* ==============================================================================
 * kernel/gdt.c - Creation, remplissage et chargement de la GDT
 * ==============================================================================
 * La table est ecrite directement a l'adresse physique 0x00000800 (imposee par
 * le sujet). Cette zone (0x500 - 0x7BFF) est de la memoire conventionnelle
 * libre : elle se situe apres la table des vecteurs d'interruption du BIOS
 * (0x000 - 0x3FF) et la BIOS Data Area (0x400 - 0x4FF).
 *
 * L'adresse vient du symbole 'gdt_location' defini dans linker.ld. Passer par
 * un symbole du linker (plutot qu'un cast "(gdt_entry_t *)0x800") evite que GCC
 * prenne cette petite adresse pour un pointeur NULL et refuse de compiler en -O2
 * (-Warray-bounds + -Werror).
 */

extern gdt_entry_t gdt_location[];

static const char *const gdt_names[GDT_ENTRIES] = {
    "Null",
    "Kernel Code",
    "Kernel Data",
    "Kernel Stack",
    "User Code",
    "User Data",
    "User Stack",
};

/**
 * gdt_set_entry - Remplit un descripteur de segment
 *
 * Le format d'un descripteur est historique (heritage du 80286) : la base et la
 * limite sont "eparpillees" dans les 8 octets. On les decoupe donc en morceaux.
 */
static void gdt_set_entry(size_t index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags)
{
    gdt_entry_t *entry = &gdt_location[index];

    entry->base_low    = (uint16_t)(base & 0xFFFF);
    entry->base_middle = (uint8_t)((base >> 16) & 0xFF);
    entry->base_high   = (uint8_t)((base >> 24) & 0xFF);

    entry->limit_low   = (uint16_t)(limit & 0xFFFF);
    entry->granularity = (uint8_t)(((limit >> 16) & 0x0F) | ((flags & 0x0F) << 4));

    entry->access      = access;
}

/**
 * gdt_init - Construit la GDT a 0x800 puis la declare au CPU (lgdt)
 *
 * Modele "flat" : tous les segments couvrent les 4 Go (base 0, limite
 * 0xFFFFF pages de 4 Ko). Ce qui les distingue, ce sont leurs droits :
 *   - Code  : executable + lisible
 *   - Data  : lisible + inscriptible
 *   - Stack : lisible + inscriptible (charge dans SS)
 *   - Kernel = ring 0 (DPL 0), User = ring 3 (DPL 3)
 *
 * Les segments de pile sont de type "donnees expand-up" et non "expand-down" :
 * avec un modele flat, un segment expand-down de base 0 empecherait d'acceder
 * aux adresses basses, et le C suppose que &variable_locale est utilisable via
 * DS comme via SS. C'est le choix fait par Linux et la plupart des noyaux.
 */
void gdt_init(void)
{
    gdt_ptr_t gdtr;

    /* 0 : Descripteur nul obligatoire (le CPU interdit d'utiliser le selecteur 0) */
    gdt_set_entry(GDT_NULL_IDX, 0, 0, 0, 0);

    /* 1-3 : Segments noyau (ring 0) */
    gdt_set_entry(GDT_KERNEL_CODE_IDX,  0, 0xFFFFF, GDT_KERNEL_CODE_ACCESS, GDT_FLAGS_32BIT_4K);
    gdt_set_entry(GDT_KERNEL_DATA_IDX,  0, 0xFFFFF, GDT_KERNEL_DATA_ACCESS, GDT_FLAGS_32BIT_4K);
    gdt_set_entry(GDT_KERNEL_STACK_IDX, 0, 0xFFFFF, GDT_KERNEL_DATA_ACCESS, GDT_FLAGS_32BIT_4K);

    /* 4-6 : Segments utilisateur (ring 3) */
    gdt_set_entry(GDT_USER_CODE_IDX,    0, 0xFFFFF, GDT_USER_CODE_ACCESS,   GDT_FLAGS_32BIT_4K);
    gdt_set_entry(GDT_USER_DATA_IDX,    0, 0xFFFFF, GDT_USER_DATA_ACCESS,   GDT_FLAGS_32BIT_4K);
    gdt_set_entry(GDT_USER_STACK_IDX,   0, 0xFFFFF, GDT_USER_DATA_ACCESS,   GDT_FLAGS_32BIT_4K);

    /* Pointeur passe a 'lgdt' : taille - 1 et adresse de la table */
    gdtr.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdtr.base  = (uint32_t)gdt_location;

    /* lgdt + rechargement de CS, DS, ES, FS, GS (Kernel Data) et SS (Kernel Stack) */
    gdt_flush(&gdtr);
}

/**
 * gdt_selector_name - Renvoie le nom lisible du segment designe par un selecteur
 */
const char *gdt_selector_name(uint16_t selector)
{
    uint16_t index = selector >> 3;

    if (selector & 0x4)
        return "LDT ?";
    if (index >= GDT_ENTRIES)
        return "Invalid";
    return gdt_names[index];
}

/**
 * gdt_print - Affiche la GDT reellement chargee dans le CPU, de facon lisible
 *
 * On relit le registre GDTR avec 'sgdt' : on affiche donc ce que le processeur
 * utilise vraiment, pas seulement ce que l'on croit y avoir ecrit.
 */
void gdt_print(void)
{
    gdt_ptr_t gdtr;

    __asm__ __volatile__("sgdt %0" : "=m"(gdtr));

    const gdt_entry_t *table = (const gdt_entry_t *)gdtr.base;
    size_t count = ((size_t)gdtr.limit + 1) / sizeof(gdt_entry_t);

    vga_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    printk("GDTR: base = 0x%08x  limit = 0x%04x  (%u entries)\n",
           gdtr.base, gdtr.limit, count);

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk(" #  Sel   %-13s Base        Limit       Acc  Flg  DPL Type\n", "Name");

    for (size_t i = 0; i < count; i++) {
        const gdt_entry_t *e = &table[i];

        uint32_t base  = (uint32_t)e->base_low
                       | ((uint32_t)e->base_middle << 16)
                       | ((uint32_t)e->base_high << 24);
        uint32_t limit = (uint32_t)e->limit_low | ((uint32_t)(e->granularity & 0x0F) << 16);
        uint8_t  flags = (uint8_t)(e->granularity >> 4);
        uint8_t  dpl   = (uint8_t)((e->access >> 5) & 0x3);

        /* Granularite 4 Ko : la limite reelle en octets = (limite << 12) | 0xFFF */
        if (flags & 0x8)
            limit = (limit << 12) | 0xFFF;

        const char *type;
        if (!(e->access & GDT_ACCESS_PRESENT))
            type = "-";
        else if (e->access & GDT_ACCESS_EXEC)
            type = (e->access & GDT_ACCESS_RW) ? "code exec/read" : "code exec";
        else
            type = (e->access & GDT_ACCESS_RW) ? "data read/write" : "data read";

        if (i == 0)
            vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        else if (dpl == 0)
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else
            vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

        printk(" %u  0x%02x  %-13s 0x%08x  0x%08x  0x%02x 0x%x  %u   %s\n",
               i, i * 8 + dpl, (i < GDT_ENTRIES) ? gdt_names[i] : "?",
               base, limit, e->access, flags, dpl, type);
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
