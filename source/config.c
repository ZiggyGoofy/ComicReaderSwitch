#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_DIR "/switch/ComicReaderSwitch"
#define CONFIG_PATH "/switch/ComicReaderSwitch/config.txt"

// Format : une ligne "clé=valeur" par réglage (comics_dir, warm_filter).
// On relit puis réécrit tout le fichier à chaque sauvegarde pour ne jamais
// perdre un réglage en en modifiant un autre.
typedef struct {
    char comics_dir[600];
    int warm_filter;
} Config;

static void config_read(Config *c) {
    c->comics_dir[0] = '\0';
    c->warm_filter = 0;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;

    char line[700];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "comics_dir=", 11) == 0) {
            strncpy(c->comics_dir, line + 11, sizeof(c->comics_dir) - 1);
            c->comics_dir[sizeof(c->comics_dir) - 1] = '\0';
        } else if (strncmp(line, "warm_filter=", 12) == 0) {
            c->warm_filter = atoi(line + 12);
        }
    }
    fclose(f);
}

static bool config_write(const Config *c) {
    if (mkdir(CONFIG_DIR, 0777) != 0 && errno != EEXIST) {
        printf("Impossible de créer %s (errno=%d)\n", CONFIG_DIR, errno);
        return false;
    }

    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        printf("Impossible d'écrire la configuration dans %s\n", CONFIG_PATH);
        return false;
    }

    if (c->comics_dir[0] != '\0') fprintf(f, "comics_dir=%s\n", c->comics_dir);
    fprintf(f, "warm_filter=%d\n", c->warm_filter);
    fclose(f);
    return true;
}

bool config_load_comics_dir(char *out, size_t out_size) {
    Config c;
    config_read(&c);
    if (c.comics_dir[0] == '\0') return false;
    strncpy(out, c.comics_dir, out_size - 1);
    out[out_size - 1] = '\0';
    return true;
}

bool config_save_comics_dir(const char *path) {
    Config c;
    config_read(&c);
    strncpy(c.comics_dir, path, sizeof(c.comics_dir) - 1);
    c.comics_dir[sizeof(c.comics_dir) - 1] = '\0';
    return config_write(&c);
}

int config_load_warm_filter(void) {
    Config c;
    config_read(&c);
    return c.warm_filter;
}

bool config_save_warm_filter(int level) {
    Config c;
    config_read(&c);
    c.warm_filter = level;
    return config_write(&c);
}
