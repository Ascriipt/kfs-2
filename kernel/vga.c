#include "string.h"
#include "printk.h"
#include "vga.h"
#include "io.h"

/* ==============================================================================
 * kernel/vga.c - Driver VGA Texte avec support Multi-Terminaux (TTYs)
 * ==============================================================================
 */

#define VGA_CRTC_INDEX_PORT 0x3D4
#define VGA_CRTC_DATA_PORT  0x3D5
#define VGA_CRTC_CURSOR_HI  0x0E
#define VGA_CRTC_CURSOR_LO  0x0F

typedef struct {
    uint16_t buffer[VGA_HEIGHT * VGA_WIDTH];
    size_t   row;
    size_t   column;
    uint8_t  color;
} tty_screen_t;

static tty_screen_t ttys[VGA_NUM_TTYS];
static size_t current_tty = 0;

/**
 * vga_set_cursor - Met a jour la position materielle du curseur clignotant
 *
 * Calcule l'indice 1D (y * 80 + x) et programme les registres internes 0x0E (poids fort)
 * et 0x0F (poids faible) du controleur CRT via les ports I/O 0x3D4 et 0x3D5.
 */
void vga_set_cursor(size_t x, size_t y)
{
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);

    outb(VGA_CRTC_INDEX_PORT, VGA_CRTC_CURSOR_HI);
    outb(VGA_CRTC_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));

    outb(VGA_CRTC_INDEX_PORT, VGA_CRTC_CURSOR_LO);
    outb(VGA_CRTC_DATA_PORT, (uint8_t)(pos & 0xFF));
}

/**
 * vga_scroll - Fait defiler l'ecran du terminal actif d'une ligne vers le haut
 *
 * Decale les lignes 1 a 24 vers le haut (la ligne 0 disparait), nettoie la
 * toute derniere ligne avec des espaces de la couleur courante, puis synchronise
 * le tout avec la memoire video reelle (0xB8000).
 */
void vga_scroll(void)
{
    tty_screen_t *tty = &ttys[current_tty];

    /* 1. Deplace chaque ligne vers le haut */
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            tty->buffer[(y - 1) * VGA_WIDTH + x] = tty->buffer[y * VGA_WIDTH + x];
        }
    }

    /* 2. Efface la derniere ligne */
    uint16_t blank = vga_entry(' ', tty->color);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        tty->buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    /* 3. Met a jour la memoire video physique */
    memcpy((void *)VGA_MEMORY, tty->buffer, sizeof(tty->buffer));
}

/**
 * vga_set_color - Change la couleur active pour le terminal courant
 *
 * Combine les deux couleurs (texte et fond) en un octet d'attribut VGA
 * et l'applique au TTY actif.
 */
void vga_set_color(vga_color_t fg, vga_color_t bg)
{
    ttys[current_tty].color = vga_entry_color(fg, bg);
}

/**
 * vga_clear - Nettoie integralement l'ecran du terminal actif
 *
 * Remplit toutes les cellules de l'ecran avec des espaces de la couleur active,
 * remet les coordonnees du curseur a (0, 0) et met a jour la memoire video.
 */
void vga_clear(void)
{
    tty_screen_t *tty = &ttys[current_tty];
    uint16_t blank = vga_entry(' ', tty->color);

    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        tty->buffer[i] = blank;
    }

    tty->row = 0;
    tty->column = 0;

    memcpy((void *)VGA_MEMORY, tty->buffer, sizeof(tty->buffer));
    vga_set_cursor(tty->column, tty->row);
}

/**
 * print_prompt - Affiche l'invite de commande (prompt) pour le shell interactif
 *
 * Affiche 'kfs2@42:~$ ' en cyan clair puis retablit la couleur de texte en blanc.
 */
