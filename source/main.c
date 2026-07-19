#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "file_browser.h"
#include "archive.h"
#include "progress.h"
#include "thumbnail.h"

#define SCREEN_W 1280
#define SCREEN_H 720

#define COMICS_DIR "/switch/comic-reader/comics"

#define ZOOM_MIN 1.0f
#define ZOOM_MAX 4.0f
#define ZOOM_SPEED_PER_FRAME 0.02f
#define PAN_SPEED_PER_FRAME 14.0f
#define STICK_DEADZONE 8000

// --- Barre de suivi de lecture (tactile + affichage), en bas de l'écran en lecture ---
#define BAR_MARGIN 40
#define BAR_OVERLAY_H 74           // hauteur totale de la bande semi-transparente
#define BAR_TOP_Y (SCREEN_H - BAR_OVERLAY_H)
#define BAR_TRACK_Y (SCREEN_H - 28)
#define BAR_TRACK_H 8
#define BAR_THUMB_R 12

// --- Barre latérale façon Noboru ---
#define SIDEBAR_W 260

// --- Grille de couvertures dans la zone de contenu (à droite de la sidebar) ---
#define CONTENT_MARGIN 32
#define GRID_COLS 4
#define GRID_GAP 14
#define GRID_TOP 36
#define GRID_FOOTER_H 40
#define GRID_TILE_W ((SCREEN_W - SIDEBAR_W - 2 * CONTENT_MARGIN - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS)
#define GRID_COVER_H ((int)(GRID_TILE_W * 1.1f))
#define GRID_TITLE_H 22
#define GRID_ROW_H (GRID_COVER_H + 8 + GRID_TITLE_H + GRID_GAP)

// --- Vue détail (à l'intérieur d'un dossier de série) : couverture à gauche,
// liste simple à droite, sans barre latérale ni élément superflu. ---
#define DETAIL_MARGIN 40
#define DETAIL_HEADER_H 70
#define DETAIL_FOOTER_H 40
#define HERO_COVER_W 260
#define HERO_COVER_H 380
#define DETAIL_LIST_GAP 40
#define DETAIL_ROW_H 46

typedef enum {
    APP_STATE_BROWSER,
    APP_STATE_READER
} AppState;

static TTF_Font *g_font = NULL;       // police standard (listes, libellés)
static TTF_Font *g_font_title = NULL; // police plus grande (titre de la sidebar)
static PlFontData g_font_data;

static char g_entry_labels[FB_MAX_ENTRIES][40];
static SDL_Texture *g_thumb_textures[FB_MAX_ENTRIES];
static bool g_thumb_attempted[FB_MAX_ENTRIES];

// Miniature dynamique unique pour la vue "détail de dossier" (couverture à
// gauche qui suit la sélection). Séparée du cache multi-miniatures de la
// grille racine, qui reste utilisé uniquement à la racine du dossier comics.
static SDL_Texture *g_hero_thumb = NULL;
static int g_hero_thumb_index = -1;

static bool load_system_font(void) {
    Result rc = plGetSharedFontByType(&g_font_data, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        printf("plGetSharedFontByType a échoué: 0x%x\n", rc);
        return false;
    }

    SDL_RWops *rw1 = SDL_RWFromConstMem(g_font_data.address, (int)g_font_data.size);
    if (rw1) g_font = TTF_OpenFontRW(rw1, 1, 22);

    SDL_RWops *rw2 = SDL_RWFromConstMem(g_font_data.address, (int)g_font_data.size);
    if (rw2) g_font_title = TTF_OpenFontRW(rw2, 1, 34);

    return g_font != NULL;
}

static int text_width_ex(TTF_Font *font, const char *text) {
    if (!font || !text || !text[0]) return 0;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) return 0;
    return w;
}

static int text_width(const char *text) {
    return text_width_ex(g_font, text);
}

