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

// --- Disposition de la vue bibliothèque hybride ---
#define OUTER_MARGIN 40
#define HEADER_H 70
#define FOOTER_H 40

// Carte "mise en avant" : uniquement pour le tout premier élément du dossier.
// C'est la SEULE miniature générée à l'ouverture d'un dossier — le reste de
// la liste est du texte simple, pour ne jamais avoir de délai de chargement
// proportionnel au nombre de fichiers.
#define HERO_COVER_W 220
#define HERO_COVER_H 310
#define HERO_GAP 30

#define LIST_ROW_H 46

typedef enum {
    APP_STATE_BROWSER,
    APP_STATE_READER
} AppState;

static TTF_Font *g_font = NULL;
static PlFontData g_font_data;

// Libellés de progression ("Lu" / "En cours (p. X/Y)"), un par entrée du browser.
static char g_entry_labels[FB_MAX_ENTRIES][40];

// Une seule miniature en cache à la fois : celle de l'élément actuellement
// sélectionné. Se recharge à chaque changement de sélection (le cache disque
// du module thumbnail rend les rechargements rapides après la 1re génération).
static SDL_Texture *g_hero_thumb = NULL;
static int g_hero_thumb_index = -1; // index d'entrée représenté par g_hero_thumb (-1 = aucun)

static bool load_system_font(int point_size) {
    Result rc = plGetSharedFontByType(&g_font_data, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        printf("plGetSharedFontByType a échoué: 0x%x\n", rc);
        return false;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(g_font_data.address, (int)g_font_data.size);
    if (!rw) return false;

    g_font = TTF_OpenFontRW(rw, 1, point_size);
    return g_font != NULL;
}

static int text_width(const char *text) {
    if (!g_font || !text || !text[0]) return 0;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(g_font, text, &w, &h) != 0) return 0;
    return w;
}