void print_prompt(void)
{
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("kfs2@42:~$ ");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * vga_init - Initialise le sous-systeme VGA et les terminaux virtuels
 *
 * Initialise les structures et tampons de tous les TTYs (blanc sur fond noir),
 * definit le TTY 0 comme terminal par defaut et nettoie l'ecran.
 */
void vga_init(void)
{
    uint8_t default_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint16_t blank = vga_entry(' ', default_color);

    for (size_t t = 0; t < VGA_NUM_TTYS; t++) {
        ttys[t].row = 0;
        ttys[t].column = 0;
        ttys[t].color = default_color;
        for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
            ttys[t].buffer[i] = blank;
        }
    }

    current_tty = 0;
    vga_clear();
}

/**
 * vga_putchar - Affiche un caractere sur le terminal actif
 *
 * Gere les caracteres speciaux :
 * - '\n' : passage a la ligne suivante
 * - '\r' : retour en debut de ligne
 * - '\t' : tabulation avancee au multiple de 8 colonnes suivant
 * - '\b' : retour arriere (efface le caractere precedent, remonte d'une
 *          ligne si on est en debut de ligne : utile quand une commande du
 *          shell depasse 80 caracteres)
 * Gere egalement le retour automatique a la ligne (wrap), le declenchement du
 * scrolling si la derniere ligne est franchie, et synchronise le curseur materiel.
 */
void vga_putchar(char c)
{
    tty_screen_t *tty = &ttys[current_tty];

    if (c == '\n') {
        tty->column = 0;
        tty->row++;
    } else if (c == '\r') {
        tty->column = 0;
    } else if (c == '\t') {
        tty->column = (tty->column + 8) & ~(8 - 1);
        if (tty->column >= VGA_WIDTH) {
            tty->column = 0;
            tty->row++;
        }
    } else if (c == '\b') {
        if (tty->column > 0 || tty->row > 0) {
            if (tty->column > 0) {
                tty->column--;
            } else {
                tty->row--;
                tty->column = VGA_WIDTH - 1;
            }
            size_t idx = tty->row * VGA_WIDTH + tty->column;
            tty->buffer[idx] = vga_entry(' ', tty->color);
            VGA_MEMORY[idx] = tty->buffer[idx];
        }
    } else {
        size_t idx = tty->row * VGA_WIDTH + tty->column;
        tty->buffer[idx] = vga_entry((unsigned char)c, tty->color);
        VGA_MEMORY[idx] = tty->buffer[idx];

        tty->column++;
        if (tty->column >= VGA_WIDTH) {
            tty->column = 0;
            tty->row++;
        }
    }

    if (tty->row >= VGA_HEIGHT) {
        vga_scroll();
        tty->row = VGA_HEIGHT - 1;
    }

    vga_set_cursor(tty->column, tty->row);
}

/**
 * vga_puts - Affiche une chaine de caracteres sur le terminal actif
 *
 * Parcourt la chaine et envoie chaque caractere a vga_putchar().
 */
void vga_puts(const char *str)
{
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i]);
    }
}

/**
 * vga_get_current_tty - Renvoie l'identifiant du terminal virtuel actif
 *
 * Retourne l'index du TTY actuellement affiche (de 0 a VGA_NUM_TTYS - 1).
 */
size_t vga_get_current_tty(void)
{
    return current_tty;
}

/**
 * vga_switch_tty - Bascule l'affichage vers un autre terminal virtuel
 *
 * Copie l'integralite du tampon du terminal cible dans la memoire video physique
 * 0xB8000 pour affichage immediat, et met a jour la position du curseur materiel.
 */
void vga_switch_tty(size_t tty_index)
{
    if (tty_index >= VGA_NUM_TTYS || tty_index == current_tty) {
        return;
    }

    current_tty = tty_index;
    tty_screen_t *tty = &ttys[current_tty];

    /* Recopie le buffer du terminal actif dans la memoire video physique 0xB8000 */
    memcpy((void *)VGA_MEMORY, tty->buffer, sizeof(tty->buffer));

    /* Met a jour le curseur physique a la position du terminal actif */
    vga_set_cursor(tty->column, tty->row);
}
