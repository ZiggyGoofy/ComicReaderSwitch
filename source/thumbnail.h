#pragma once

#include <SDL2/SDL.h>

// Récupère (depuis le cache disque si présent, sinon génère et met en cache)
// une texture de miniature pour une couverture.
//
// `cache_key` : identifiant utilisé pour nommer le fichier de cache (typiquement
//   le chemin de l'entrée telle qu'affichée dans le browser : un fichier comic,
//   ou un dossier — dans ce dernier cas la miniature représente le dossier
//   même si son contenu vient d'un fichier différent).
// `source_comic_path` : chemin du fichier .cbz/.cbr dont la première page sert
//   de source à la miniature (peut être identique à cache_key pour un fichier
//   direct, ou différent pour la couverture "représentative" d'un dossier).
//
// Retourne NULL si l'archive est illisible ou ne contient aucune image décodable.
SDL_Texture *thumbnail_get(SDL_Renderer *renderer, const char *cache_key,
                           const char *source_comic_path, int max_w, int max_h);
