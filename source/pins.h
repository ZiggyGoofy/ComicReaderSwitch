#pragma once

#include <stdbool.h>

// Œuvres épinglées de la bibliothèque, identifiées par leur nom (dossier ou
// fichier à la racine). Sauvegardées dans /switch/ComicReaderSwitch/pins.txt.

// Charge la liste depuis la SD (à appeler une fois au démarrage).
void pins_load(void);

bool pins_is_pinned(const char *name);

// Épingle ou désépingle, et sauvegarde immédiatement. Retourne true si
// l'œuvre est épinglée après l'appel.
bool pins_toggle(const char *name);
