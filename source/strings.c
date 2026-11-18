#include "strings.h"

#include <switch.h>
#include <stdio.h>

static const char *const k_controls_fr[] = {
    "#Bibliothèque",
    "A : ouvrir une œuvre",
    "Haut/Bas/Gauche/Droite : naviguer",
    "X : épingler / désépingler",
    "Y : rechercher par titre",
    "B : dossier parent / effacer la recherche",
    "- : réglages",
    "+ : quitter",
    "#Liste des chapitres",
    "A : lire le chapitre",
    "X : marquer lu / non lu",
    "Haut/Bas (maintenir) : défilement rapide",
    "B : retour à la bibliothèque",
    "\f",
    "#Lecture",
    "L / R : page précédente / suivante",
    "Stick gauche : déplacer l'image",
    "Stick droit : zoomer",
    "R3 : réinitialiser le zoom",
    "Y : mode page / défilement",
    "X : afficher / masquer la barre",
    "- : portrait / paysage",
    "B : retour à la liste",
    "Tactile : swipe = changer de page",
    "Tactile : pincer = zoomer",
    "Tactile : barre du bas = aller à une page",
    "#Fin de chapitre",
    "A / R : chapitre suivant     L : rester",
};

static const char *const k_controls_en[] = {
    "#Library",
    "A: open a series",
    "D-pad/Left stick: navigate",
    "X: pin / unpin",
    "Y: search by title",
    "B: parent folder / clear search",
    "-: settings",
    "+: quit",
    "#Chapter list",
    "A: read the chapter",
    "X: mark read / unread",
    "Up/Down (hold): fast scroll",
    "B: back to library",
    "\f",
    "#Reading",
    "L / R: previous / next page",
    "Left stick: move the image",
    "Right stick: zoom",
    "R3: reset zoom",
    "Y: page / scroll mode",
    "X: show / hide the bar",
    "-: portrait / landscape",
    "B: back to the list",
    "Touch: swipe = change page",
    "Touch: pinch = zoom",
    "Touch: bottom bar = jump to a page",
    "#End of chapter",
    "A / R: next chapter     L: stay",
};

static const UIStrings k_strings_fr = {
    .menu_library = "BIBLIOTHÈQUE",

    .no_files_msg = "Aucun fichier .cbz/.cbr/.zip/.rar ni dossier ici.",
    .root_footer = "A: ouvrir   Y: chercher   -: réglages   B: parent   +: quitter",
    .parent_quit_footer = "B: dossier parent   +: quitter",

    .detail_empty_msg = "Ce dossier est vide.",
    .detail_footer = "A: ouvrir   X: marquer lu/non lu   B: dossier parent   Haut/Bas: naviguer   +: quitter",

    .progress_done = "Lu",
    .progress_in_progress_fmt = "En cours (p. %d/%d)",

    .reader_error_page = "Impossible de charger cette page.",
    .reader_hint_page = "L/R/swipe: page   Pincer/stick droit: zoom   B: retour",
    .reader_hint_strip = "Stick/doigt: défiler   B: retour",
    .mode_label_page = "Mode: Page (Y)",
    .mode_label_strip = "Mode: Défilement (Y)",
    .page_counter_fmt = "Page %d / %d",

    .end_title_next = "Fin du chapitre",
    .end_hint_next = "A / R: chapitre suivant   L: rester   B: liste",
    .end_title_last = "Dernier chapitre terminé",
    .end_hint_last = "A / R: retour à la liste   L: rester",

    .settings_title = "RÉGLAGES",
    .settings_dir_label = "Dossier des comics",
    .settings_filter_label = "Filtre chaud",
    .settings_filter_off = "Désactivé",
    .settings_filter_fmt = "Niveau %d / %d",
    .settings_footer = "Haut/Bas: choisir   A: modifier   Gauche/Droite: ajuster   B: retour",
    .picker_title = "Choisis ton dossier de comics",
    .picker_footer = "A: entrer   B: dossier parent   +: choisir ce dossier",
    .picker_footer_cancel = "A: entrer   B: parent   +: choisir ce dossier   -: annuler",
    .search_header = "Rechercher une œuvre",
    .search_label = "Recherche",
    .search_clear_hint = "B: effacer",
    .search_none = "Aucun résultat pour cette recherche.",
    .search_unavailable = "Clavier indisponible dans ce mode de lancement.",

    .legend_open = "A : ouvrir",
    .legend_search = "Y : chercher",
    .legend_pin = "X : épingler",

    .settings_controls_label = "Touches",
    .settings_controls_hint = "Afficher toutes les touches",
    .controls_title = "TOUCHES",
    .controls_footer = "B: retour",
    .controls_lines = k_controls_fr,
    .controls_line_count = sizeof(k_controls_fr) / sizeof(k_controls_fr[0]),
    .legend_settings = "- : réglages",
    .legend_back = "B : dossier parent",
    .legend_quit = "+ : quitter",
};

