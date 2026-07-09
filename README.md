# 📚 ComicReader

**A local CBZ/CBR comic reader for Nintendo Switch homebrew** — built for reading your own comic collection directly from the SD card, no internet required.

[🇬🇧 English](#-english) · [🇫🇷 Français](#-français)

---

## 🇬🇧 English

### ✨ Features

- 📖 **CBZ & CBR support** via `libarchive` — read your comics straight from ZIP or RAR archives
- 🖼️ **Library view** — browse your collection as a grid of cover thumbnails, generated from each comic's first page and cached to the SD card for instant loading afterwards
- 🔍 **Zoom & pan** — right stick to zoom (1×–4×), left stick to pan around when zoomed in, click the right stick to reset instantly
- 📌 **Reading progress tracking** — automatically resumes where you left off, and shows `En cours (p. X/Y)` / `Lu` (in progress / finished) right on the library tiles
- 🗂️ **Nested folders welcome** — organize your comics in subfolders (e.g. one folder per series); folder covers automatically borrow the first page of the first comic found inside
- ⚡ Built with `libnx` + `SDL2` — runs on any homebrew-capable Switch (developed & tested on Switch Lite)

### 🎮 Controls

| Screen | Button | Action |
|---|---|---|
| Library | D-Pad / Left stick | Navigate the grid |
| Library | **A** | Open selected comic or folder |
| Library | **B** | Go to parent folder |
| Reader | **L** / **R** | Previous / next page |
| Reader | Right stick | Zoom in / out |
| Reader | Left stick | Pan (when zoomed) |
| Reader | Right stick click | Reset zoom & pan |
| Reader | **B** | Back to library |
| Anywhere | **+** | Quit |

### 📥 Installation (just want to use it?)

1. Make sure your Switch is running a custom firmware (e.g. Atmosphère) with the Homebrew Menu set up. *This project doesn't help you set that part up — plenty of guides exist elsewhere for it.*
2. Copy `ComicReader.nro` to `/switch/ComicReader/` on your SD card.
3. Create a folder for your comics: `/switch/ComicReader/comics/` (subfolders allowed).
4. Drop your `.cbz`/`.cbr` files (or organize them in subfolders) inside.
5. Launch **ComicReader** from the Homebrew Menu. 🎉

> Only `.jpg`/`.jpeg`/`.png` pages are currently decoded inside archives. `.gif`/`.webp` support isn't wired up yet.

### 🛠️ Building from source

**Prerequisites:** [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `devkitA64` + `switch-dev`, on Windows via MSYS2.

```bash
pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx \
          switch-zlib switch-libarchive switch-libjpeg-turbo switch-libpng \
          switch-freetype switch-harfbuzz switch-libwebp switch-liblzma \
          switch-libzstd switch-lz4 switch-bzip2
```

Then:

```bash
git clone https://github.com/ZiggyGoofy/ComicReaderSwitch.git
cd ComicReaderSwitch
make
```

This produces `ComicReader.nro`, ready to copy to your SD card.

### 📂 Project structure

```
ComicReaderSwitch/
├── Makefile
├── icon.jpg              # Homebrew Menu icon
├── source/
│   ├── main.c            # main loop, input, rendering states
│   ├── file_browser.c/h  # SD card navigation & filtering
│   ├── archive.c/h       # CBZ/CBR reading via libarchive
│   ├── thumbnail.c/h     # cover generation & disk cache
│   └── progress.c/h      # per-comic reading progress tracking
```

### 🗺️ Roadmap / known limitations

- [ ] Custom icon on the Switch Home Menu (NSP forwarder)
- [ ] `.gif` / `.webp` page support
- [ ] Sort/filter options in the library (by name, recent, unread)
- [ ] More thorough `.cbr` (RAR) testing across real-world files

### 🙏 Built with

[devkitPro](https://devkitpro.org/) · [libnx](https://github.com/switchbrew/libnx) · [SDL2](https://www.libsdl.org/) · [libarchive](https://www.libarchive.org/) · [FreeType](https://freetype.org/) / [HarfBuzz](https://harfbuzz.github.io/)

---

## 🇫🇷 Français

### ✨ Fonctionnalités

- 📖 **Support CBZ & CBR** via `libarchive` — lecture directe des archives ZIP ou RAR
- 🖼️ **Vue bibliothèque** — parcours de la collection sous forme de grille de couvertures, générées à partir de la première page de chaque comic et mises en cache sur la carte SD pour un chargement instantané ensuite
- 🔍 **Zoom & déplacement** — stick droit pour zoomer (1×–4×), stick gauche pour se déplacer dans l'image zoomée, clic sur le stick droit pour réinitialiser instantanément
- 📌 **Suivi de lecture** — reprise automatique à la page où tu t'es arrêté, avec l'affichage `En cours (p. X/Y)` / `Lu` directement sur les vignettes de la bibliothèque
- 🗂️ **Sous-dossiers bienvenus** — organise tes comics en sous-dossiers (un par série par exemple) ; la couverture d'un dossier reprend automatiquement la première page du premier comic trouvé à l'intérieur
- ⚡ Développé avec `libnx` + `SDL2` — fonctionne sur toute Switch compatible homebrew (développé et testé sur Switch Lite)

### 🎮 Contrôles

| Écran | Bouton | Action |
|---|---|---|
| Bibliothèque | D-pad / Stick gauche | Naviguer dans la grille |
| Bibliothèque | **A** | Ouvrir le comic ou dossier sélectionné |
| Bibliothèque | **B** | Remonter au dossier parent |
| Lecture | **L** / **R** | Page précédente / suivante |
| Lecture | Stick droit | Zoomer / dézoomer |
| Lecture | Stick gauche | Se déplacer (si zoomé) |
| Lecture | Clic stick droit | Réinitialiser le zoom |
| Lecture | **B** | Retour à la bibliothèque |
| Partout | **+** | Quitter |

### 📥 Installation (juste pour l'utiliser)

1. Ta Switch doit déjà tourner sous custom firmware (ex : Atmosphère) avec le Homebrew Menu configuré. *Ce projet ne couvre pas cette étape — de nombreux guides existent déjà ailleurs pour ça.*
2. Copie `ComicReader.nro` dans `/switch/ComicReader/` sur ta carte SD.
3. Crée un dossier pour tes comics : `/switch/ComicReader/comics/` (les sous-dossiers sont acceptés).
4. Dépose tes fichiers `.cbz`/`.cbr` (ou organise-les en sous-dossiers) à l'intérieur.
5. Lance **ComicReader** depuis le Homebrew Menu. 🎉

> Seules les pages `.jpg`/`.jpeg`/`.png` sont décodées dans les archives pour l'instant. Le support `.gif`/`.webp` n'est pas encore branché.

### 🛠️ Compiler depuis les sources

**Prérequis :** [devkitPro](https://devkitpro.org/wiki/Getting_Started) avec `devkitA64` + `switch-dev`, via MSYS2 sous Windows.

```bash
pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx \
          switch-zlib switch-libarchive switch-libjpeg-turbo switch-libpng \
          switch-freetype switch-harfbuzz switch-libwebp switch-liblzma \
          switch-libzstd switch-lz4 switch-bzip2
```

Puis :

```bash
git clone https://github.com/ZiggyGoofy/ComicReaderSwitch.git
cd ComicReaderSwitch
make
```

Ça génère `ComicReader.nro`, prêt à copier sur la carte SD.

### 📂 Structure du projet

```
ComicReaderSwitch/
├── Makefile
├── icon.jpg              # icône du Homebrew Menu
├── source/
│   ├── main.c            # boucle principale, entrées, rendu des états
│   ├── file_browser.c/h  # navigation et filtrage sur la carte SD
│   ├── archive.c/h       # lecture CBZ/CBR via libarchive
│   ├── thumbnail.c/h     # génération et cache disque des couvertures
│   └── progress.c/h      # suivi de progression de lecture par comic
```

### 🗺️ Feuille de route / limitations connues

- [ ] Icône personnalisée sur le menu d'accueil de la Switch (forwarder NSP)
- [ ] Support des pages `.gif` / `.webp`
- [ ] Options de tri/filtre dans la bibliothèque (par nom, récents, non lus)
- [ ] Tests plus poussés du `.cbr` (RAR) sur des fichiers réels variés

### 🙏 Réalisé avec

[devkitPro](https://devkitpro.org/) · [libnx](https://github.com/switchbrew/libnx) · [SDL2](https://www.libsdl.org/) · [libarchive](https://www.libarchive.org/) · [FreeType](https://freetype.org/) / [HarfBuzz](https://harfbuzz.github.io/)
