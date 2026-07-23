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
#include "strings.h"
#include "config.h"

#define SCREEN_W 1280
#define SCREEN_H 720

#define ZOOM_MIN 1.0f
#define ZOOM_MAX 4.0f
#define ZOOM_SPEED_PER_FRAME 0.02f
#define PAN_SPEED_PER_FRAME 14.0f
#define STICK_DEADZONE 8000

#define BAR_MARGIN 40
#define BAR_OVERLAY_H 74
#define BAR_TOP_Y (SCREEN_H - BAR_OVERLAY_H)
#define BAR_TRACK_Y (SCREEN_H - 28)
#define BAR_TRACK_H 8
#define BAR_THUMB_R 12

#define MODE_BUTTON_X (BAR_MARGIN + 200)
#define MODE_BUTTON_Y (BAR_TOP_Y + 6)
#define MODE_BUTTON_W 200
#define MODE_BUTTON_H 28

#define SWIPE_THRESHOLD_PX 80
#define PINCH_ZOOM_SCALE 0.006f

#define SIDEBAR_W 260

#define CONTENT_MARGIN 32
#define GRID_COLS 4
#define GRID_GAP 14
#define GRID_TOP 36
#define GRID_FOOTER_H 40
#define GRID_TILE_W ((SCREEN_W - SIDEBAR_W - 2 * CONTENT_MARGIN - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS)
#define GRID_COVER_H ((int)(GRID_TILE_W * 1.1f))
#define GRID_TITLE_H 22
#define GRID_ROW_H (GRID_COVER_H + 8 + GRID_TITLE_H + GRID_GAP)

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

typedef enum {
    READ_MODE_PAGE,
    READ_MODE_STRIP
} ReadMode;

#define STRIP_BEFORE 1
#define STRIP_AFTER 3
#define STRIP_WINDOW (STRIP_BEFORE + 1 + STRIP_AFTER)
#define STRIP_CENTER STRIP_BEFORE
typedef struct {
    int page_index;
    SDL_Texture *tex;
    int draw_h;
} StripSlot;

static TTF_Font *g_font = NULL;
static TTF_Font *g_font_title = NULL;
static PlFontData g_font_data;

static const UIStrings *UI = NULL;

static char g_entry_labels[FB_MAX_ENTRIES][40];
static SDL_Texture *g_thumb_textures[FB_MAX_ENTRIES];
static bool g_thumb_attempted[FB_MAX_ENTRIES];

static SDL_Texture *g_hero_thumb = NULL;
static int g_hero_thumb_index = -1;

static void save_current_progress(const ComicArchive *ar);

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
            snprintf(g_entry_labels[i], sizeof(g_entry_labels[i]), "%s", UI->progress_done);
        } else {
            snprintf(g_entry_labels[i], sizeof(g_entry_labels[i]),
                     UI->progress_in_progress_fmt, page + 1, total);
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

static void render_sidebar(SDL_Renderer *renderer) {
    draw_rect(renderer, 0, 0, SIDEBAR_W, SCREEN_H, 14, 14, 20, 255);

    SDL_Color title_color = { 235, 235, 235, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };

    draw_text_ex(renderer, g_font_title, "Comic Reader", 28, 40, title_color);

    int menu_y = 130;
    draw_rect(renderer, 0, menu_y - 8, SIDEBAR_W, 44, 40, 40, 58, 255);
    draw_rect(renderer, 0, menu_y - 8, 4, 44, 255, 210, 90, 255);
    draw_text(renderer, UI->menu_library, 28, menu_y, accent);
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
        draw_text(renderer, UI->no_files_msg, content_x, GRID_TOP, gray);
        draw_text(renderer, UI->parent_quit_footer, content_x, SCREEN_H - 34, gray);
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
                bool is_done = (strcmp(g_entry_labels[index], UI->progress_done) == 0);
                SDL_Color badge_color = is_done ? done_color : prog_color;
                draw_text(renderer, g_entry_labels[index], tile_x,
                           tile_y + GRID_COVER_H + 8 + GRID_TITLE_H - 10, badge_color);
            }
        }
    }

    draw_text(renderer, UI->root_footer,
               content_x, SCREEN_H - 34, gray);
}

