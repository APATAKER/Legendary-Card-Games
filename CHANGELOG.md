# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> [!NOTE]
> The project was developed locally before it was put under version control. The history
> before `0.1.0` was reconstructed from file timestamps and source contents, and is grouped
> into dated milestones.

## [Unreleased]

### Added
- `README.md` with an overview, setup and build steps, controls, the print pipeline and a
  data file reference.
- `CHANGELOG.md` (this file).
- Root `.gitignore` covering build trees, `node_modules`, generated assets and print PDFs,
  runtime logs, and copyrighted source material (card art, rulebook/card-list PDFs,
  extracted rules text).

### Changed
- Moved the root-level scratch scripts into `tools/rules/`:
  - `scratch_extract.py` → `tools/rules/extract_rules_text.py`
  - `inspect_rules.py` → `tools/rules/inspect_rules.py`
  - `find_spaces.py` → `tools/rules/find_spaces.py`
- The rules helpers now resolve paths from the repository root instead of the current
  working directory, so they can be run from anywhere.
- Replaced the Vite template README in `archive/app-react/` with notes about the prototype.

### Fixed
- `archive/app-react/scripts/build-catalog.mjs` now finds `predator/` again. The path
  broke when the app was moved into `archive/`.

## [0.1.0] - 2026-11-01

The first playable version of the native Windows game, **Legendary Encounters: Predator**
(`game/`, C++20 / OpenGL / SDL3 / Dear ImGui).

### Added
- **Official turn engine** (`Table.cpp`): the rulebook's four steps (Enemy, Action, Strike,
  Cleanup) plus Victory and Defeat states.
  - Actions: Play Card, Play All, Coordinate, Recruit (HQ and Commanders), Scan the Wilds,
    Fight Enemy, and Complete Objective 2 (spend 6 Recruit).
  - Objective stack: Expendable Assets → Flares/Frags/Claymores → *Get to the Choppa!*
  - Enemy deck reshuffle tracking. Defeat when every player is dead or the enemy deck runs
    out twice.
  - Played-card abilities based on class, team and character already played this turn.
- **Wilds mechanics**: Attach keyword, Guerrilla Camp reveal and attach, location hazards,
  enemies pushed from a full Wilds space into the Combat Zone, scan costs per space, and
  enemy attack modifiers.
- **Multiplayer**: 1–5 players, each with a separate, non-overlapping 7-zone board (Avatar,
  Strikes, Marks, Played, Hand, Deck, Discard) built at runtime. Turns rotate, eliminated
  players are skipped, and Coordinate lends a virtual copy of a card to another player.
- **New Mission Setup wizard**: *Predator* (1987) or *Predator 2* (1990), Humans (co-op) or
  Predators (Hunter Mode), 1–5 players with unique, faction-valid Avatars. Printed Health and
  Defense values are included for all 15 Avatars.
- **Tactical HUD**: turn, phase, objective, health, strikes, marks and the recruit and
  attack pools, all editable by hand. Includes Draw 1, Shuffle Discard → Deck and phase
  buttons.
- **Movement and telemetry log**: an in-game window (`L`) with a filter. Every drag, drop,
  stack, flip, shuffle and rule action is recorded to `game/logs/movement_log.jsonl` and
  `.txt`.
- **Card interaction**: a context menu for sending piles to player zones, a full-screen 3D
  inspect modal, a stack browser (pull to hand or table, flip all), and a box catalog of all
  331 printings that can spawn any card.
- **Multi-select**: `Ctrl`+click piles, then press `G` to stack them or drag them together.
  `M` switches whole-stack dragging on and off.
- **`--verify-mechanics [report]`**: a headless rules check. It covers 1–5 player tray
  layout, unique Avatars, turn rotation, Coordinate, elimination, and Attach / Guerrilla Camp
  Wilds rules. It writes a PASS/FAIL report and returns a non-zero exit code on failure.
- **`--smoke-test`**: renders 15 frames and exits.
- **Rich card metadata** in `game/data/catalog.json` (title, subtitle, rarity, cost, attack,
  recruit, class, team, keywords, rules text) for the cards that drive the rules.