static void clear_hero_thumbnail(void) {
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

// À appeler après chaque (re)scan du dossier affiché.
static void on_directory_changed(const FileBrowser *fb) {
    clear_hero_thumbnail();
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

static void draw_text(SDL_Renderer *r, const char *text, int x, int y, SDL_Color color) {
    if (!g_font || !text || !text[0]) return;

    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, text, color);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = { x, y, surf->w, surf->h };
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
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

// Charge la miniature de l'entrée `index` si elle diffère de celle déjà en
// cache. Pour un dossier, va chercher la première page du premier comic
// trouvé à l'intérieur (récursivement).
static void ensure_hero_thumbnail_loaded(SDL_Renderer *renderer, const FileBrowser *fb, int index) {
    if (index == g_hero_thumb_index) return; // déjà la bonne miniature en cache

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

static void render_library(SDL_Renderer *renderer, const FileBrowser *fb) {
    SDL_SetRenderDrawColor(renderer, 20, 20, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color white  = { 235, 235, 235, 255 };
    SDL_Color gray   = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color done_color = { 120, 200, 120, 255 };
    SDL_Color prog_color = { 120, 170, 220, 255 };

    draw_text(renderer, "Comic Reader", OUTER_MARGIN, 24, gray);

    if (fb->entry_count == 0) {
        draw_text(renderer, "Aucun fichier .cbz/.cbr ni dossier ici.", OUTER_MARGIN, 100, gray);
        draw_text(renderer, "B: dossier parent   +: quitter", OUTER_MARGIN, SCREEN_H - 34, gray);
        return;
    }

    ensure_hero_thumbnail_loaded(renderer, fb, fb->selected);

    // --- Carte de prévisualisation (reflète l'élément sélectionné) ---
    int hero_y = HEADER_H;
    const FBEntry *hero_entry = &fb->entries[fb->selected];

    if (g_hero_thumb) {
        SDL_Rect dst = { OUTER_MARGIN, hero_y, HERO_COVER_W, HERO_COVER_H };
        SDL_RenderCopy(renderer, g_hero_thumb, NULL, &dst);
    } else {
        draw_rect(renderer, OUTER_MARGIN, hero_y, HERO_COVER_W, HERO_COVER_H, 45, 45, 60, 255);
    }

    draw_rect_outline(renderer, OUTER_MARGIN, hero_y, HERO_COVER_W, HERO_COVER_H,
                       255, 210, 90, 255, 4);

    int hero_text_x = OUTER_MARGIN + HERO_COVER_W + HERO_GAP;
    int hero_text_max_w = SCREEN_W - hero_text_x - OUTER_MARGIN;
    char hero_title[96];
    truncate_to_width(hero_entry->name, hero_text_max_w, hero_title, sizeof(hero_title));
    draw_text(renderer, hero_title, hero_text_x, hero_y + 10, accent);

    if (g_entry_labels[fb->selected][0] != '\0') {
        bool is_done = (strcmp(g_entry_labels[fb->selected], "Lu") == 0);
        draw_text(renderer, g_entry_labels[fb->selected], hero_text_x, hero_y + 46,
                   is_done ? done_color : prog_color);
    }

    // --- Liste complète (tous les éléments, y compris celui prévisualisé) ---
    int list_top = hero_y + HERO_COVER_H + HERO_GAP;
    int available_h = SCREEN_H - list_top - FOOTER_H;
    int visible_rows = available_h / LIST_ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int start = fb->selected - visible_rows / 2;
    if (start < 0) start = 0;
    if (start + visible_rows > fb->entry_count) {
        start = fb->entry_count - visible_rows;
        if (start < 0) start = 0;
    }

    for (int row = 0; row < visible_rows && start + row < fb->entry_count; row++) {
        int entry_index = start + row;
        int y = list_top + row * LIST_ROW_H;
        const FBEntry *e = &fb->entries[entry_index];

        bool is_selected = (entry_index == fb->selected);
        if (is_selected) {
            draw_rect(renderer, OUTER_MARGIN - 10, y - 4, SCREEN_W - 2 * (OUTER_MARGIN - 10),
                       LIST_ROW_H - 6, 60, 60, 90, 255);
        }

        draw_text(renderer, e->name, OUTER_MARGIN, y, is_selected ? accent : white);

        if (g_entry_labels[entry_index][0] != '\0') {
            int name_w = text_width(e->name);
            int progress_x = OUTER_MARGIN + name_w + 24;
            bool is_done = (strcmp(g_entry_labels[entry_index], "Lu") == 0);
            SDL_Color color = is_done ? done_color : prog_color;

            int label_w = text_width(g_entry_labels[entry_index]);
            if (progress_x + label_w < SCREEN_W - 20) {
                draw_text(renderer, g_entry_labels[entry_index], progress_x, y, color);
            }
        }
    }

    draw_text(renderer, "A: ouvrir   B: dossier parent   Haut/Bas: naviguer   +: quitter",
               OUTER_MARGIN, SCREEN_H - 34, gray);
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

    float cover_scale = SDL_max((float)SCREEN_W / tw, (float)SCREEN_H / th);
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

        float base_scale = SDL_max((float)SCREEN_W / tex_w, (float)SCREEN_H / tex_h);
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
    char footer[160];
    snprintf(footer, sizeof(footer),
             "Page %d / %d   L/R: page   Stick droit: zoom   Stick gauche: déplacer   B: retour",
             page_index + 1, page_count);
    draw_text(renderer, footer, 40, SCREEN_H - 34, gray);
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

    load_system_font(24);

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
    bool prev_up = false, prev_down = false, prev_a = false, prev_b = false,
         prev_plus = false, prev_l = false, prev_r = false, prev_r3 = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }

        bool up = false, down = false;
        bool a = false, b = false, plus = false, l = false, r = false, r3 = false;
        int right_y = 0, left_x = 0, left_y = 0;
        if (pad) {
            up    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -16000;
            down  = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > 16000;
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
            if (up && !prev_up)     fb_move_selection(&fb, -1);
            if (down && !prev_down) fb_move_selection(&fb, 1);

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
                pan_y += (left_y / 32768.0f) * PAN_SPEED_PER_FRAME;
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

        prev_up = up; prev_down = down;
        prev_a = a; prev_b = b; prev_plus = plus; prev_l = l; prev_r = r; prev_r3 = r3;

        if (state == APP_STATE_BROWSER) {
            render_library(renderer, &fb);
        } else {
            render_reader(renderer, page_tex, ar.current_page, ar.page_count, zoom, &pan_x, &pan_y);
        }
        SDL_RenderPresent(renderer);
    }

    if (page_tex) SDL_DestroyTexture(page_tex);
    clear_hero_thumbnail();
    if (pad) SDL_GameControllerClose(pad);
    if (g_font) TTF_CloseFont(g_font);
    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    plExit();

    return 0;
}