static void render_folder_detail(SDL_Renderer *renderer, const FileBrowser *fb) {
    SDL_SetRenderDrawColor(renderer, 20, 20, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color white  = { 235, 235, 235, 255 };
    SDL_Color gray   = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color done_color = { 120, 200, 120, 255 };
    SDL_Color prog_color = { 120, 170, 220, 255 };

    const char *folder_name = strrchr(fb->base_path, '/');
    folder_name = folder_name ? folder_name + 1 : fb->base_path;
    draw_text_ex(renderer, g_font_title, folder_name, DETAIL_MARGIN, 20, white);

    if (fb->entry_count == 0) {
        draw_text(renderer, UI->detail_empty_msg, DETAIL_MARGIN, DETAIL_HEADER_H + 20, gray);
        draw_text(renderer, UI->parent_quit_footer, DETAIL_MARGIN, SCREEN_H - 34, gray);
        return;
    }

    ensure_hero_thumbnail_loaded(renderer, fb, fb->selected);

    int cover_y = DETAIL_HEADER_H;
    if (g_hero_thumb) {
        SDL_Rect dst = { DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H };
        SDL_RenderCopy(renderer, g_hero_thumb, NULL, &dst);
    } else {
        draw_rect(renderer, DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H, 45, 45, 60, 255);
    }
    draw_rect_outline(renderer, DETAIL_MARGIN, cover_y, HERO_COVER_W, HERO_COVER_H, 255, 210, 90, 255, 3);

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
            bool is_done = (strcmp(g_entry_labels[entry_index], UI->progress_done) == 0);
            SDL_Color color = is_done ? done_color : prog_color;

            int label_w = text_width(g_entry_labels[entry_index]);
            if (progress_x + label_w < list_right_edge) {
                draw_text(renderer, g_entry_labels[entry_index], progress_x, y, color);
            }
        }
    }

    draw_text(renderer, UI->detail_footer,
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

static StripSlot g_strip[STRIP_WINDOW];
static float g_strip_scroll = 0.0f;

static void strip_clear_slot(int i) {
    if (g_strip[i].tex) {
        SDL_DestroyTexture(g_strip[i].tex);
        g_strip[i].tex = NULL;
    }
    g_strip[i].page_index = -1;
    g_strip[i].draw_h = 0;
}

static void strip_clear_all(void) {
    for (int i = 0; i < STRIP_WINDOW; i++) strip_clear_slot(i);
    g_strip_scroll = 0.0f;
}

static void strip_load_slot(SDL_Renderer *renderer, ComicArchive *ar, int slot_i, int page_index) {
    strip_clear_slot(slot_i);
    if (page_index < 0 || page_index >= ar->page_count) return;

    void *data = NULL;
    size_t size = 0;
    if (!ar_extract_page(ar, page_index, &data, &size)) return;

    SDL_Texture *tex = decode_page_to_texture(renderer, data, size);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    int draw_h = (th > 0 && tw > 0) ? (int)((float)th * ((float)SCREEN_W / (float)tw)) : SCREEN_H;

    g_strip[slot_i].page_index = page_index;
    g_strip[slot_i].tex = tex;
    g_strip[slot_i].draw_h = draw_h;
}

static void strip_reset(SDL_Renderer *renderer, ComicArchive *ar) {
    for (int i = 0; i < STRIP_WINDOW; i++) {
        strip_load_slot(renderer, ar, i, ar->current_page + (i - STRIP_CENTER));
    }
    g_strip_scroll = 0.0f;
}

static void strip_shift_forward(SDL_Renderer *renderer, ComicArchive *ar) {
    if (g_strip[STRIP_CENTER + 1].page_index < 0) return;

    int old_center_h = g_strip[STRIP_CENTER].draw_h;

    strip_clear_slot(0);
    for (int i = 0; i < STRIP_WINDOW - 1; i++) {
        g_strip[i] = g_strip[i + 1];
    }
    g_strip[STRIP_WINDOW - 1].tex = NULL;
    g_strip[STRIP_WINDOW - 1].page_index = -1;
    g_strip[STRIP_WINDOW - 1].draw_h = 0;

    ar->current_page = g_strip[STRIP_CENTER].page_index;
    strip_load_slot(renderer, ar, STRIP_WINDOW - 1, ar->current_page + (STRIP_WINDOW - 1 - STRIP_CENTER));

    g_strip_scroll -= old_center_h;
    save_current_progress(ar);
}

static void strip_shift_backward(SDL_Renderer *renderer, ComicArchive *ar) {
    if (g_strip[STRIP_CENTER - 1].page_index < 0) return;

    int old_before_center_h = g_strip[STRIP_CENTER - 1].draw_h;

    strip_clear_slot(STRIP_WINDOW - 1);
    for (int i = STRIP_WINDOW - 1; i > 0; i--) {
        g_strip[i] = g_strip[i - 1];
    }
    g_strip[0].tex = NULL;
    g_strip[0].page_index = -1;
    g_strip[0].draw_h = 0;

    ar->current_page = g_strip[STRIP_CENTER].page_index;
    strip_load_slot(renderer, ar, 0, ar->current_page - STRIP_CENTER);

    g_strip_scroll += old_before_center_h;
    save_current_progress(ar);
}

static void strip_apply_scroll(SDL_Renderer *renderer, ComicArchive *ar, float delta) {
    g_strip_scroll += delta;

    if (g_strip[STRIP_CENTER - 1].page_index < 0 && g_strip_scroll < 0.0f) {
        g_strip_scroll = 0.0f;
    }
    if (g_strip[STRIP_CENTER + 1].page_index < 0) {
        float max_scroll = (float)g_strip[STRIP_CENTER].draw_h - (float)SCREEN_H;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (g_strip_scroll > max_scroll) g_strip_scroll = max_scroll;
    }

    while (g_strip_scroll >= (float)g_strip[STRIP_CENTER].draw_h && g_strip[STRIP_CENTER + 1].page_index >= 0) {
        strip_shift_forward(renderer, ar);
    }
    while (g_strip_scroll < 0.0f && g_strip[STRIP_CENTER - 1].page_index >= 0) {
        strip_shift_backward(renderer, ar);
    }
    if (g_strip_scroll < 0.0f) g_strip_scroll = 0.0f;
}

static float min_zoom_for_texture(SDL_Texture *tex) {
    if (!tex) return ZOOM_MIN;
    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return ZOOM_MIN;

    float cover_scale = (float)SCREEN_H / th;
    float fit_scale = SDL_min((float)SCREEN_W / tw, (float)SCREEN_H / th);
    return fit_scale / cover_scale;
}

static void render_reader_bar(SDL_Renderer *renderer, int page_index, int page_count, ReadMode mode) {
    SDL_Color gray = { 150, 150, 160, 255 };
    SDL_Color accent = { 255, 210, 90, 255 };
    SDL_Color white = { 235, 235, 235, 255 };

    SDL_SetRenderDrawColor(renderer, 10, 10, 14, 190);
    SDL_Rect overlay = { 0, BAR_TOP_Y, SCREEN_W, BAR_OVERLAY_H };
    SDL_RenderFillRect(renderer, &overlay);

    char page_counter[32];
    snprintf(page_counter, sizeof(page_counter), UI->page_counter_fmt, page_index + 1, page_count);
    draw_text(renderer, page_counter, BAR_MARGIN, BAR_TOP_Y + 8, white);

    draw_rect_outline(renderer, MODE_BUTTON_X, MODE_BUTTON_Y, MODE_BUTTON_W, MODE_BUTTON_H,
                       255, 210, 90, 255, 2);
    const char *mode_label = (mode == READ_MODE_PAGE) ? UI->mode_label_page : UI->mode_label_strip;
    int mode_label_w = text_width(mode_label);
    draw_text(renderer, mode_label, MODE_BUTTON_X + (MODE_BUTTON_W - mode_label_w) / 2,
               MODE_BUTTON_Y + 5, accent);

    const char *hint = (mode == READ_MODE_PAGE) ? UI->reader_hint_page : UI->reader_hint_strip;
    int hint_w = text_width(hint);
    draw_text(renderer, hint, SCREEN_W - BAR_MARGIN - hint_w, BAR_TOP_Y + 8, gray);

    int track_left = BAR_MARGIN;
    int track_right = SCREEN_W - BAR_MARGIN;
    SDL_SetRenderDrawColor(renderer, 70, 70, 85, 255);
    SDL_Rect track = { track_left, BAR_TRACK_Y - BAR_TRACK_H / 2, track_right - track_left, BAR_TRACK_H };
    SDL_RenderFillRect(renderer, &track);

    float progress_fraction = (page_count > 1) ? (float)page_index / (float)(page_count - 1) : 0.0f;
    int filled_w = (int)((track_right - track_left) * progress_fraction);
    SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
    SDL_Rect filled = { track_left, BAR_TRACK_Y - BAR_TRACK_H / 2, filled_w, BAR_TRACK_H };
    SDL_RenderFillRect(renderer, &filled);

    int thumb_x = track_left + filled_w;
    SDL_SetRenderDrawColor(renderer, 255, 230, 150, 255);
    SDL_Rect thumb = { thumb_x - BAR_THUMB_R, BAR_TRACK_Y - BAR_THUMB_R,
                       BAR_THUMB_R * 2, BAR_THUMB_R * 2 };
    SDL_RenderFillRect(renderer, &thumb);
}

static void render_reader(SDL_Renderer *renderer, SDL_Texture *tex, int page_index,
                           int page_count, float zoom, float *pan_x, float *pan_y, bool bar_visible) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (tex) {
        int tex_w, tex_h;
        SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);

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
        draw_text(renderer, UI->reader_error_page, 40, SCREEN_H / 2, gray);
    }

    if (bar_visible) {
        render_reader_bar(renderer, page_index, page_count, READ_MODE_PAGE);
    }
}

