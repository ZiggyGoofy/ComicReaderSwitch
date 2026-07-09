#include "progress.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define PROGRESS_DIR "/switch/comic-reader/progress"
#define PROGRESS_PATH_MAX 768

// Transforme un chemin complet ("/switch/.../comics/Truc.cbz") en un nom de
// fichier "plat" utilisable dans PROGRESS_DIR, en remplaçant les '/' par '_'.
// Évite d'avoir à recréer une arborescence de dossiers miroir.
static void sanitize_to_filename(const char *path, char *out, size_t out_size) {
    size_t i = 0;
    for (; path[i] != '\0' && i < out_size - 1; i++) {
        out[i] = (path[i] == '/') ? '_' : path[i];
    }
    out[i] = '\0';
}

static bool build_progress_path(const char *comic_path, char *out, size_t out_size) {
    char sanitized[PROGRESS_PATH_MAX];
    sanitize_to_filename(comic_path, sanitized, sizeof(sanitized));

    int written = snprintf(out, out_size, "%s/%s.txt", PROGRESS_DIR, sanitized);
    return written > 0 && (size_t)written < out_size;
}

// S'assure que le dossier de progression existe (ignore l'erreur si déjà présent).
static void ensure_progress_dir(void) {
    if (mkdir(PROGRESS_DIR, 0777) != 0 && errno != EEXIST) {
        printf("Impossible de créer %s (errno=%d)\n", PROGRESS_DIR, errno);
    }
}

bool progress_load(const char *comic_path, int *out_page, int *out_total) {
    char file_path[PROGRESS_PATH_MAX];
    if (!build_progress_path(comic_path, file_path, sizeof(file_path))) return false;

    FILE *f = fopen(file_path, "r");
    if (!f) return false;

    int page = -1, total = -1;
    // Format simple, une valeur par ligne : "page=N" puis "total=N".
    if (fscanf(f, "page=%d\ntotal=%d\n", &page, &total) != 2) {
        fclose(f);
        return false;
    }
    fclose(f);

    if (page < 0 || total < 0) return false;

    *out_page = page;
    *out_total = total;
    return true;
}

bool progress_save(const char *comic_path, int page, int total) {
    ensure_progress_dir();

    char file_path[PROGRESS_PATH_MAX];
    if (!build_progress_path(comic_path, file_path, sizeof(file_path))) return false;

    FILE *f = fopen(file_path, "w");
    if (!f) {
        printf("Impossible d'écrire la progression dans %s\n", file_path);
        return false;
    }

    fprintf(f, "page=%d\ntotal=%d\n", page, total);
    fclose(f);
    return true;
}
