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

    const char *end_title_next;     // fin de chapitre, un suivant existe
    const char *end_hint_next;
    const char *end_title_last;     // fin du dernier chapitre
    const char *end_hint_last;

    const char *settings_title;
    const char *settings_dir_label;
    const char *settings_filter_label;
    const char *settings_filter_off;
    const char *settings_filter_fmt;   // "Niveau %d / %d"
    const char *settings_footer;
    const char *picker_title;
    const char *picker_footer;
    const char *picker_footer_cancel;
    const char *search_header;
    const char *search_label;          // "Recherche"
    const char *search_clear_hint;
    const char *search_none;
    const char *search_unavailable;

    // Légende des touches (bibliothèque), une ligne par touche.
    const char *legend_open;
    const char *legend_search;
    const char *legend_settings;
    const char *legend_back;
    const char *legend_quit;
} UIStrings;

// Détecte la langue configurée sur la console (français ou anglais par
// défaut pour tout le reste, vu qu'on ne gère que ces deux langues).
Language detect_system_language(void);

// Retourne la table de textes correspondant à la langue donnée.
const UIStrings *get_ui_strings(Language lang);