static const UIStrings k_strings_en = {
    .menu_library = "LIBRARY",

    .no_files_msg = "No .cbz/.cbr/.zip/.rar files or folders here.",
    .root_footer = "A: open   Y: search   -: settings   B: parent   +: quit",
    .parent_quit_footer = "B: parent folder   +: quit",

    .detail_empty_msg = "This folder is empty.",
    .detail_footer = "A: open   X: mark read/unread   B: parent folder   Up/Down: navigate   +: quit",

    .progress_done = "Read",
    .progress_in_progress_fmt = "In progress (p. %d/%d)",

    .reader_error_page = "Unable to load this page.",
    .reader_hint_page = "L/R/swipe: page   Pinch/right stick: zoom   B: back",
    .reader_hint_strip = "Stick/finger: scroll   B: back",
    .mode_label_page = "Mode: Page (Y)",
    .mode_label_strip = "Mode: Scroll (Y)",
    .page_counter_fmt = "Page %d / %d",

    .end_title_next = "End of chapter",
    .end_hint_next = "A / R: next chapter   L: stay   B: list",
    .end_title_last = "Last chapter finished",
    .end_hint_last = "A / R: back to list   L: stay",

    .settings_title = "SETTINGS",
    .settings_dir_label = "Comics folder",
    .settings_filter_label = "Warm filter",
    .settings_filter_off = "Off",
    .settings_filter_fmt = "Level %d / %d",
    .settings_footer = "Up/Down: select   A: change   Left/Right: adjust   B: back",
    .picker_title = "Choose your comics folder",
    .picker_footer = "A: enter   B: parent folder   +: pick this folder",
    .picker_footer_cancel = "A: enter   B: parent   +: pick this folder   -: cancel",
    .search_header = "Search a series",
    .search_label = "Search",
    .search_clear_hint = "B: clear",
    .search_none = "No results for this search.",
    .search_unavailable = "Keyboard unavailable in this launch mode.",

    .legend_open = "A: open",
    .legend_search = "Y: search",
    .legend_pin = "X: pin",

    .settings_controls_label = "Controls",
    .settings_controls_hint = "Show all controls",
    .controls_title = "CONTROLS",
    .controls_footer = "B: back",
    .controls_lines = k_controls_en,
    .controls_line_count = sizeof(k_controls_en) / sizeof(k_controls_en[0]),
    .legend_settings = "-: settings",
    .legend_back = "B: parent folder",
    .legend_quit = "+: quit",
};

const UIStrings *get_ui_strings(Language lang) {
    return (lang == LANG_FR) ? &k_strings_fr : &k_strings_en;
}

Language detect_system_language(void) {
    Language result = LANG_EN; // repli par défaut si la détection échoue

    Result rc = setInitialize();
    if (R_FAILED(rc)) {
        printf("setInitialize a échoué: 0x%x\n", rc);
        return result;
    }

    u64 language_code = 0;
    rc = setGetSystemLanguage(&language_code);
    if (R_SUCCEEDED(rc)) {
        SetLanguage lang = SetLanguage_ENUS;
        rc = setMakeLanguage(language_code, &lang);
        if (R_SUCCEEDED(rc)) {
            if (lang == SetLanguage_FR || lang == SetLanguage_FRCA) {
                result = LANG_FR;
            } else {
                result = LANG_EN;
            }
        } else {
            printf("setMakeLanguage a échoué: 0x%x\n", rc);
        }
    } else {
        printf("setGetSystemLanguage a échoué: 0x%x\n", rc);
    }

    setExit();
    return result;
}
