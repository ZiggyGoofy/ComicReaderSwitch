#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_DIR "/switch/ComicReaderSwitch"
#define CONFIG_PATH "/switch/ComicReaderSwitch/config.txt"

bool config_load_comics_dir(char *out, size_t out_size) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return false;

    // Format simple : une seule ligne "comics_dir=<chemin>".
    char line[600];
    bool found = false;
    if (fgets(line, sizeof(line), f)) {
        const char *prefix = "comics_dir=";
        size_t prefix_len = strlen(prefix);
        if (strncmp(line, prefix, prefix_len) == 0) {
            // Retire le retour à la ligne final s'il y en a un.
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            strncpy(out, line + prefix_len, out_size - 1);
            out[out_size - 1] = '\0';
            found = (out[0] != '\0');
        }
    }
    fclose(f);
    return found;
}

bool config_save_comics_dir(const char *path) {
    if (mkdir(CONFIG_DIR, 0777) != 0 && errno != EEXIST) {
        printf("Impossible de créer %s (errno=%d)\n", CONFIG_DIR, errno);
        return false;
    }

    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        printf("Impossible d'écrire la configuration dans %s\n", CONFIG_PATH);
        return false;
    }

    fprintf(f, "comics_dir=%s\n", path);
    fclose(f);
    return true;
}
