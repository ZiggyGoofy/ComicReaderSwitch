#include "archive.h"

#include <archive.h>
#include <archive_entry.h>

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

// Retourne true si le nom de fichier a une extension d'image qu'on sait décoder
// (SDL2_image, avec les libs installées, gère bien jpg/jpeg/png).
static bool has_image_extension(const char *name) {
    size_t len = strlen(name);
    const char *exts[] = { ".jpg", ".jpeg", ".png" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strcasecmp(name + len - elen, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int page_cmp(const void *a, const void *b) {
    const ArPageEntry *ea = (const ArPageEntry *)a;
    const ArPageEntry *eb = (const ArPageEntry *)b;
    return strcasecmp(ea->name, eb->name);
}

bool ar_open(ComicArchive *ar, const char *path) {
    memset(ar, 0, sizeof(ComicArchive));
    strncpy(ar->archive_path, path, sizeof(ar->archive_path) - 1);

    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, path, 10240) != ARCHIVE_OK) {
        printf("archive_read_open_filename a échoué pour %s: %s\n",
               path, archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        if (name && has_image_extension(name) && ar->page_count < AR_MAX_PAGES) {
            strncpy(ar->pages[ar->page_count].name, name, AR_MAX_NAME - 1);
            ar->page_count++;
        }
        // On ne lit pas le contenu ici, juste l'en-tête : on saute les données.
        archive_read_data_skip(a);
    }

    archive_read_free(a);

    if (ar->page_count == 0) {
        printf("Aucune image trouvée dans %s\n", path);
        return false;
    }

    qsort(ar->pages, ar->page_count, sizeof(ArPageEntry), page_cmp);
    ar->current_page = 0;
    return true;
}

void ar_close(ComicArchive *ar) {
    if (ar->_handle) {
        archive_read_free((struct archive *)ar->_handle);
    }
    memset(ar, 0, sizeof(ComicArchive));
}

bool ar_extract_page(ComicArchive *ar, int index, void **out_data, size_t *out_size) {
    if (index < 0 || index >= ar->page_count) return false;

    // Si on n'a pas de handle ouvert, ou si la page demandée est déjà "derrière"
    // (on doit reculer), on (re)ouvre l'archive depuis le début. Sinon, on
    // continue avec le handle existant : c'est le cas le plus fréquent (tourner
    // les pages dans l'ordre), qui évite de re-parcourir tout ce qui précède
    // à chaque appel — important pour les grosses archives à beaucoup de pages.
    if (!ar->_handle || index < ar->_handle_pos) {
        if (ar->_handle) {
            archive_read_free((struct archive *)ar->_handle);
            ar->_handle = NULL;
        }

        struct archive *a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);

        if (archive_read_open_filename(a, ar->archive_path, 10240) != ARCHIVE_OK) {
            printf("Réouverture de l'archive échouée: %s\n", archive_error_string(a));
            archive_read_free(a);
            return false;
        }

        ar->_handle = a;
        ar->_handle_pos = 0;
    }

    struct archive *a = (struct archive *)ar->_handle;
    struct archive_entry *entry = NULL;
    bool found = false;

    while (ar->_handle_pos <= index) {
        if (archive_read_next_header(a, &entry) != ARCHIVE_OK) {
            break; // fin d'archive ou erreur inattendue
        }
        if (ar->_handle_pos == index) {
            found = true;
            break; // c'est la page voulue : on ne saute pas son contenu
        }
        archive_read_data_skip(a);
        ar->_handle_pos++;
    }

    if (!found) {
        printf("Page introuvable en avançant dans l'archive (index %d)\n", index);
        // État incertain : on referme pour forcer une réouverture propre au
        // prochain appel plutôt que de risquer une lecture incohérente.
        archive_read_free(a);
        ar->_handle = NULL;
        ar->_handle_pos = 0;
        return false;
    }

    la_int64_t declared_size = archive_entry_size(entry);
    size_t capacity = declared_size > 0 ? (size_t)declared_size : (1024 * 1024);
    size_t used = 0;
    unsigned char *buffer = malloc(capacity);
    if (!buffer) return false;

    for (;;) {
        if (used + 65536 > capacity) {
            capacity *= 2;
            unsigned char *grown = realloc(buffer, capacity);
            if (!grown) {
                free(buffer);
                return false;
            }
            buffer = grown;
        }

        ssize_t n = archive_read_data(a, buffer + used, capacity - used);
        if (n < 0) {
            printf("Erreur de lecture de la page: %s\n", archive_error_string(a));
            free(buffer);
            // Handle potentiellement corrompu après une erreur de lecture en
            // plein milieu : on le referme pour repartir propre ensuite.
            archive_read_free(a);
            ar->_handle = NULL;
            ar->_handle_pos = 0;
            return false;
        }
        if (n == 0) break; // fin de l'entrée
        used += (size_t)n;
    }

    ar->_handle_pos = index + 1; // le prochain appel séquentiel continue juste après

    *out_data = buffer;
    *out_size = used;
    return true;
}

void ar_next_page(ComicArchive *ar) {
    if (ar->current_page < ar->page_count - 1) ar->current_page++;
}

void ar_prev_page(ComicArchive *ar) {
    if (ar->current_page > 0) ar->current_page--;
}
