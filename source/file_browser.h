#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FB_MAX_ENTRIES 512
#define FB_MAX_NAME    256
#define FB_MAX_PATH    512

typedef struct {
    char name[FB_MAX_NAME];
    bool is_dir;
} FBEntry;

typedef struct {
    char base_path[FB_MAX_PATH];   // dossier actuellement affiché
    char root_path[FB_MAX_PATH];   // dossier racine, infranchissable vers le haut
    FBEntry entries[FB_MAX_ENTRIES];
    int entry_count;
    int selected;                  // index sélectionné dans entries[]
} FileBrowser;

// Initialise le browser sur un dossier de départ et scanne son contenu.
// Retourne false si le dossier n'a pas pu être ouvert.
bool fb_init(FileBrowser *fb, const char *start_path);

// Rescanne le dossier courant (utile après un changement de base_path).
bool fb_scan(FileBrowser *fb);

// Déplace la sélection de `delta` (peut être négatif), avec clamp sur les bornes.
void fb_move_selection(FileBrowser *fb, int delta);

// Si l'entrée sélectionnée est un dossier, y entre et rescanne.
// Retourne true si on a changé de dossier.
bool fb_enter_selected(FileBrowser *fb);

// Remonte d'un niveau (dossier parent), si possible.
// Retourne true si on a changé de dossier.
bool fb_go_parent(FileBrowser *fb);

// Écrit le chemin complet de l'entrée sélectionnée dans `out` (taille outsize).
// Retourne false si rien n'est sélectionné.
bool fb_get_selected_path(const FileBrowser *fb, char *out, size_t outsize);

// Retourne true si l'entrée sélectionnée est un dossier.
bool fb_selected_is_dir(const FileBrowser *fb);

// Cherche, dans dir_path ou ses sous-dossiers (jusqu'à depth_limit niveaux),
// le premier fichier .cbz/.cbr rencontré (même ordre que fb_scan : priorité
// aux fichiers directs du dossier, sinon on descend dans le premier sous-dossier
// alphabétique). Écrit son chemin complet dans out. Retourne false si rien trouvé.
bool fb_find_representative_comic(const char *dir_path, char *out, size_t outsize, int depth_limit);