static void clear_thumbnail_cache(void) {
    for (int i = 0; i < FB_MAX_ENTRIES; i++) {
        if (g_thumb_textures[i]) {
            SDL_DestroyTexture(g_thumb_textures[i]);
            g_thumb_textures[i] = NULL;
        }
        g_thumb_attempted[i] = false;
    }
    if (g_hero_thumb) {
        SDL_DestroyTexture(g_hero_thumb);
        g_hero_thumb = NULL;
    }
    g_hero_thumb_index = -1;
}

static void refresh_entry_labels(const FileBrowser *fb) {
    for (int i = 0; i < fb->entry_count; i++) {
        g_entry_labels[i][0] = '\0';
        if (fb->entries[i].is_dir) continue;

        char full_path[FB_MAX_PATH];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s",
                                fb->base_path, fb->entries[i].name);
        if (written <= 0 || (size_t)written >= sizeof(full_path)) continue;

        int page, total;
        if (!progress_load(full_path, &page, &total) || total <= 0) continue;

        if (page >= total - 1) {
            snprintf(g_entry_labels[i], sizeof(g_entry_labels[i]), "Lu");
        } else {
            snprintf(g_entry_labels[i], sizeof(g_entry_labels[i]),
                     "En cours (p. %d/%d)", page + 1, total);
        }
    }
}

static void on_directory_changed(const FileBrowser *fb) {
    clear_thumbnail_cache();
    refresh_entry_labels(fb);
}

static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h,
                       Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer *r, int x, int y, int w, int h,
                               Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha, int thickness) {
    for (int t = 0; t < thickness; t++) {
        SDL_SetRenderDrawColor(r, red, green, blue, alpha);
        SDL_Rect rect = { x - t, y - t, w + 2 * t, h + 2 * t };
        SDL_RenderDrawRect(r, &rect);
    }
}

static void draw_text_ex(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!font || !text || !text[0]) return;

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = { x, y, surf->w, surf->h };
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static void draw_text(SDL_Renderer *r, const char *text, int x, int y, SDL_Color color) {
    draw_text_ex(r, g_font, text, x, y, color);
}

static void truncate_to_width(const char *text, int max_w, char *out, size_t outsize) {
    strncpy(out, text, outsize - 1);
    out[outsize - 1] = '\0';

    if (text_width(out) <= max_w) return;

    size_t len = strlen(out);
    while (len > 1) {
        len--;
        out[len] = '\0';
        char with_ellipsis[128];
        snprintf(with_ellipsis, sizeof(with_ellipsis), "%s…", out);
        if (text_width(with_ellipsis) <= max_w) {
            strncpy(out, with_ellipsis, outsize - 1);
            out[outsize - 1] = '\0';
            return;
        }
    }
}

static void ensure_thumbnail_loaded(SDL_Renderer *renderer, const FileBrowser *fb, int index) {
    if (g_thumb_attempted[index]) return;
    g_thumb_attempted[index] = true;

    const FBEntry *e = &fb->entries[index];
    char full_path[FB_MAX_PATH];
    int written = snprintf(full_path, sizeof(full_path), "%s/%s", fb->base_path, e->name);
    if (written <= 0 || (size_t)written >= sizeof(full_path)) return;

    if (e->is_dir) {
        char representative[FB_MAX_PATH];
        if (fb_find_representative_comic(full_path, representative, sizeof(representative), 6)) {
            g_thumb_textures[index] = thumbnail_get(renderer, full_path, representative,
                                                     GRID_TILE_W, GRID_COVER_H);
        }
    } else {
        g_thumb_textures[index] = thumbnail_get(renderer, full_path, full_path,
                                                 GRID_TILE_W, GRID_COVER_H);
    }
}