### Foundation (2026-10-20)
- New native project: CMake ≥ 3.25 with a Ninja Multi-Config / MSVC / vcpkg preset. vcpkg
  manifest with SDL3, glad, imgui (SDL3 and OpenGL3 bindings), stb and nlohmann-json.
- `build.ps1` enters the VS 2026 Build Tools developer shell automatically, builds the
  assets if they are missing, then configures, builds and optionally runs the game.
- Batched `Renderer2D`, a shader wrapper, and a `TextureCache` that loads images with
  stb_image.
- Pan and zoom `Camera`, and an asset directory lookup that checks `<exe>/assets` before
  the source tree.
- A table model in which every card is a pile, with split, merge, flip, rotate and shuffle,
  snap-to-slot drop targets, undo and redo, and animated lift, flip and merge effects.
- The game asks hybrid-GPU laptops for the discrete NVIDIA or AMD GPU.
- **`tools/build_assets.py`**: stitches the game mat from rulebook pages 3 and 4, defines
  every board slot and area in `board_layout.json`, downscales the card art to 1024 px
  textures, and crops the card back from the mat.

### Changed
- The React web app is no longer the main client. It was moved to `archive/app-react/` on
  2026-10-20 and replaced by the native game.

---

## Pre-release milestones

### 2026-10-27 — Archived React classic engine updates
#### Changed
- Updated the card definitions (`src/data/cards.ts`), the rules engine
  (`src/engine/gameEngine.ts`) and the shared types (`src/types/game.ts`) in the archived
  React prototype.

### 2026-10-19 — React tabletop sandbox
#### Added
- A free-form **tabletop mode**, now the default view in the React app (`src/table/`). It
  uses a Zustand store, a mat with zones, drag and drop, and piles. It has a box drawer, a
  hand tray, a context menu, an inspect overlay, a search window, a toolbar and hotkeys.
- `scripts/build-catalog.mjs` generates `catalog.generated.ts` from `card_quantities.csv`,
  and creates 480 px WebP thumbnails with `sharp`.
- The earlier automated engine is still available at `?classic`.

### 2026-10-14 — React prototype and rules extraction
#### Added
- **React 19 + Vite + TypeScript web app** with an automated game engine (`gameEngine.ts`)
  and card data (`cards.ts`). Board components: Combat Zone, HQ/Barracks, Objective panel,
  Wilderness track. Also a player area, card view, card inspect modal, combat log, game-over
  modal, sound effects (`audio.ts`) and confetti on victory.
- Rulebook text extraction (`predator/data/rules_extracted.json`) and the lookup scripts,
  used to check starting decks and setup rules.

### 2026-10-13 — Card quantities and print-ready sheets
#### Added
- `build_quantities.py`: works out the copies of each card in the 700-card box, using a
  strict "never guess" rule. Every count is cross-checked against the rulebook Sorting Guide
  (p.2), the Game Contents (p.30) and the official card list PDF. Writes
  `card_quantities.csv` and `group_summary.csv`.
- `generate_reviewer_html.py`: an offline HTML reviewer with group filters, a running total
  towards 700, editable quantities and CSV export.
- `generate_printable_cards.py`: print-ready A4 PDFs at 300 DPI, 9 poker-size cards per
  sheet with hairline cut guides. Full colour and high-contrast B/W versions of all 700
  cards (78 pages each), plus the 16-card Alien Conversion Kit.

### 2026-10-11 — Card acquisition
#### Added
- `download_cards.py`: downloads all 331 card images from the official Legendary Encounters:
  Predator page. Uses only the standard library and can be re-run safely. Files go into
  folders matching the page's sections, and `manifest.csv` records each one (dimensions,
  bytes, SHA-256, URL).
- `generate_test_sheet.py`: an A4 calibration sheet with a 3×3 grid of poker-size cards.
- Added the official rulebook and the All Card List PDF as reference sources.

[Unreleased]: https://github.com/APATAKER/Legendary-Card-Games/commits/main
