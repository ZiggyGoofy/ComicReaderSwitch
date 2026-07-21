#pragma once

typedef enum {
    LANG_FR,
    LANG_EN
} Language;

// Tous les textes affichés à l'écran, dans une langue donnée. Les champs se
// terminant par _fmt sont des formats snprintf (avec %d), les autres sont
// des chaînes littérales prêtes à afficher.
typedef struct {
    const char *menu_library;

    const char *no_files_msg;
    const char *root_footer;
    const char *parent_quit_footer; // footer court utilisé quand un dossier est vide

    const char *detail_empty_msg;
    const char *detail_footer;

    const char *progress_done;      // "Lu" / "Read"
    const char *progress_in_progress_fmt; // "En cours (p. %d/%d)" / "In progress (p. %d/%d)"

    const char *reader_error_page;
    const char *reader_hint_page;
    const char *reader_hint_strip;
    const char *mode_label_page;    // "Mode: Page (Y)"
    const char *mode_label_strip;   // "Mode: Défilement (Y)" / "Mode: Scroll (Y)"
    const char *page_counter_fmt;   // "Page %d / %d"
} UIStrings;

// Détecte la langue configurée sur la console (français ou anglais par
// défaut pour tout le reste, vu qu'on ne gère que ces deux langues).
Language detect_system_language(void);

// Retourne la table de textes correspondant à la langue donnée.
const UIStrings *get_ui_strings(Language lang);
