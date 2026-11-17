; ==============================================================================
; boot/gdt_flush.s - Chargement de la GDT dans le CPU et rechargement des segments
; ==============================================================================
; void gdt_flush(const gdt_ptr_t *ptr);
;
; 'lgdt' indique au processeur ou se trouve la nouvelle table. Mais les registres
; de segment gardent en cache les anciens descripteurs (ceux de GRUB) : il faut
; donc les recharger un par un pour que la nouvelle GDT soit reellement utilisee.

; Selecteurs (doivent correspondre a include/gdt.h)
KERNEL_CODE_SEL  equ 0x08
KERNEL_DATA_SEL  equ 0x10
KERNEL_STACK_SEL equ 0x18

section .text
global gdt_flush:function

gdt_flush:
    mov eax, [esp + 4]          ; 1er argument : adresse de la structure gdt_ptr_t
    lgdt [eax]                  ; Declare la GDT au CPU (registre GDTR)

    ; Segments de donnees : DS, ES, FS, GS -> Kernel Data
    mov ax, KERNEL_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Segment de pile : SS -> Kernel Stack
    ; La base reste 0 (modele flat) donc ESP reste valide tel quel.
    mov ax, KERNEL_STACK_SEL
    mov ss, ax

    ; CS ne peut pas etre modifie avec 'mov' : on utilise un saut lointain
    ; (far jump) qui charge a la fois CS et EIP.
    jmp KERNEL_CODE_SEL:.reload_cs

.reload_cs:
    ret

; Indique au linker que ce code n'a pas besoin d'une pile executable
; (supprime le warning "missing .note.GNU-stack section")
section .note.GNU-stack noalloc noexec nowrite progbits
