#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

/* ==============================================================================
 * include/multiboot.h - Structures Multiboot 1 et ELF utiles au noyau
 * ==============================================================================
 * Quand GRUB lance le noyau, EBX pointe sur une structure "multiboot_info"
 * qui decrit la machine (memoire, ligne de commande...). Pour un noyau ELF,
 * GRUB y ajoute aussi l'emplacement des en-tetes de sections ELF : c'est ce qui
 * nous permet de retrouver la table des symboles (noms des fonctions) afin
 * d'afficher une pile d'appels lisible.
 */

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_ELF_SHDR    0x00000020   /* bit 5 de 'flags' */

typedef struct __attribute__((packed)) {
    uint32_t num;       /* nombre d'en-tetes de section     */
    uint32_t size;      /* taille d'un en-tete              */
    uint32_t addr;      /* adresse du tableau d'en-tetes    */
    uint32_t shndx;     /* index de la table des noms       */
} multiboot_elf_section_t;

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    multiboot_elf_section_t elf_sec;   /* valide si flags & MULTIBOOT_INFO_ELF_SHDR */
    /* ... autres champs non utilises ... */
} multiboot_info_t;

/* --- Format ELF32 (juste ce dont on a besoin) --- */

#define SHT_SYMTAB 2

typedef struct __attribute__((packed)) {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} elf32_shdr_t;

#define STT_NOTYPE 0
#define STT_FUNC   2
#define STB_LOCAL  0
#define ELF32_ST_TYPE(info) ((info) & 0x0F)
#define ELF32_ST_BIND(info) ((info) >> 4)

typedef struct __attribute__((packed)) {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} elf32_sym_t;

#endif /* MULTIBOOT_H */
