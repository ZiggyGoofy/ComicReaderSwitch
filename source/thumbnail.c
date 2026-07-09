#include "thumbnail.h"
#include "archive.h"

#include <SDL2/SDL_image.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define THUMB_DIR "/switch/comic-reader/thumbnails"
#define THUMB_PATH_MAX 768

static void sanitize_to_filename(const char *path, char *out, size_t out_size) {
    size_t i = 0;
    for (; path[i] != '\0' && i < out_size - 1; i++) {
        out[i] = (path[i] == '/') ? '_' : path[i];
    }
    out[i] = '\0';
}

static bool build_thumb_path(const char *cache_key, char *out, size_t out_size) {
    char sanitized[THUMB_PATH_MAX];
    sanitize_to_filename(cache_key, sanitized, sizeof(sanitized));
    int written = snprintf(out, out_size, "%s/%s.png", THUMB_DIR, sanitized);
    return written > 0 && (size_t)written < out_size;
}

static void ensure_thumb_dir(void) {
    if (mkdir(THUMB_DIR, 0777) != 0 && errno != EEXIST) {
        printf("Impossible de créer %s (errno=%d)\n", THUMB_DIR, errno);
    }
}

// Extrait et décode la première page d'une archive comic en surface pleine résolution.
static SDL_Surface *load_first_page_surface(const char *comic_path) {
    ComicArchive ar;
    if (!ar_open(&ar, comic_path)) return NULL;

    void *data = NULL;
    size_t size = 0;
    bool ok = ar_extract_page(&ar, 0, &data, &size);
    ar_close(&ar);
    if (!ok) return NULL;

    SDL_RWops *rw = SDL_RWFromMem(data, (int)size);
    if (!rw) {
        free(data);
        return NULL;
    }

    SDL_Surface *surf = IMG_Load_RW(rw, 1); // libère rw et data via free() ci-dessous
    free(data);
    return surf; // peut être NULL si le décodage échoue
}

// Redimensionne `src` dans une surface max_w x max_h, centrée avec un fond
// transparent (letterboxing) pour préserver le ratio d'aspect de la couverture.
static SDL_Surface *scale_to_thumbnail(SDL_Surface *src, int max_w, int max_h) {
    float scale = SDL_min((float)max_w / src->w, (float)max_h / src->h);
    int scaled_w = (int)(src->w * scale);
    int scaled_h = (int)(src->h * scale);
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;

    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, max_w, max_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst) return NULL;
    SDL_FillRect(dst, NULL, SDL_MapRGBA(dst->format, 0, 0, 0, 0));

    SDL_Rect dst_rect = { (max_w - scaled_w) / 2, (max_h - scaled_h) / 2, scaled_w, scaled_h };
    SDL_BlitScaled(src, NULL, dst, &dst_rect);
    return dst;
}

SDL_Texture *thumbnail_get(SDL_Renderer *renderer, const char *cache_key,
                           const char *source_comic_path, int max_w, int max_h) {
    char thumb_path[THUMB_PATH_MAX];
    if (!build_thumb_path(cache_key, thumb_path, sizeof(thumb_path))) return NULL;

    // Chemin rapide : la miniature est déjà en cache sur la SD.
    SDL_Surface *cached = IMG_Load(thumb_path);
    if (cached) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, cached);
        SDL_FreeSurface(cached);
        return tex;
    }

    // Sinon, génération complète : décodage de la première page puis redimensionnement.
    SDL_Surface *full = load_first_page_surface(source_comic_path);
    if (!full) return NULL;

    SDL_Surface *thumb = scale_to_thumbnail(full, max_w, max_h);
    SDL_FreeSurface(full);
    if (!thumb) return NULL;

    ensure_thumb_dir();
    if (IMG_SavePNG(thumb, thumb_path) != 0) {
        printf("Impossible de sauvegarder la miniature %s: %s\n", thumb_path, IMG_GetError());
        // Non bloquant : on continue avec la texture déjà en mémoire.
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, thumb);
    SDL_FreeSurface(thumb);
    return tex;
}