static void render_reader_strip(SDL_Renderer *renderer, int page_count, bool bar_visible,
                                 float zoom, float *pan_x) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int draw_w = (int)(SCREEN_W * zoom);
    float max_pan_x = (draw_w > SCREEN_W) ? (draw_w - SCREEN_W) / 2.0f : 0.0f;
    if (*pan_x > max_pan_x) *pan_x = max_pan_x;
    if (*pan_x < -max_pan_x) *pan_x = -max_pan_x;
    int x_pos = (SCREEN_W - draw_w) / 2 + (int)*pan_x;

    float y_cursor = -g_strip_scroll * zoom;
    for (int i = 0; i < STRIP_CENTER; i++) {
        if (g_strip[i].page_index >= 0) {
            y_cursor -= (float)g_strip[i].draw_h * zoom;
        }
    }

    for (int i = 0; i < STRIP_WINDOW; i++) {
        if (g_strip[i].page_index < 0 || !g_strip[i].tex) continue;

        int slot_draw_h = (int)((float)g_strip[i].draw_h * zoom);
        SDL_Rect dst = { x_pos, (int)y_cursor, draw_w, slot_draw_h };
        if (dst.y + dst.h > 0 && dst.y < SCREEN_H) {
            SDL_RenderCopy(renderer, g_strip[i].tex, NULL, &dst);
        }
        y_cursor += (float)slot_draw_h;
    }

    int current_page = g_strip[STRIP_CENTER].page_index >= 0 ? g_strip[STRIP_CENTER].page_index : 0;
    if (bar_visible) {
        render_reader_bar(renderer, current_page, page_count, READ_MODE_STRIP);
    }
}

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

