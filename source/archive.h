#pragma once

#include <stdbool.h>
#include <stddef.h>

#define AR_MAX_PAGES 1024
#define AR_MAX_NAME  256

typedef struct {
    char name[AR_MAX_NAME]; // chemin de l'entrée à l'intérieur de l'archive
    int stream_index;       // position réelle de cette entrée dans le flux brut
                             // de l'archive (avant tri alphabétique) — nécessaire
                             // car l'ordre interne d'une archive ne correspond pas
                             // toujours à l'ordre alphabétique des noms.
} ArPageEntry;

typedef struct {
    char archive_path[512];
    ArPageEntry pages[AR_MAX_PAGES];
    int page_count;
    int current_page;

    // Handle d'archive gardé ouvert entre deux appels à ar_extract_page, pour
    // une lecture séquentielle rapide (cas le plus courant : tourner les pages
    // dans l'ordre). Opaque ici (void*) pour ne pas exposer libarchive dans ce
    // header ; voir archive.c pour le cast réel vers struct archive*.
    void *_handle;
    int _handle_pos; // index (0-based) du prochain entry lisible séquentiellement via ce handle
} ComicArchive;

// Ouvre l'archive (cbz ou cbr, détecté automatiquement par libarchive) et
// liste les pages (fichiers image) triées par nom. Ne charge aucune image
// en mémoire à ce stade.
bool ar_open(ComicArchive *ar, const char *path);

// Remet la structure à zéro (pas d'allocation dynamique dans cette version).
void ar_close(ComicArchive *ar);

// Extrait la page d'index `index` en mémoire (alloue via malloc dans *out_data).
// L'appelant est responsable de free(*out_data) après usage.
// Retourne false en cas d'échec (index invalide, erreur de lecture...).
bool ar_extract_page(ComicArchive *ar, int index, void **out_data, size_t *out_size);

void ar_next_page(ComicArchive *ar);
void ar_prev_page(ComicArchive *ar);
