#pragma once

#include <stdbool.h>
#include <stddef.h>

// Charge le chemin du dossier comics choisi par l'utilisateur (sauvegardé lors
// d'un lancement précédent). Retourne false si aucune configuration n'existe
// encore (premier lancement).
bool config_load_comics_dir(char *out, size_t out_size);

// Sauvegarde le chemin choisi, pour ne plus jamais redemander à l'utilisateur.
bool config_save_comics_dir(const char *path);
