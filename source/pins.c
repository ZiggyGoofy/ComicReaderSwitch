#include "pins.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define PINS_DIR "/switch/ComicReaderSwitch"
#define PINS_PATH "/switch/ComicReaderSwitch/pins.txt"
#define PINS_MAX 128
#define PIN_NAME_MAX 256

static char g_pins[PINS_MAX][PIN_NAME_MAX];
static int g_pin_count = 0;

static int pins_find(const char *name) {
    for (int i = 0; i < g_pin_count; i++) {
        if (strcmp(g_pins[i], name) == 0) return i;
    }
    return -1;
}

static void pins_save(void) {
    if (mkdir(PINS_DIR, 0777) != 0 && errno != EEXIST) return;

    FILE *f = fopen(PINS_PATH, "w");
    if (!f) {
        printf("Impossible d'écrire %s\n", PINS_PATH);
        return;
    }
    for (int i = 0; i < g_pin_count; i++) {
        fprintf(f, "%s\n", g_pins[i]);
    }
    fclose(f);
}

void pins_load(void) {
    g_pin_count = 0;
    FILE *f = fopen(PINS_PATH, "r");
    if (!f) return;

    char line[PIN_NAME_MAX + 2];
    while (g_pin_count < PINS_MAX && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || pins_find(line) >= 0) continue;
        strncpy(g_pins[g_pin_count], line, PIN_NAME_MAX - 1);
        g_pins[g_pin_count][PIN_NAME_MAX - 1] = '\0';
        g_pin_count++;
    }
    fclose(f);
}

bool pins_is_pinned(const char *name) {
    return pins_find(name) >= 0;
}

bool pins_toggle(const char *name) {
    int idx = pins_find(name);
    if (idx >= 0) {
        for (int i = idx; i < g_pin_count - 1; i++) {
            memcpy(g_pins[i], g_pins[i + 1], PIN_NAME_MAX);
        }
        g_pin_count--;
        pins_save();
        return false;
    }

    if (g_pin_count >= PINS_MAX) return false; // liste pleine
    strncpy(g_pins[g_pin_count], name, PIN_NAME_MAX - 1);
    g_pins[g_pin_count][PIN_NAME_MAX - 1] = '\0';
    g_pin_count++;
    pins_save();
    return true;
}
