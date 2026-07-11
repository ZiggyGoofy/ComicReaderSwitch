#include "file_browser.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdio.h>
#include <stdlib.h>

// Retourne true si le nom de fichier se termine par .cbz ou .cbr (insensible à la casse).
static bool has_comic_extension(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return false;
    const char *ext = name + len - 4;
    return strcasecmp(ext, ".cbz") == 0 || strcasecmp(ext, ".cbr") == 0;
}

// Comparateur pour qsort : dossiers d'abord, puis ordre alphabétique.
static int entry_cmp(const void *a, const void *b) {
    const FBEntry *ea = (const FBEntry *)a;
    const FBEntry *eb = (const FBEntry *)b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    return strcasecmp(ea->name, eb->name);
}

bool fb_scan(FileBrowser *fb) {
    DIR *dir = opendir(fb->base_path);
    if (!dir) {
        fb->entry_count = 0;
        return false;
    }

    fb->entry_count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        // On garde ".." uniquement pour remonter, mais on gère la remontée
        // séparément via fb_go_parent, donc on l'ignore ici pour éviter les doublons.
        if (strcmp(ent->d_name, "..") == 0) continue;

        bool is_dir = (ent->d_type == DT_DIR);

        if (!is_dir && !has_comic_extension(ent->d_name)) {
            continue; // on ignore les fichiers qui ne sont pas des comics
        }

        if (fb->entry_count >= FB_MAX_ENTRIES) break;

        FBEntry *e = &fb->entries[fb->entry_count];
        strncpy(e->name, ent->d_name, FB_MAX_NAME - 1);
        e->name[FB_MAX_NAME - 1] = '\0';
        e->is_dir = is_dir;
        fb->entry_count++;
    }
    closedir(dir);

    qsort(fb->entries, fb->entry_count, sizeof(FBEntry), entry_cmp);

    fb->selected = 0;
    return true;
}

bool fb_init(FileBrowser *fb, const char *start_path) {
    memset(fb, 0, sizeof(FileBrowser));
    strncpy(fb->base_path, start_path, FB_MAX_PATH - 1);
    fb->base_path[FB_MAX_PATH - 1] = '\0';
    strncpy(fb->root_path, start_path, FB_MAX_PATH - 1);
    fb->root_path[FB_MAX_PATH - 1] = '\0';
    return fb_scan(fb);
}

void fb_move_selection(FileBrowser *fb, int delta) {
    if (fb->entry_count == 0) return;
    fb->selected += delta;
    if (fb->selected < 0) fb->selected = 0;
    if (fb->selected >= fb->entry_count) fb->selected = fb->entry_count - 1;
}

bool fb_selected_is_dir(const FileBrowser *fb) {
    if (fb->entry_count == 0) return false;
    return fb->entries[fb->selected].is_dir;
}

bool fb_enter_selected(FileBrowser *fb) {
    if (fb->entry_count == 0) return false;
    if (!fb->entries[fb->selected].is_dir) return false;

    char new_path[FB_MAX_PATH];
    int written = snprintf(new_path, FB_MAX_PATH, "%s/%s",
                            fb->base_path, fb->entries[fb->selected].name);
    if (written < 0 || written >= FB_MAX_PATH) return false;

    strncpy(fb->base_path, new_path, FB_MAX_PATH - 1);
    fb->base_path[FB_MAX_PATH - 1] = '\0';
    return fb_scan(fb);
}

bool fb_go_parent(FileBrowser *fb) {
    // On ne remonte jamais au-delà de la racine autorisée (le dossier "comics"),
    // pour ne pas exposer les dossiers voisins internes à l'appli (progress/, thumbnails/)
    // ni le reste de la carte SD.
    if (strcmp(fb->base_path, fb->root_path) == 0) {
        return false;
    }

    char *last_slash = strrchr(fb->base_path, '/');
    if (!last_slash || last_slash == fb->base_path) {
        return false; // déjà à la racine du système de fichiers
    }
    *last_slash = '\0';
    return fb_scan(fb);
}

bool fb_get_selected_path(const FileBrowser *fb, char *out, size_t outsize) {
    if (fb->entry_count == 0) return false;
    int written = snprintf(out, outsize, "%s/%s",
                            fb->base_path, fb->entries[fb->selected].name);
    return written > 0 && (size_t)written < outsize;
}

static int name_cmp_str(const void *a, const void *b) {
    return strcasecmp(*(const char **)a, *(const char **)b);
}

bool fb_find_representative_comic(const char *dir_path, char *out, size_t outsize, int depth_limit) {
    if (depth_limit <= 0) return false;

    DIR *dir = opendir(dir_path);
    if (!dir) return false;

    char *comic_names[FB_MAX_ENTRIES];
    int comic_count = 0;
    char *sub_dir_names[FB_MAX_ENTRIES];
    int sub_dir_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        if (ent->d_type == DT_DIR) {
            if (sub_dir_count < FB_MAX_ENTRIES) sub_dir_names[sub_dir_count++] = strdup(ent->d_name);
        } else if (has_comic_extension(ent->d_name)) {
            if (comic_count < FB_MAX_ENTRIES) comic_names[comic_count++] = strdup(ent->d_name);
        }
    }
    closedir(dir);

    bool found = false;

    if (comic_count > 0) {
        qsort(comic_names, comic_count, sizeof(char *), name_cmp_str);
        int written = snprintf(out, outsize, "%s/%s", dir_path, comic_names[0]);
        found = written > 0 && (size_t)written < outsize;
    } else if (sub_dir_count > 0) {
        qsort(sub_dir_names, sub_dir_count, sizeof(char *), name_cmp_str);
        char sub_path[FB_MAX_PATH];
        for (int i = 0; i < sub_dir_count && !found; i++) {
            int written = snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, sub_dir_names[i]);
            if (written > 0 && (size_t)written < (int)sizeof(sub_path)) {
                found = fb_find_representative_comic(sub_path, out, outsize, depth_limit - 1);
            }
        }
    }

    for (int i = 0; i < comic_count; i++) free(comic_names[i]);
    for (int i = 0; i < sub_dir_count; i++) free(sub_dir_names[i]);

    return found;
}
