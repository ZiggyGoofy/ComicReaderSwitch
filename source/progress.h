#pragma once

#include <stdbool.h>

// Charge la progression sauvegardée pour un comic donné (chemin complet).
// Retourne false si aucune progression n'existe encore pour ce fichier.
bool progress_load(const char *comic_path, int *out_page, int *out_total);

// Sauvegarde la page courante (index 0-based) et le nombre total de pages.
// Crée le dossier de progression si besoin. Retourne false en cas d'échec d'écriture.
bool progress_save(const char *comic_path, int page, int total);
