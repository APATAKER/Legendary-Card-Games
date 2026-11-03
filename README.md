# Legendary Card Games

A fan-made digital tabletop and print-and-play toolkit for **Legendary Encounters: Predator** (Upper Deck, 2015).

The main deliverable is a **native Windows game** built with C++20, OpenGL, SDL3 and Dear ImGui. It puts every physical card on a zoomable virtual table and runs the official rulebook turn flow on top. It supports 1–5 players, both movies, and both factions. Alongside the game, the repo holds the card data pipeline and the print sheet generators used to build it. An earlier React prototype is kept in `archive/`.

> [!IMPORTANT]
> This is an unofficial, non-commercial fan project. It is not affiliated with Upper Deck or 20th Century Studios. **No card art, rulebook PDFs or verbatim rules text are committed to this repository.** You fetch them locally with the scripts below, and they stay git-ignored.

---

## Contents

- [Features](#features)
- [Repository layout](#repository-layout)
- [Getting started](#getting-started)
- [Playing the game](#playing-the-game)
- [Print-and-play pipeline](#print-and-play-pipeline)
- [Archived React prototype](#archived-react-prototype)
- [Data files](#data-files)
- [Changelog](#changelog)

---

## Features

### Native desktop game (`game/`)

- **Whole-box tabletop.** All 331 distinct card printings (700 physical cards) are on a pannable, zoomable table. You can drag, stack, split, flip, rotate and shuffle cards, use snap-to-slot drop zones, multi-select piles, and have animated lift and flip feedback.
- **Rulebook turn engine.** It follows the four-step turn sequence: Enemy Phase, Action Phase, Strike Phase and Cleanup. It also handles Recruit, Scan the Wilds, Fight, Coordinate, the Objective stack (Expendable Assets → Flares/Frags → *Get to the Choppa!*), Victory and Defeat.
- **New mission setup wizard.** Choose *Predator* (1987) or *Predator 2* (1990), play as Humans (co-op) or Predators (Hunter Mode), and pick 1–5 players with unique, faction-valid Avatars.
- **Multiplayer boards.** Each player gets their own non-overlapping 7-zone board: Avatar, Strikes, Marks, Played, Hand, Deck and Discard. Turns rotate between players, and the game handles Coordinate and player elimination.
- **Wilds mechanics.** Attach cards, Guerrilla Camp, location hazards and enemy push-through into the Combat Zone are all modelled.
- **Tactical HUD.** It shows live turn, phase, objective, health, strikes, marks and the recruit (`*`) and attack (`///`) pools. Every value can be adjusted by hand.
- **Undo/redo and replayable state.** Every manual move or rule action is a checkpointed `TableState`.
- **Movement telemetry.** Every move is logged in-game (`L`) and written to `game/logs/movement_log.{jsonl,txt}`.
- **Card inspect and catalog.** You can open a full-screen 3D card inspect view, search inside a stack, and browse the box catalog to spawn any card.
- **Automated rules check.** `--verify-mechanics` runs deterministic multiplayer and Attach/Guerrilla Camp scenarios and writes a PASS/FAIL report.

### Card data and print pipeline (`predator/`, `tools/`)

- **Card downloader.** It fetches the card images from the official product page and records where each one came from in a manifest.
- **Card quantities.** It works out how many copies of each card are in the 700-card box, using a strict "never guess" rule: every number is cross-checked against the rulebook's Sorting Guide and Game Contents and the official card list.
- **Offline reviewer.** A browser page for auditing and editing those quantities.
- **Print sheets.** It generates A4 sheets at 300 DPI with 9 poker-size (2.5″ × 3.5″) cards per page and cutting guides, in colour and high-contrast B/W.
- **Game asset builder.** It stitches the game mat from the rulebook spread, defines every board zone, and downscales the card textures for the GPU.

---

## Repository layout

```text
Legendary-Card-Games/
├── game/                       Native C++20 desktop game
│   ├── src/
│   │   ├── main.cpp            Entry point (--smoke-test, --verify-mechanics)
│   │   ├── app/                Window, input, ImGui overlay (HUD, wizard, modals)
│   │   ├── game/               Table model + rule engine, setup, assets, camera, verification
│   │   ├── gfx/                Batched 2D renderer, shaders, texture cache (stb_image)
│   │   └── core/               Math + logging helpers
│   ├── data/catalog.json       Curated card definitions & box groups (source of truth)
│   ├── CMakeLists.txt          CMake ≥ 3.25 project (target: LegendaryEncounters)
│   ├── CMakePresets.json       Ninja Multi-Config + MSVC + vcpkg preset
│   ├── vcpkg.json              SDL3, glad, imgui, stb, nlohmann-json
│   └── build.ps1               One-shot build/run script
├── predator/                   Card data & print-and-play pipeline
│   ├── scripts/                download / quantities / reviewer / print-sheet generators
│   └── data/                   manifest.csv, card_quantities.csv, group_summary.csv
├── tools/
│   ├── build_assets.py         Generates game/assets/ (board, layout, textures, card back)
│   └── rules/                  Rulebook text extraction & lookup helpers
├── archive/app-react/          Archived React + Vite web prototype (superseded)
├── CHANGELOG.md
└── README.md
```

Git-ignored locally-generated folders: `predator/assets/`, `predator/output/`, `predator/source/`, `game/assets/`, `game/build/`, `game/logs/`.

---

## Getting started

### Prerequisites

| Tool | Notes |
| --- | --- |
| Windows 10/11 x64 | The game is currently Windows-only (MSVC + `WIN32` subsystem). |
| Visual Studio 2026 Build Tools | Needs the **Desktop development with C++** workload (MSVC x64, CMake, Ninja, vcpkg). |
| vcpkg | `VCPKG_ROOT` must be set. `build.ps1` falls back to the copy bundled with Visual Studio. |
| Python 3.10+ | Run `py -m pip install pillow pypdf` |
| GPU with OpenGL 4.6 | On hybrid laptops the discrete GPU is requested automatically. |

### 1. Fetch the source material (one time)

Download the official **Legendary Encounters: Predator** rulebook from Upper Deck and save it as:

```text
predator/Legendary_Encounters_Rules-Predator.pdf
```

Then download the card images. This step needs network access, uses only the standard library, and can be re-run safely:

```powershell
py predator/scripts/download_cards.py
```

This fills `predator/assets/raw/` with the card images in folders, and writes `predator/data/manifest.csv`.

### 2. Build the game assets

```powershell
py tools/build_assets.py            # add --force to regenerate everything
```

This writes `game/assets/`: `board.png`, `board_layout.json`, `catalog.json`, `card_back.png` and `cards/**`. `build.ps1` runs this step for you if the assets are missing.

### 3. Build and run

```powershell
cd game
./build.ps1                # Debug build
./build.ps1 -Run           # Debug build, then launch
./build.ps1 Release -Run   # Optimised build, then launch
./build.ps1 -Clean         # wipe build/ first
```

The executable is written to `game/build/<Config>/LegendaryEncounters.exe`. If you prefer to drive CMake yourself, run it from a VS developer shell:

```powershell
cmake --preset windows-x64
cmake --build --preset release
```

### Command-line options

| Flag | Effect |
| --- | --- |
| `--smoke-test` | Renders 15 frames and exits. Useful for CI and as a quick check after building. |
| `--verify-mechanics [report.txt]` | Runs the automated rules checks headless. It writes the report (default `logs/verify_mechanics.txt`) and exits with `0` only if every check passes. |

---

## Playing the game

When the game starts, the **New Mission Setup** wizard opens. Pick the movie, faction, player count and Avatars there. After that, either double-click cards to take context-sensitive actions, or press `Enter` to move through the phases.

### Mouse

| Input | Action |
| --- | --- |
| Left-drag a card | Pull the top card. Toggle `M` to drag the whole stack by default. |
| `Shift` + left-drag | Drag the whole pile |
| `Alt` while dropping | Drop freely without snapping to a slot |
| `Ctrl` + click | Add the pile to the selection or remove it |
| Double-click | Context action. In the Enemy Phase on the Prey deck it draws an Enemy. In the Action Phase it plays a card from your hand, recruits from the HQ or Commanders, scans a face-down Wilds card, or fights a face-up enemy. Otherwise it inspects a face-up card or flips a face-down one. |
| Right-click a card | Context menu: move to a player zone, play, coordinate, inspect, browse, flip, shuffle, rotate |
| Middle-drag / right-drag empty space / `Space` + drag | Pan the camera |
| Mouse wheel | Zoom at the cursor |

### Keyboard

| Key | Action |
| --- | --- |
| `Enter` / `N` | Go to the next phase or step |
| `A` | Play all cards in hand (Action Phase) |
| `F` / `Shift+F` | Flip the top card / flip the whole pile |
| `R` | Rotate 90° |
| `S` | Shuffle the pile under the cursor |
| `I` | Inspect the card, or browse a face-down stack |
| `G` | Stack all selected piles together |
| `M` | Switch whole-stack drag mode on or off |
| `L` | Show or hide the movement log |
| `F2` | Show or hide the board zone overlay |
| `Ctrl+Z` / `Ctrl+Y` / `Ctrl+Shift+Z` | Undo / Redo |
| `0` / `Home` | Fit the main board on screen |
| `1` / `2` | Fit all player boards / fit the box shelf |
| `Alt+1`, `Alt+2`, `3`–`5` | Fit the board of player 1–5 |
| `Esc` | Close the inspect view, browser, catalog or wizard, or clear the selection |

---

## Print-and-play pipeline

These steps need the card art in `predator/assets/raw/` (see [step 1](#1-fetch-the-source-material-one-time)).

```powershell
py predator/scripts/build_quantities.py        # -> data/card_quantities.csv + data/group_summary.csv
py predator/scripts/generate_reviewer_html.py  # -> predator/review.html (offline audit/editor)
py predator/scripts/generate_test_sheet.py     # -> output/test_sheet_a4.{pdf,png} (printer calibration)
py predator/scripts/generate_printable_cards.py
```

`generate_printable_cards.py` produces the following in `predator/output/`:

| File | Contents |
| --- | --- |
| `predator_cards_color_700.pdf` | All 700 cards, full colour, 78 A4 pages |
| `predator_cards_bw_700.pdf` | All 700 cards, high-contrast greyscale |
| `predator_alien_conversion_kit_{color,bw}.pdf` | 16-card Alien Conversion Kit (a web extra that is not part of the box) |
| `preview_{color,bw}_page1.png` | Quick visual check of page 1 |

Every sheet is A4 at 300 DPI, with a 3 × 3 grid of 2.5″ × 3.5″ cards, hairline cutting guides and a sheet/index footer.

To extract the rulebook text for writing rules (optional):

```powershell
py tools/rules/extract_rules_text.py   # -> predator/data/rules_extracted.json (git-ignored)
py tools/rules/inspect_rules.py        # dump pages 13–15
py tools/rules/find_spaces.py          # grep starting-deck / experience lines
```

---

## Archived React prototype

`archive/app-react/` holds the first, browser-based version, built with React 19, Vite 8, TypeScript and Zustand. It was replaced by the native game on 2026-10-20. It has two modes:

- **Tabletop sandbox** (default): a free-form virtual table with a box drawer, hand tray, search window and hotkeys.
- **Classic engine** (`?classic`): an earlier rules engine that runs the game automatically.

To run it:

```powershell
cd archive/app-react
cmd /c mklink /J public\assets ..\..\predator\assets   # serve full-res art
npm install
node scripts/build-catalog.mjs                         # thumbnails + catalog.generated.ts
npm run dev
```

---

## Data files

| File | Description |
| --- | --- |
| `predator/data/manifest.csv` | Every downloaded image: section, sub-section, filename, URL, size and hash |
| `predator/data/card_quantities.csv` | Copies per image, with source and `CONFIRMED`/`UNCONFIRMED` status |
| `predator/data/group_summary.csv` | Rulebook totals for each group, cross-checked against the image counts |
| `game/data/catalog.json` | The game's card catalog: definitions, box groups, sections and per-card metadata (cost, attack, recruit, class, team, keywords) |

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the full project history.

## License

No open-source license has been chosen yet. The code is © the repository owner. *Legendary Encounters*, *Predator* and all card artwork and text are © their respective rights holders (Upper Deck, 20th Century Studios). They are used here only for personal, non-commercial play.
