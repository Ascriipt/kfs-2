#ifndef GDT_H
#define GDT_H

#include "types.h"

/* ==============================================================================
 * include/gdt.h - Global Descriptor Table (GDT)
 * ==============================================================================
 * La GDT est un tableau de "descripteurs de segment" (8 octets chacun) que le
 * CPU consulte a chaque fois qu'un registre de segment (CS, DS, SS, ...) est
 * charge. Chaque descripteur decrit une zone memoire : adresse de base, taille
 * (limite), droits (code/donnees, lecture/ecriture) et niveau de privilege
 * (ring 0 = noyau, ring 3 = utilisateur).
 *
 * Le sujet KFS_2 impose :
 *   - 7 entrees : Null + Kernel Code/Data/Stack + User Code/Data/Stack
 *   - la table placee a l'adresse physique 0x00000800
 *     (le symbole gdt_location est defini dans linker.ld)
 */

#define GDT_ADDRESS        0x00000800
#define GDT_ENTRIES        7

/* Index des descripteurs dans la table */
#define GDT_NULL_IDX         0
#define GDT_KERNEL_CODE_IDX  1
#define GDT_KERNEL_DATA_IDX  2
#define GDT_KERNEL_STACK_IDX 3
#define GDT_USER_CODE_IDX    4
#define GDT_USER_DATA_IDX    5
#define GDT_USER_STACK_IDX   6

/*
 * Selecteur = (index << 3) | TI | RPL
 *   - bits 0-1 : RPL (Requested Privilege Level), 0 pour le noyau, 3 pour l'utilisateur
 *   - bit 2    : TI (0 = GDT, 1 = LDT)
 *   - bits 3+  : index du descripteur dans la table
 * Ces valeurs sont dupliquees dans boot/gdt_flush.s (NASM ne lit pas les .h).
 */
#define GDT_KERNEL_CODE_SEL  0x08
#define GDT_KERNEL_DATA_SEL  0x10
#define GDT_KERNEL_STACK_SEL 0x18
#define GDT_USER_CODE_SEL    0x23   /* 0x20 | RPL 3 */
#define GDT_USER_DATA_SEL    0x2B   /* 0x28 | RPL 3 */
#define GDT_USER_STACK_SEL   0x33   /* 0x30 | RPL 3 */

/*
 * Octet "Access" d'un descripteur :
 *   bit 7   : P   (Present)               -> 1 = segment valide
 *   bit 6-5 : DPL (Descriptor Priv. Level) -> 0 = noyau, 3 = utilisateur
 *   bit 4   : S   (Type)                  -> 1 = segment de code/donnees
 *   bit 3   : E   (Executable)            -> 1 = code, 0 = donnees
 *   bit 2   : DC  (Direction/Conforming)  -> 0
 *   bit 1   : RW  (Readable code / Writable data)
 *   bit 0   : A   (Accessed, mis a 1 par le CPU)
 */
#define GDT_ACCESS_PRESENT   0x80
#define GDT_ACCESS_RING0     0x00
#define GDT_ACCESS_RING3     0x60
#define GDT_ACCESS_CODE_DATA 0x10
#define GDT_ACCESS_EXEC      0x08
#define GDT_ACCESS_DC        0x04
#define GDT_ACCESS_RW        0x02

#define GDT_KERNEL_CODE_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | GDT_ACCESS_RW) /* 0x9A */
#define GDT_KERNEL_DATA_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)                   /* 0x92 */
#define GDT_USER_CODE_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | GDT_ACCESS_RW) /* 0xFA */
#define GDT_USER_DATA_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)                   /* 0xF2 */

/*
 * Flags (4 bits de poids fort de l'octet "granularity") :
 *   bit 3 : G  (Granularity) -> 1 = la limite est comptee en pages de 4 Ko
 *   bit 2 : DB (Size)        -> 1 = segment 32 bits
 *   bit 1 : L  (Long mode)   -> 0 (pas de 64 bits)
 */
#define GDT_FLAGS_32BIT_4K   0xC

/* Descripteur de segment (8 octets, format impose par le CPU) */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;     /* limite bits 0-15  */
    uint16_t base_low;      /* base   bits 0-15  */
    uint8_t  base_middle;   /* base   bits 16-23 */
    uint8_t  access;        /* octet d'acces     */
    uint8_t  granularity;   /* flags (4 bits hauts) | limite bits 16-19 (4 bits bas) */
    uint8_t  base_high;     /* base   bits 24-31 */
} gdt_entry_t;

/* Structure lue par l'instruction 'lgdt' (6 octets) */
typedef struct __attribute__((packed)) {
    uint16_t limit;         /* taille de la table en octets - 1 */
    uint32_t base;          /* adresse lineaire de la table      */
} gdt_ptr_t;

/* Implementee en assembleur (boot/gdt_flush.s) */
void        gdt_flush(const gdt_ptr_t *ptr);

void        gdt_init(void);
void        gdt_print(void);
const char *gdt_selector_name(uint16_t selector);

#endif /* GDT_H */
