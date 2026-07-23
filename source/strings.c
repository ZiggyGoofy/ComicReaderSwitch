#include "strings.h"

#include <switch.h>
#include <stdio.h>

static const UIStrings k_strings_fr = {
    .menu_library = "BIBLIOTHÈQUE",

    .no_files_msg = "Aucun fichier .cbz/.cbr ni dossier ici.",
    .root_footer = "A: ouvrir   B: dossier parent   Stick/D-pad: naviguer   +: quitter",
    .parent_quit_footer = "B: dossier parent   +: quitter",

    .detail_empty_msg = "Ce dossier est vide.",
    .detail_footer = "A: ouvrir   B: dossier parent   Haut/Bas: naviguer   +: quitter",

    .progress_done = "Lu",
    .progress_in_progress_fmt = "En cours (p. %d/%d)",

    .reader_error_page = "Impossible de charger cette page.",
    .reader_hint_page = "L/R/swipe: page   Pincer/stick droit: zoom   B: retour",
    .reader_hint_strip = "Stick/doigt: défiler   B: retour",
    .mode_label_page = "Mode: Page (Y)",
    .mode_label_strip = "Mode: Défilement (Y)",
    .page_counter_fmt = "Page %d / %d",
};

static const UIStrings k_strings_en = {
    .menu_library = "LIBRARY",

    .no_files_msg = "No .cbz/.cbr files or folders here.",
    .root_footer = "A: open   B: parent folder   Stick/D-pad: navigate   +: quit",
    .parent_quit_footer = "B: parent folder   +: quit",

    .detail_empty_msg = "This folder is empty.",
    .detail_footer = "A: open   B: parent folder   Up/Down: navigate   +: quit",

    .progress_done = "Read",
    .progress_in_progress_fmt = "In progress (p. %d/%d)",

    .reader_error_page = "Unable to load this page.",
    .reader_hint_page = "L/R/swipe: page   Pinch/right stick: zoom   B: back",
    .reader_hint_strip = "Stick/finger: scroll   B: back",
    .mode_label_page = "Mode: Page (Y)",
    .mode_label_strip = "Mode: Scroll (Y)",
    .page_counter_fmt = "Page %d / %d",
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
