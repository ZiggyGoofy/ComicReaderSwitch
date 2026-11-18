#include "progress.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#define PROGRESS_DIR "/switch/ComicReaderSwitch/progress"
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

// --- Cache mémoire de la progression -----------------------------------
// Évite de rouvrir le fichier de progression de chaque chapitre à chaque
// rafraîchissement (liste de chapitres, compteurs) et d'en réécrire un à
// chaque image en défilement quand la page n'a pas changé.
#define PC_SIZE 4096
#define PC_PROBES 16
typedef struct {
    uint64_t key;
    int page, total;
    unsigned char state; // 0 = vide, 1 = progression connue, 2 = pas de progression
} PCEntry;
static PCEntry g_pc[PC_SIZE];

static uint64_t pc_hash(const char *path) {
    uint64_t h = 1469598103934665603ULL;
    for (; *path; path++) {
        h ^= (unsigned char)*path;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

// Retourne l'entrée du cache pour cette clé (en la créant, vide, si absente).
static PCEntry *pc_slot(uint64_t key) {
    size_t start = (size_t)(key % PC_SIZE);
    for (int i = 0; i < PC_PROBES; i++) {
        PCEntry *e = &g_pc[(start + i) % PC_SIZE];
        if (e->state != 0 && e->key == key) return e;
        if (e->state == 0) { e->key = key; return e; }
    }
    // Zone saturée : on écrase la première case (le fichier reste la source de vérité).
    PCEntry *e = &g_pc[start];
    e->key = key;
    e->state = 0;
    return e;
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
    PCEntry *cached = pc_slot(pc_hash(comic_path));
    if (cached->state == 1) {
        *out_page = cached->page;
        *out_total = cached->total;
        return true;
    }
    if (cached->state == 2) return false;

    char file_path[PROGRESS_PATH_MAX];
    if (!build_progress_path(comic_path, file_path, sizeof(file_path))) return false;

    FILE *f = fopen(file_path, "r");
    if (!f) { cached->state = 2; return false; }

    int page = -1, total = -1;
    // Format simple, une valeur par ligne : "page=N" puis "total=N".
    if (fscanf(f, "page=%d\ntotal=%d\n", &page, &total) != 2) {
        fclose(f);
        cached->state = 2;
        return false;
    }
    fclose(f);

    if (page < 0 || total < 0) { cached->state = 2; return false; }

    cached->page = page;
    cached->total = total;
    cached->state = 1;
    *out_page = page;
    *out_total = total;
    return true;
}

bool progress_save(const char *comic_path, int page, int total) {
    // Rien à écrire si la valeur enregistrée est déjà la bonne (cas typique
    // du défilement continu, qui sauvegarde à chaque image).
    PCEntry *cached = pc_slot(pc_hash(comic_path));
    if (cached->state == 1 && cached->page == page && cached->total == total) return true;

    static bool dir_ready = false;
    if (!dir_ready) {
        ensure_progress_dir();
        dir_ready = true;
    }

    char file_path[PROGRESS_PATH_MAX];
    if (!build_progress_path(comic_path, file_path, sizeof(file_path))) return false;

    FILE *f = fopen(file_path, "w");
    if (!f) {
        printf("Impossible d'écrire la progression dans %s\n", file_path);
        return false;
    }

    fprintf(f, "page=%d\ntotal=%d\n", page, total);
    fclose(f);

    cached->page = page;
    cached->total = total;
    cached->state = 1;
    return true;
}

bool progress_clear(const char *comic_path) {
    char file_path[PROGRESS_PATH_MAX];
    if (!build_progress_path(comic_path, file_path, sizeof(file_path))) return false;
    PCEntry *cached = pc_slot(pc_hash(comic_path));
    cached->state = 2;
    return remove(file_path) == 0;
}