static float touch_distance(int x1, int y1, int x2, int y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return SDL_sqrtf(dx * dx + dy * dy);
}

static void save_current_progress(const ComicArchive *ar) {
    if (ar->page_count > 0) {
        progress_save(ar->archive_path, ar->current_page, ar->page_count);
    }
}

static bool run_folder_picker(SDL_Renderer *renderer, SDL_GameController *pad,
                               char *out_path, size_t out_size) {
    FileBrowser picker;
    if (!fb_init(&picker, "/")) {
        strncpy(out_path, "/", out_size - 1);
        out_path[out_size - 1] = '\0';
        return true;
    }

    bool prev_up = false, prev_down = false, prev_a = false, prev_b = false, prev_plus = false;
    bool running = true;
    bool confirmed = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }

        bool up = false, down = false, a = false, b = false, plus = false;
        if (pad) {
            up   = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)
                || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -16000;
            down = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > 16000;
            a    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
            b    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
            plus = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
        }

        if (up && !prev_up) fb_move_selection(&picker, -1);
        if (down && !prev_down) fb_move_selection(&picker, 1);

        if (a && !prev_a) {
            if (fb_selected_is_dir(&picker)) {
                fb_enter_selected(&picker);
            }
        }
        if (b && !prev_b) {
            fb_go_parent(&picker);
        }
        if (plus && !prev_plus) {
            confirmed = true;
            running = false;
        }

        prev_up = up; prev_down = down; prev_a = a; prev_b = b; prev_plus = plus;

        SDL_SetRenderDrawColor(renderer, 20, 20, 28, 255);
        SDL_RenderClear(renderer);

        SDL_Color white  = { 235, 235, 235, 255 };
        SDL_Color gray   = { 150, 150, 160, 255 };
        SDL_Color accent = { 255, 210, 90, 255 };

        draw_text_ex(renderer, g_font_title, "Choisis ton dossier de comics", 40, 20, white);
        draw_text(renderer, picker.base_path, 40, 70, gray);

        int row_height = 40;
        int list_top = 110;
        int visible_rows = (SCREEN_H - list_top - 60) / row_height;
        if (visible_rows < 1) visible_rows = 1;

        if (picker.entry_count == 0) {
            draw_text(renderer, "(dossier vide)", 40, list_top, gray);
        } else {
            int start = picker.selected - visible_rows / 2;
            if (start < 0) start = 0;
            if (start + visible_rows > picker.entry_count) {
                start = picker.entry_count - visible_rows;
                if (start < 0) start = 0;
            }

            for (int i = start; i < picker.entry_count && i < start + visible_rows; i++) {
                int y = list_top + (i - start) * row_height;
                bool is_selected = (i == picker.selected);
                if (is_selected) {
                    draw_rect(renderer, 30, y - 4, SCREEN_W - 60, row_height - 6, 60, 60, 90, 255);
                }
                draw_text(renderer, picker.entries[i].name, 50, y, is_selected ? accent : white);
            }
        }

        draw_text(renderer, "A: entrer   B: dossier parent   +: choisir ce dossier",
                   40, SCREEN_H - 40, gray);

        SDL_RenderPresent(renderer);
    }

    if (confirmed) {
        strncpy(out_path, picker.base_path, out_size - 1);
        out_path[out_size - 1] = '\0';
    }
    return confirmed;
}