// Charge la miniature de l'entrée `index` pour la vue détail, seulement si
// elle diffère de celle déjà en cache (se recharge à chaque changement de
// sélection ; le cache disque du module thumbnail rend ça rapide après la
// toute première génération).
static void ensure_hero_thumbnail_loaded(SDL_Renderer *renderer, const FileBrowser *fb, int index) {
    if (index == g_hero_thumb_index) return;

    if (g_hero_thumb) {
        SDL_DestroyTexture(g_hero_thumb);
        g_hero_thumb = NULL;
    }
    g_hero_thumb_index = index;

    if (index < 0 || index >= fb->entry_count) return;

    const FBEntry *e = &fb->entries[index];
    char full_path[FB_MAX_PATH];
    int written = snprintf(full_path, sizeof(full_path), "%s/%s", fb->base_path, e->name);
    if (written <= 0 || (size_t)written >= sizeof(full_path)) return;

    if (e->is_dir) {
        char representative[FB_MAX_PATH];
        if (fb_find_representative_comic(full_path, representative, sizeof(representative), 6)) {
            g_hero_thumb = thumbnail_get(renderer, full_path, representative,
                                          HERO_COVER_W, HERO_COVER_H);
        }
    } else {
        g_hero_thumb = thumbnail_get(renderer, full_path, full_path,
                                      HERO_COVER_W, HERO_COVER_H);
    }
}
// (seule section pertinente vu que tout est local, pas de catalogues en ligne).
static void render_sidebar(SDL_Renderer *renderer) {
    draw_rect(renderer, 0, 0, SIDEBAR_W, SCREEN_H, 14, 14, 20, 255);

    SDL_Color title_color = { 235, 235, 235, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };

    draw_text_ex(renderer, g_font_title, "Comic Reader", 28, 40, title_color);

    // Entrée de menu "Library", mise en avant comme section active.
    int menu_y = 130;
    draw_rect(renderer, 0, menu_y - 8, SIDEBAR_W, 44, 40, 40, 58, 255);
    draw_rect(renderer, 0, menu_y - 8, 4, 44, 255, 210, 90, 255); // liseré d'accent à gauche
    draw_text(renderer, "LIBRARY", 28, menu_y, accent);
}

static void render_library(SDL_Renderer *renderer, const FileBrowser *fb) {
    SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
    SDL_RenderClear(renderer);

    render_sidebar(renderer);

    SDL_Color white  = { 235, 235, 235, 255 };
    SDL_Color gray   = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color done_color = { 120, 200, 120, 255 };
    SDL_Color prog_color = { 120, 170, 220, 255 };

    int content_x = SIDEBAR_W + CONTENT_MARGIN;

    if (fb->entry_count == 0) {
        draw_text(renderer, "Aucun fichier .cbz/.cbr ni dossier ici.", content_x, GRID_TOP, gray);
        draw_text(renderer, "B: dossier parent   +: quitter", content_x, SCREEN_H - 34, gray);
        return;
    }

    int total_rows = (fb->entry_count + GRID_COLS - 1) / GRID_COLS;
    int available_h = SCREEN_H - GRID_TOP - GRID_FOOTER_H;
    int visible_rows = available_h / GRID_ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int selected_row = fb->selected / GRID_COLS;

    int scroll_row = selected_row - visible_rows / 2;
    if (scroll_row < 0) scroll_row = 0;
    if (scroll_row + visible_rows > total_rows) {
        scroll_row = total_rows - visible_rows;
        if (scroll_row < 0) scroll_row = 0;
    }

    for (int row = scroll_row; row < total_rows && row < scroll_row + visible_rows; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int index = row * GRID_COLS + col;
            if (index >= fb->entry_count) break;

            ensure_thumbnail_loaded(renderer, fb, index);

            int tile_x = content_x + col * (GRID_TILE_W + GRID_GAP);
            int tile_y = GRID_TOP + (row - scroll_row) * GRID_ROW_H;

            const FBEntry *e = &fb->entries[index];
            SDL_Texture *thumb = g_thumb_textures[index];

            if (thumb) {
                SDL_Rect dst = { tile_x, tile_y, GRID_TILE_W, GRID_COVER_H };
                SDL_RenderCopy(renderer, thumb, NULL, &dst);
            } else {
                draw_rect(renderer, tile_x, tile_y, GRID_TILE_W, GRID_COVER_H, 45, 45, 60, 255);
                char fallback[64];
                truncate_to_width(e->name, GRID_TILE_W - 20, fallback, sizeof(fallback));
                draw_text(renderer, fallback, tile_x + 10, tile_y + GRID_COVER_H / 2 - 12, gray);
            }

            if (index == fb->selected) {
                draw_rect_outline(renderer, tile_x, tile_y, GRID_TILE_W, GRID_COVER_H,
                                   255, 210, 90, 255, 4);
            }

            char title_display[64];
            truncate_to_width(e->name, GRID_TILE_W, title_display, sizeof(title_display));
            draw_text(renderer, title_display, tile_x, tile_y + GRID_COVER_H + 8,
                       index == fb->selected ? accent : white);

            if (g_entry_labels[index][0] != '\0') {
                bool is_done = (strcmp(g_entry_labels[index], "Lu") == 0);
                SDL_Color badge_color = is_done ? done_color : prog_color;
                draw_text(renderer, g_entry_labels[index], tile_x,
                           tile_y + GRID_COVER_H + 8 + GRID_TITLE_H - 10, badge_color);
            }
        }
    }

    draw_text(renderer, "A: ouvrir   B: dossier parent   Stick/D-pad: naviguer   +: quitter",
               content_x, SCREEN_H - 34, gray);
}