int main(int argc, char *argv[]) {
    consoleDebugInit(debugDevice_SVC);

    UI = get_ui_strings(detect_system_language());

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

    char comics_dir[FB_MAX_PATH];
    if (!config_load_comics_dir(comics_dir, sizeof(comics_dir))) {
        if (!run_folder_picker(renderer, pad, comics_dir, sizeof(comics_dir))) {
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
        config_save_comics_dir(comics_dir);
    }

    FileBrowser fb;
    if (!fb_init(&fb, comics_dir)) {
        printf("Impossible d'ouvrir %s — vérifie qu'il existe sur la SD.\n", comics_dir);
    }
    on_directory_changed(&fb);

    ComicArchive ar;
    memset(&ar, 0, sizeof(ar));
    SDL_Texture *page_tex = NULL;

    float zoom = ZOOM_MIN;
    float pan_x = 0.0f, pan_y = 0.0f;

    AppState state = APP_STATE_BROWSER;
    ReadMode read_mode = READ_MODE_PAGE;
    bool running = true;
    bool prev_up = false, prev_down = false, prev_left = false, prev_right = false,
         prev_a = false, prev_b = false, prev_plus = false, prev_l = false,
         prev_r = false, prev_r3 = false, prev_y_btn = false, prev_x_btn = false;

    bool bar_visible = true;

    bool bar_dragging = false;
    int drag_target_page = 0;
    bool strip_touch_dragging = false;
    int strip_last_touch_y = 0;

    SDL_FingerID swipe_finger_id = -1;
    int swipe_start_x = 0, swipe_start_y = 0;
    int swipe_last_x = 0, swipe_last_y = 0;
    bool swipe_active = false;

    SDL_FingerID pinch_finger2_id = -1;
    int pinch_x1 = 0, pinch_y1 = 0, pinch_x2 = 0, pinch_y2 = 0;
    float pinch_last_distance = 0.0f;
    bool pinching = false;

    while (running) {
        SDL_Event event;
        int events_this_frame = 0;
        while (SDL_PollEvent(&event)) {
            events_this_frame++;
            if (events_this_frame > 200) break;
            if (event.type == SDL_QUIT) running = false;

            if (state == APP_STATE_READER) {
                int touch_x = -1, touch_y = -1;
                bool is_down = false, is_move = false, is_up = false;
                SDL_FingerID finger_id = -1;

                if (event.type == SDL_FINGERDOWN) {
                    touch_x = (int)(event.tfinger.x * SCREEN_W);
                    touch_y = (int)(event.tfinger.y * SCREEN_H);
                    is_down = true;
                    finger_id = event.tfinger.fingerId;
                } else if (event.type == SDL_FINGERMOTION) {
                    touch_x = (int)(event.tfinger.x * SCREEN_W);
                    touch_y = (int)(event.tfinger.y * SCREEN_H);
                    is_move = true;
                    finger_id = event.tfinger.fingerId;
                } else if (event.type == SDL_FINGERUP) {
                    is_up = true;
                    finger_id = event.tfinger.fingerId;
                } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    touch_x = event.button.x;
                    touch_y = event.button.y;
                    is_down = true;
                    finger_id = -100;
                } else if (event.type == SDL_MOUSEMOTION) {
                    touch_x = event.motion.x;
                    touch_y = event.motion.y;
                    is_move = true;
                    finger_id = -100;
                } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    is_up = true;
                    finger_id = -100;
                }

                bool was_bar_visible = bar_visible;
                bool hit_mode_button = was_bar_visible && is_down
                                     && touch_x >= MODE_BUTTON_X && touch_x <= MODE_BUTTON_X + MODE_BUTTON_W
                                     && touch_y >= MODE_BUTTON_Y && touch_y <= MODE_BUTTON_Y + MODE_BUTTON_H;

                if (is_down && !hit_mode_button) {
                    bar_visible = false;
                }

                if (hit_mode_button) {
                    if (read_mode == READ_MODE_PAGE) {
                        read_mode = READ_MODE_STRIP;
                        zoom = ZOOM_MIN;
                        pan_x = 0.0f;
                        strip_reset(renderer, &ar);
                    } else {
                        read_mode = READ_MODE_PAGE;
                        zoom = ZOOM_MIN;
                        pan_x = 0.0f;
                        pan_y = 0.0f;
                        load_current_page(renderer, &ar, &page_tex);
                    }
                } else if (read_mode == READ_MODE_PAGE) {
                    if (is_down && was_bar_visible && touch_y >= BAR_TOP_Y && ar.page_count > 0) {
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
                    } else if (is_down && touch_y < BAR_TOP_Y) {
                        if (swipe_finger_id == -1 && pinch_finger2_id == -1) {
                            swipe_finger_id = finger_id;
                            swipe_start_x = touch_x;
                            swipe_start_y = touch_y;
                            swipe_last_x = touch_x;
                            swipe_last_y = touch_y;
                            swipe_active = true;
                        } else if (swipe_finger_id != -1 && finger_id != swipe_finger_id && pinch_finger2_id == -1) {
                            pinch_finger2_id = finger_id;
                            pinch_x1 = swipe_last_x;
                            pinch_y1 = swipe_last_y;
                            pinch_x2 = touch_x;
                            pinch_y2 = touch_y;
                            pinch_last_distance = touch_distance(pinch_x1, pinch_y1, pinch_x2, pinch_y2);
                            pinching = true;
                            swipe_active = false;
                        }
                    } else if (is_move) {
                        if (pinching && finger_id == swipe_finger_id) {
                            pinch_x1 = touch_x; pinch_y1 = touch_y;
                        } else if (pinching && finger_id == pinch_finger2_id) {
                            pinch_x2 = touch_x; pinch_y2 = touch_y;
                        } else if (swipe_active && finger_id == swipe_finger_id) {
                            swipe_last_x = touch_x;
                            swipe_last_y = touch_y;
                        }

                        if (pinching) {
                            float current_distance = touch_distance(pinch_x1, pinch_y1, pinch_x2, pinch_y2);
                            float delta = current_distance - pinch_last_distance;
                            zoom += delta * PINCH_ZOOM_SCALE;
                            float min_zoom = min_zoom_for_texture(page_tex);
                            if (zoom < min_zoom) zoom = min_zoom;
                            if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
                            pinch_last_distance = current_distance;
                        }
                    } else if (is_up) {
                        if (pinching && (finger_id == swipe_finger_id || finger_id == pinch_finger2_id)) {
                            pinching = false;
                            swipe_finger_id = -1;
                            pinch_finger2_id = -1;
                            swipe_active = false;
                        } else if (swipe_active && finger_id == swipe_finger_id) {
                            int delta_x = swipe_last_x - swipe_start_x;
                            int delta_y = swipe_last_y - swipe_start_y;
                            if (abs(delta_x) > SWIPE_THRESHOLD_PX && abs(delta_x) > abs(delta_y)) {
                                if (delta_x < 0 && ar.current_page < ar.page_count - 1) {
                                    ar_next_page(&ar);
                                    zoom = ZOOM_MIN; pan_x = 0.0f; pan_y = 0.0f;
                                    load_current_page(renderer, &ar, &page_tex);
                                    save_current_progress(&ar);
                                } else if (delta_x > 0 && ar.current_page > 0) {
                                    ar_prev_page(&ar);
                                    zoom = ZOOM_MIN; pan_x = 0.0f; pan_y = 0.0f;
                                    load_current_page(renderer, &ar, &page_tex);
                                    save_current_progress(&ar);
                                }
                            }
                            swipe_finger_id = -1;
                            swipe_active = false;
                        }
                    }
                } else {
                    if (is_down) {
                        strip_touch_dragging = true;
                        strip_last_touch_y = touch_y;
                    } else if (is_move && strip_touch_dragging) {
                        int delta = strip_last_touch_y - touch_y;
                        strip_apply_scroll(renderer, &ar, (float)delta);
                        strip_last_touch_y = touch_y;
                    } else if (is_up && strip_touch_dragging) {
                        strip_touch_dragging = false;
                        save_current_progress(&ar);
                    }
                }
            }
        }

        bool up = false, down = false, left = false, right = false;
        bool a = false, b = false, plus = false, l = false, r = false, r3 = false, y_btn = false, x_btn = false;
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
            y_btn = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
            x_btn = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y);

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
                            read_mode = READ_MODE_PAGE;
                            strip_clear_all();
                            bar_visible = true;
                            swipe_finger_id = -1;
                            swipe_active = false;
                            pinch_finger2_id = -1;
                            pinching = false;

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

        } else {
            if (y_btn && !prev_y_btn) {
                if (read_mode == READ_MODE_PAGE) {
                    read_mode = READ_MODE_STRIP;
                    zoom = ZOOM_MIN;
                    pan_x = 0.0f;
                    strip_reset(renderer, &ar);
                } else {
                    read_mode = READ_MODE_PAGE;
                    zoom = ZOOM_MIN;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
                    load_current_page(renderer, &ar, &page_tex);
                }
            }

            if (x_btn && !prev_x_btn) {
                bar_visible = !bar_visible;
            }

            if (read_mode == READ_MODE_PAGE) {
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
            } else {
                if (abs(right_y) > STICK_DEADZONE) {
                    float normalized = -right_y / 32768.0f;
                    zoom += normalized * ZOOM_SPEED_PER_FRAME;
                    if (zoom < ZOOM_MIN) zoom = ZOOM_MIN;
                    if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
                }

                if (abs(left_y) > STICK_DEADZONE) {
                    strip_apply_scroll(renderer, &ar, (left_y / 32768.0f) * PAN_SPEED_PER_FRAME);
                    save_current_progress(&ar);
                }

                if (zoom > ZOOM_MIN && abs(left_x) > STICK_DEADZONE) {
                    pan_x += (left_x / 32768.0f) * PAN_SPEED_PER_FRAME;
                }

                if (r3 && !prev_r3) {
                    zoom = ZOOM_MIN;
                    pan_x = 0.0f;
                }

                if (r && !prev_r) {
                    strip_apply_scroll(renderer, &ar, (float)SCREEN_H);
                    save_current_progress(&ar);
                }
                if (l && !prev_l) {
                    strip_apply_scroll(renderer, &ar, -(float)SCREEN_H);
                    save_current_progress(&ar);
                }
            }

            if (b && !prev_b) {
                save_current_progress(&ar);
                if (page_tex) {
                    SDL_DestroyTexture(page_tex);
                    page_tex = NULL;
                }
                strip_clear_all();
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
        prev_y_btn = y_btn;
        prev_x_btn = x_btn;

        if (state == APP_STATE_BROWSER) {
            if (fb_is_at_root(&fb)) {
                render_library(renderer, &fb);
            } else {
                render_folder_detail(renderer, &fb);
            }
        } else {
            if (read_mode == READ_MODE_PAGE) {
                int display_page = bar_dragging ? drag_target_page : ar.current_page;
                render_reader(renderer, page_tex, display_page, ar.page_count, zoom, &pan_x, &pan_y, bar_visible);
            } else {
                render_reader_strip(renderer, ar.page_count, bar_visible, zoom, &pan_x);
            }
        }
        SDL_RenderPresent(renderer);
    }

    if (page_tex) SDL_DestroyTexture(page_tex);
    strip_clear_all();
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