// Vue détail : utilisée dès qu'on est descendu dans un dossier de série.
// Couverture dynamique à gauche (suit la sélection), liste simple à droite,
// sans barre latérale ni élément superflu (pas de lien, pas d'icône de
// téléchargement — tout est déjà local).
static void render_folder_detail(SDL_Renderer *renderer, const FileBrowser *fb) {
    SDL_SetRenderDrawColor(renderer, 20, 20, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color white  = { 235, 235, 235, 255 };
    SDL_Color gray   = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color done_color = { 120, 200, 120, 255 };
    SDL_Color prog_color = { 120, 170, 220, 255 };

    // Titre = nom du dossier courant (dernier segment du chemin), pas le chemin complet.
    const char *folder_name = strrchr(fb->base_path, '/');
    folder_name = folder_name ? folder_name + 1 : fb->base_path;
    draw_text_ex(renderer, g_font_title, folder_name, DETAIL_MARGIN, 20, white);

    if (fb->entry_count == 0) {
        draw_text(renderer, "Ce dossier est vide.", DETAIL_MARGIN, DETAIL_HEADER_H + 20, gray);
        draw_text(renderer, "B: dossier parent   +: quitter", DETAIL_MARGIN, SCREEN_H - 34, gray);
        return;
    }

    ensure_hero_thumbnail_loaded(renderer, fb, fb->selected);

    // --- Couverture à gauche ---
    int cover_y = DETAIL_HEADER_H;
    if (g_hero_thumb) {
        SDL_Rect dst = { DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H };
        SDL_RenderCopy(renderer, g_hero_thumb, NULL, &dst);
    } else {
        draw_rect(renderer, DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H, 45, 45, 60, 255);
    }
    draw_rect_outline(renderer, DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H, 255, 210, 90, 255, 3);

    // --- Liste simple à droite ---
    int list_x = DETAIL_MARGIN + HERO_COVER_W + DETAIL_LIST_GAP;
    int list_top = DETAIL_HEADER_H;
    int available_h = SCREEN_H - list_top - DETAIL_FOOTER_H;
    int visible_rows = available_h / DETAIL_ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int start = fb->selected - visible_rows / 2;
    if (start < 0) start = 0;
    if (start + visible_rows > fb->entry_count) {
        start = fb->entry_count - visible_rows;
        if (start < 0) start = 0;
    }

    int list_right_edge = SCREEN_W - DETAIL_MARGIN;

    for (int row = 0; row < visible_rows && start + row < fb->entry_count; row++) {
        int entry_index = start + row;
        int y = list_top + row * DETAIL_ROW_H;
        const FBEntry *e = &fb->entries[entry_index];

        bool is_selected = (entry_index == fb->selected);
        if (is_selected) {
            draw_rect(renderer, list_x - 10, y - 4, list_right_edge - list_x + 10,
                       DETAIL_ROW_H - 6, 45, 45, 65, 255);
        }

        draw_text(renderer, e->name, list_x, y, is_selected ? accent : white);

        if (g_entry_labels[entry_index][0] != '\0') {
            int name_w = text_width(e->name);
            int progress_x = list_x + name_w + 24;
            bool is_done = (strcmp(g_entry_labels[entry_index], "Lu") == 0);
            SDL_Color color = is_done ? done_color : prog_color;

            int label_w = text_width(g_entry_labels[entry_index]);
            if (progress_x + label_w < list_right_edge) {
                draw_text(renderer, g_entry_labels[entry_index], progress_x, y, color);
            }
        }
    }

    draw_text(renderer, "A: ouvrir   B: dossier parent   Haut/Bas: naviguer   +: quitter",
               DETAIL_MARGIN, SCREEN_H - 34, gray);
}

static SDL_Texture *decode_page_to_texture(SDL_Renderer *renderer, void *data, size_t size) {
    SDL_RWops *rw = SDL_RWFromMem(data, (int)size);
    if (!rw) {
        free(data);
        return NULL;
    }

    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    free(data);

    if (!surf) {
        printf("IMG_Load_RW a échoué: %s\n", IMG_GetError());
        return NULL;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

static void load_current_page(SDL_Renderer *renderer, ComicArchive *ar, SDL_Texture **out_tex) {
    if (*out_tex) {
        SDL_DestroyTexture(*out_tex);
        *out_tex = NULL;
    }

    void *data = NULL;
    size_t size = 0;
    if (!ar_extract_page(ar, ar->current_page, &data, &size)) {
        printf("Extraction de la page %d échouée\n", ar->current_page);
        return;
    }

    *out_tex = decode_page_to_texture(renderer, data, size);
}

static float min_zoom_for_texture(SDL_Texture *tex) {
    if (!tex) return ZOOM_MIN;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return ZOOM_MIN;

    // Le "1.0" de référence est maintenant le calibrage sur la hauteur d'écran
    // (voir render_reader), pas le mode "cover" — on garde le même calcul du
    // ratio minimum pour voir la page entière, relatif à cette nouvelle référence.
    float cover_scale = (float)SCREEN_H / th;
    float fit_scale = SDL_min((float)SCREEN_W / tw, (float)SCREEN_H / th);
    return fit_scale / cover_scale;
}

static void render_reader(SDL_Renderer *renderer, SDL_Texture *tex, int page_index,
                           int page_count, float zoom, float *pan_x, float *pan_y) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (tex) {
        int tex_w, tex_h;
        SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);

        // Calibrage par défaut sur la HAUTEUR de l'écran (plutôt que sur la
        // largeur) : la page remplit toute la hauteur, quitte à rogner les
        // côtés si elle est plus large que l'écran une fois mise à cette échelle.
        // C'est le calibrage le plus utile pour lire des pages de comics,
        // presque toujours plus hautes que larges.
        float base_scale = (float)SCREEN_H / tex_h;
        int draw_w = (int)(tex_w * base_scale * zoom);
        int draw_h = (int)(tex_h * base_scale * zoom);

        float max_pan_x = (draw_w > SCREEN_W) ? (draw_w - SCREEN_W) / 2.0f : 0.0f;
        float max_pan_y = (draw_h > SCREEN_H) ? (draw_h - SCREEN_H) / 2.0f : 0.0f;

        if (*pan_x > max_pan_x) *pan_x = max_pan_x;
        if (*pan_x < -max_pan_x) *pan_x = -max_pan_x;
        if (*pan_y > max_pan_y) *pan_y = max_pan_y;
        if (*pan_y < -max_pan_y) *pan_y = -max_pan_y;

        SDL_Rect dst = {
            (SCREEN_W - draw_w) / 2 + (int)*pan_x,
            (SCREEN_H - draw_h) / 2 + (int)*pan_y,
            draw_w, draw_h
        };
        SDL_RenderCopy(renderer, tex, NULL, &dst);
    } else {
        SDL_Color gray = { 150, 150, 160, 255 };
        draw_text(renderer, "Impossible de charger cette page.", 40, SCREEN_H / 2, gray);
    }

    SDL_Color gray = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color white = { 235, 235, 235, 255 };

    // Bande semi-transparente en bas de l'écran, contenant le compteur de
    // page et la barre de suivi tactile.
    SDL_SetRenderDrawColor(renderer, 10, 10, 14, 190);
    SDL_Rect overlay = { 0, BAR_TOP_Y, SCREEN_W, BAR_OVERLAY_H };
    SDL_RenderFillRect(renderer, &overlay);

    char page_counter[32];
    snprintf(page_counter, sizeof(page_counter), "Page %d / %d", page_index + 1, page_count);
    draw_text(renderer, page_counter, BAR_MARGIN, BAR_TOP_Y + 8, white);

    const char *hint = "L/R ou barre tactile: page   Stick droit: zoom   B: retour";
    int hint_w = text_width(hint);
    draw_text(renderer, hint, SCREEN_W - BAR_MARGIN - hint_w, BAR_TOP_Y + 8, gray);

    // Piste de la barre de progression.
    int track_left = BAR_MARGIN;
    int track_right = SCREEN_W - BAR_MARGIN;
    SDL_SetRenderDrawColor(renderer, 70, 70, 85, 255);
    SDL_Rect track = { track_left, BAR_TRACK_Y - BAR_TRACK_H / 2, track_right - track_left, BAR_TRACK_H };
    SDL_RenderFillRect(renderer, &track);

    // Portion remplie jusqu'à la page courante.
    float progress_fraction = (page_count > 1) ? (float)page_index / (float)(page_count - 1) : 0.0f;
    int filled_w = (int)((track_right - track_left) * progress_fraction);
    SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
    SDL_Rect filled = { track_left, BAR_TRACK_Y - BAR_TRACK_H / 2, filled_w, BAR_TRACK_H };
    SDL_RenderFillRect(renderer, &filled);

    // Curseur (cercle simple approximé par un petit carré arrondi visuellement suffisant).
    int thumb_x = track_left + filled_w;
    SDL_SetRenderDrawColor(renderer, 255, 230, 150, 255);
    SDL_Rect thumb = { thumb_x - BAR_THUMB_R, BAR_TRACK_Y - BAR_THUMB_R,
                       BAR_THUMB_R * 2, BAR_THUMB_R * 2 };
    SDL_RenderFillRect(renderer, &thumb);
}

// Convertit une position X (en pixels écran) sur la barre de progression en
// index de page cible (0-based), en tenant compte des marges de la barre.
static int page_from_bar_x(int x, int page_count) {
    if (page_count <= 1) return 0;

    int track_left = BAR_MARGIN;
    int track_right = SCREEN_W - BAR_MARGIN;

    float fraction = (float)(x - track_left) / (float)(track_right - track_left);
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    int target = (int)(fraction * (page_count - 1) + 0.5f);
    if (target < 0) target = 0;
    if (target >= page_count) target = page_count - 1;
    return target;
}

static void save_current_progress(const ComicArchive *ar) {
    if (ar->page_count > 0) {
        progress_save(ar->archive_path, ar->current_page, ar->page_count);
    }
}

int main(int argc, char *argv[]) {
    consoleDebugInit(debugDevice_SVC);

    Result pl_rc = plInitialize(PlServiceType_User);
    if (R_FAILED(pl_rc)) {
        printf("plInitialize a échoué: 0x%x\n", pl_rc);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init a échoué: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        printf("TTF_Init a échoué: %s\n", TTF_GetError());
    }

    int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        printf("IMG_Init n'a pas pu charger tous les décodeurs: %s\n", IMG_GetError());
    }

    SDL_Window *window = SDL_CreateWindow("Comic Reader",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    load_system_font();

    SDL_GameController *pad = NULL;
    if (SDL_NumJoysticks() > 0) {
        pad = SDL_GameControllerOpen(0);
    }

    FileBrowser fb;
    if (!fb_init(&fb, COMICS_DIR)) {
        printf("Impossible d'ouvrir %s — vérifie qu'il existe sur la SD.\n", COMICS_DIR);
    }
    on_directory_changed(&fb);

    ComicArchive ar;
    memset(&ar, 0, sizeof(ar));
    SDL_Texture *page_tex = NULL;

    float zoom = ZOOM_MIN;
    float pan_x = 0.0f, pan_y = 0.0f;

    AppState state = APP_STATE_BROWSER;
    bool running = true;
    bool prev_up = false, prev_down = false, prev_left = false, prev_right = false,
         prev_a = false, prev_b = false, prev_plus = false, prev_l = false,
         prev_r = false, prev_r3 = false;

    // Suivi du glissement tactile/souris sur la barre de progression (lecture uniquement).
    bool bar_dragging = false;
    int drag_target_page = 0;

    while (running) {
        SDL_Event event;
        int events_this_frame = 0;
        while (SDL_PollEvent(&event)) {
            events_this_frame++;
            if (events_this_frame > 200) break; // sécurité anti-inondation d'événements
            if (event.type == SDL_QUIT) running = false;

            if (state == APP_STATE_READER) {
                int touch_x = -1, touch_y = -1;
                bool is_down = false, is_move = false, is_up = false;

                if (event.type == SDL_FINGERDOWN) {
                    touch_x = (int)(event.tfinger.x * SCREEN_W);
                    touch_y = (int)(event.tfinger.y * SCREEN_H);
                    is_down = true;
                } else if (event.type == SDL_FINGERMOTION) {
                    touch_x = (int)(event.tfinger.x * SCREEN_W);
                    touch_y = (int)(event.tfinger.y * SCREEN_H);
                    is_move = true;
                } else if (event.type == SDL_FINGERUP) {
                    is_up = true;
                } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    touch_x = event.button.x;
                    touch_y = event.button.y;
                    is_down = true;
                } else if (event.type == SDL_MOUSEMOTION) {
                    touch_x = event.motion.x;
                    touch_y = event.motion.y;
                    is_move = true;
                } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    is_up = true;
                }

                if (is_down && touch_y >= BAR_TOP_Y && ar.page_count > 0) {
                    bar_dragging = true;
                    drag_target_page = page_from_bar_x(touch_x, ar.page_count);
                } else if (is_move && bar_dragging && ar.page_count > 0) {
                    drag_target_page = page_from_bar_x(touch_x, ar.page_count);
                } else if (is_up && bar_dragging) {
                    bar_dragging = false;
                    if (drag_target_page != ar.current_page) {
                        ar.current_page = drag_target_page;
                        zoom = ZOOM_MIN;
                        pan_x = 0.0f;
                        pan_y = 0.0f;
                        load_current_page(renderer, &ar, &page_tex);
                        save_current_progress(&ar);
                    }
                }
            }
        }

        bool up = false, down = false, left = false, right = false;
        bool a = false, b = false, plus = false, l = false, r = false, r3 = false;
        int right_y = 0, left_x = 0, left_y = 0;
        if (pad) {
            up    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -16000;
            down  = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > 16000;
            left  = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) < -16000;
            right = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) > 16000;
            a    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
            b    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
            plus = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
            l    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            r    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            r3   = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);

            right_y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
            left_x  = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            left_y  = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        }

        if (state == APP_STATE_BROWSER) {
            bool at_root = fb_is_at_root(&fb);

            if (at_root) {
                int total = fb.entry_count;
                int row = fb.selected / GRID_COLS;
                int col = fb.selected % GRID_COLS;
                int total_rows = (total + GRID_COLS - 1) / GRID_COLS;

                if (up && !prev_up && row > 0) {
                    fb.selected -= GRID_COLS;
                }
                if (down && !prev_down && row < total_rows - 1) {
                    int candidate = fb.selected + GRID_COLS;
                    fb.selected = (candidate < total) ? candidate : total - 1;
                }
                if (left && !prev_left && col > 0) {
                    fb.selected -= 1;
                }
                if (right && !prev_right && col < GRID_COLS - 1 && fb.selected + 1 < total) {
                    fb.selected += 1;
                }
            } else {
                // Vue détail : liste simple, uniquement haut/bas.
                if (up && !prev_up)     fb_move_selection(&fb, -1);
                if (down && !prev_down) fb_move_selection(&fb, 1);
            }

            if (a && !prev_a) {
                if (fb_selected_is_dir(&fb)) {
                    if (fb_enter_selected(&fb)) on_directory_changed(&fb);
                } else {
                    char full_path[FB_MAX_PATH];
                    if (fb_get_selected_path(&fb, full_path, sizeof(full_path))) {
                        if (ar_open(&ar, full_path)) {
                            zoom = ZOOM_MIN;
                            pan_x = 0.0f;
                            pan_y = 0.0f;

                            int saved_page, saved_total;
                            if (progress_load(ar.archive_path, &saved_page, &saved_total)
                                && saved_page >= 0 && saved_page < ar.page_count) {
                                ar.current_page = saved_page;
                            }

                            load_current_page(renderer, &ar, &page_tex);
                            save_current_progress(&ar);
                            state = APP_STATE_READER;
                        } else {
                            printf("Impossible d'ouvrir l'archive: %s\n", full_path);
                        }
                    }
                }
            }

            if (b && !prev_b) {
                if (fb_go_parent(&fb)) on_directory_changed(&fb);
            }

        } else { // APP_STATE_READER
            if (abs(right_y) > STICK_DEADZONE) {
                float normalized = -right_y / 32768.0f;
                zoom += normalized * ZOOM_SPEED_PER_FRAME;
                float min_zoom = min_zoom_for_texture(page_tex);
                if (zoom < min_zoom) zoom = min_zoom;
                if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
            }

            if (abs(left_x) > STICK_DEADZONE) {
                pan_x += (left_x / 32768.0f) * PAN_SPEED_PER_FRAME;
            }
            if (abs(left_y) > STICK_DEADZONE) {
                pan_y -= (left_y / 32768.0f) * PAN_SPEED_PER_FRAME;
            }

            if (r3 && !prev_r3) {
                zoom = ZOOM_MIN;
                pan_x = 0.0f;
                pan_y = 0.0f;
            }

            if (r && !prev_r) {
                if (ar.current_page < ar.page_count - 1) {
                    ar_next_page(&ar);
                    zoom = ZOOM_MIN;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
                    load_current_page(renderer, &ar, &page_tex);
                    save_current_progress(&ar);
                }
            }
            if (l && !prev_l) {
                if (ar.current_page > 0) {
                    ar_prev_page(&ar);
                    zoom = ZOOM_MIN;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
                    load_current_page(renderer, &ar, &page_tex);
                    save_current_progress(&ar);
                }
            }

            if (b && !prev_b) {
                save_current_progress(&ar);
                if (page_tex) {
                    SDL_DestroyTexture(page_tex);
                    page_tex = NULL;
                }
                ar_close(&ar);
                state = APP_STATE_BROWSER;
                refresh_entry_labels(&fb);
            }
        }

        if (plus && !prev_plus) {
            if (state == APP_STATE_READER) save_current_progress(&ar);
            running = false;
        }

        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_a = a; prev_b = b; prev_plus = plus; prev_l = l; prev_r = r; prev_r3 = r3;

        if (state == APP_STATE_BROWSER) {
            if (fb_is_at_root(&fb)) {
                render_library(renderer, &fb);
            } else {
                render_folder_detail(renderer, &fb);
            }
        } else {
            int display_page = bar_dragging ? drag_target_page : ar.current_page;
            render_reader(renderer, page_tex, display_page, ar.page_count, zoom, &pan_x, &pan_y);
        }
        SDL_RenderPresent(renderer);
    }

    if (page_tex) SDL_DestroyTexture(page_tex);
    clear_thumbnail_cache();
    if (pad) SDL_GameControllerClose(pad);
    if (g_font) TTF_CloseFont(g_font);
    if (g_font_title) TTF_CloseFont(g_font_title);
    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    plExit();

    return 0;
}
