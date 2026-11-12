# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> [!NOTE]
> The project was developed locally before it was put under version control. The history
> before `0.1.0` was reconstructed from file timestamps and source contents, and is grouped
> into dated milestones.

## [Unreleased]

### 2026-11-13

#### Added
- **Standardized Bug-Fix Workflow Skill** (`.agents/skills/bug-fix-workflow/SKILL.md`): codified the
  7-stage end-to-end process for triaging, scoping (enriching full sibling card groups/Acts),
  implementing, regression-testing (`Verify.cpp` + `--verify-mechanics` + `--smoke-test`),
  resolving `bug_reports.{jsonl,md}`, updating `CHANGELOG.md` (with dated entries), and pausing for
  user approval before committing.
- **Predator 2 Act 2 (*Personal Little War*) & Objective 2/3 Event mechanics** (`catalog.json`,
  `Table.cpp`, `App.cpp`, `Overlay.cpp`, `Verify.cpp`) (`BUG-0006`):
  - Enriched authentic catalog metadata and rules text for Predator 2 Act 2 cards (`#129`–`#137`:
    the three Clues, `#132 Shish Kebab`, `#133 This Is Dread. Truly Dread.`,
    `#134 Able To Bend Light`, `#135 City Of Fear`, `#136 Event`, `#137 Hazard!`).
  - **Clue pseudo-piles in Operations (`testing`)**: Clues revealed in The Wilds go to the
    **Found Clues** pile on the left (Blue `0x3b82f6` outline + `FOUND CLUES (X)` badge); Clues
    entering the Combat Zone go to the **Lost Clues** pile on the right (Red `0xef4444` outline +
    `LOST CLUES (Y)` badge). When the 3rd Clue enters Operations, each living player draws cards
    equal to `Found Clues`, all Clues are discarded, and Objective 2 advances to Objective 3.
  - **Objective 2 Event (*A New Player in Town*)**: draws 2 Strikes immediately when
    `Found Clues == 0`, otherwise opens `PersonalWarEventModal` (move 1 Found Clue to Lost, or draw
    2 Strikes).
  - **Act 2 Enemies**: `#132 Shish Kebab` (player attachment, start-of-Strike-Phase strike,
    exclusive fight restriction, reassignment on player death), `#133 This Is Dread. Truly Dread.`
    (Death modal: pay `4 *` Recruit or draw 3 Strikes), `#134 Able To Bend Light` (Marked Wilds
    fight restriction), `#135 City Of Fear` (Ongoing: Coordinating draws a Strike).
  - **Objective 3 Event (*Remarkable Weaponry*)**: revealed Event becomes an Event Mark beneath the
    active player's Avatar, discardable for `6 *` Recruit (HUD button).
  - Regression tests in `Verify.cpp` (`PART 7`).

#### Changed
- **Lead the Assault result text** (`Overlay.cpp`, `BUG-0007`): the `LeadAssaultModal` now states the
  rule explicitly (`Success needs: Total Attack >= Total Recruit`) and notes that an Event in
  Operations is discarded instead of completing the Objective (*Where the Hell is Special
  Weapons?!?*), so the outcome is self-explanatory.

#### Fixed
- **Global UI & modal text wrapping (`BUG-0004`)**: enforced `ImGui::PushTextWrapPos(0.0f)`,
  `ImGui::TextWrapped` and viewport-clamped window sizes across all ability modals (including
  `LeadAssaultModal`), the Tactical HUD, Move Log rows, Bug Report previews and Status Bar toasts.
- **Illegal Free Scan on Coordinate (`BUG-0005`)**: removed the erroneous Free Scan grant from
  `#67 Knocking on Doors` and `#85 Close Quarters` in `Table::syncStateFromZones()`, and capped the
  `#65 In the Shadows` Avoid Strike bonus at 1 per turn.
- **Objective-aware Event dispatch (`BUG-0006`)**: Predator 2 Events now execute the active
  Objective's Event (*A New Player in Town* in Act 2, *Remarkable Weaponry* in Act 3) instead of
  falling through to Predator 1's *Right Out From Under Our Noses* ambush.
- **`BUG-0007` closed as *not a bug* (working as intended)**: `#124 Assault The Stronghold` succeeds
  when total printed Attack is **at least** total printed Recruit (`2 /// vs 0 *` is a success), and
  a success discards an Event in Operations instead of completing the Objective. No rule change.

### 2026-11-12

#### Added
- **In-game Bug Reporter & Diagnostic Snapshot Engine** (`App.h`, `App.cpp`, `Overlay.cpp`,
  `Table.cpp`, commit `11addd9`): `[B] Report Bug` modal (HUD bar, `B` key, card context menu and
  Move Log rows) that auto-captures a Tactical State Snapshot, a Board Zones Snapshot and recent
  Move Log history, persisted to `game/logs/bug_reports.jsonl` and `game/logs/bug_reports.md`.
- **Predator 2 Barracks Heroes catalog & mechanics** (`catalog.json`, `Table.cpp`, `Overlay.cpp`,
  `Verify.cpp`, commit `a5b4b19`):
  - Enriched rules text, costs, Recruit/Attack stats, classes, teams and keywords for all 16
    Predator 2 Barracks hero printings (`#16`–`#31`: `Danny`, `Harrigan`, `Keyes`, `Lambert`).
  - Implemented the **Showboat** keyword (`#17`, `#21`, `#29`) and the active, passive and modal
    abilities across the Predator 2 Barracks cards, with regression tests (`PART 5`).
- **Predator 2 Act 1 (*War Zone*) catalog & mechanics** (`catalog.json`, `Table.cpp`,
  `Overlay.cpp`, `Verify.cpp`, commit `e9ae680`):
  - Enriched metadata for Act 1 cards (`#124`–`#128`) and Objective 1 (`#99 War Zone`).
  - `#124 Assault The Stronghold` moves to Operations on reveal and unlocks the once-per-turn
    **Lead the Assault** action and result modal (`BUG-0002`).
  - `#125 Scorpion Enforcer` Death trigger (reveal top card, optionally discard) and
    `#126 El Scorpio` Ongoing Strike, plus the Objective 1 Event (*Where the Hell is Special
    Weapons?!?*), with regression tests (`PART 6`).

#### Fixed
- **Predator 2 HQ 0-cost recruitment (`BUG-0001`, `BUG-0003`)**: populated authentic Recruit costs
  and printed stats for Predator 2 Barracks heroes so HQ recruiting deducts Recruit points.
- **Predator 2 Objective 1 Goal reveal & action (`BUG-0002`)**: `#124 Assault The Stronghold` moves
  to Operations when revealed and enables Lead the Assault.

## [0.2.0] - 2026-11-05

### Added
- **Auto-available hand stats & on-spend auto-playing**: Recruit (`*`) and Attack (`///`) from
  cards in the active player's hand are immediately available in the Action Phase without
  requiring "Play All" first. Spending Recruit or Attack automatically plays the needed cards
  from hand, prioritising 0-cost Starters (`Experience`, `Brute Strength`) first.
- **Optional card ability engine & interactive choice modals** (`Table.cpp`, `Overlay.cpp`):
  - **Choose Card to Kill modal** (`Stick Around`, `Running the Streets`, `Nothing On Earth`,
    `A Fitting Demise`): optional or mandatory card-kill picker. Cards killed by player
    abilities remain in the Played Area with a crimson `KILLED (-> Dead)` badge and outline,
    retaining their Recruit, Attack, Class and Team for the turn before moving to
    `dead_characters` at Cleanup.
  - **Heal Strike modal** (`Sexual Tyrannosaurus`, `I See You`, `Sources and Contacts`,
    `Crafty Veteran`): interactive Strike picker across players, enforcing the `Open Wound`
    heal-first restriction, the least-damage rule, and `Permanent Damage` replacement draws.
  - **Sneak Peek modal** (`Pushing Pencils`): privately inspect any face-down Wilds card
    without triggering its Reveal effect, then choose to keep it hidden or scan it face-up for
    free.
  - **Switch Two Wilds Cards modal** (`Same Kind of Jungle`): swap any two cards in The Wilds
    (`wilds_0`..`wilds_4`, excluding Combat Zone and attached Goals).
  - **Player Picker & Discard modal** (`Ol' Painless Is Waiting`, `I Can Get Even`): choose a
    living player to draw 2 and discard 2 chosen hand cards, or grant `Coordinate` to all
    cards in a chosen player's hand this turn.
  - **Role card `Call for Backup` modals**: `The Highest Tech` (swap printed Recruit `*` and
    Attack `///` on a card for the turn, with a `SWAPPED` badge), `Position and Situation`
    (place a card from Discard on top of the Deck), and `Partners in Crime` (gain a Commander
    or an HQ card costing 3 `*` or less).
  - **Interactive Strike modals**: `Sacrifice` (5 Damage — keep on target or redirect to any
    living teammate with fatal-damage warnings) and `Cover Me!` (4 Damage — teammates select
    Attack cards from hand totalling 4+ `///` to discard and cancel the Strike).
  - **`Like A Hunter` reveal modal**: shows each player's top-3 deck reveal results (Cost 1+
    discarded vs. Cost 0 Starters kept on top of the deck).
- **Tactical HUD & on-board badges**:
  - **Expected Strikes This Turn** indicator showing raw strikes, Avoid Strike reduction and
    net strikes, plus a **Free Scans Ready** counter.
  - **Optional Card Abilities Ready** panel in the HUD, plus `ABILITY READY`,
    `KILLED (-> Dead)` and `SWAPPED` on-card world badges and context-menu / 3D-inspect
    activation buttons.
- **Pile-to-pile flying card animations** (`FlyingCardAnim`): cubic ease-out arc, lift shadow,
  stagger delay and mid-flight 3D flip squash whenever cards move between decks, hands, Wilds,
  Combat Zone and discard piles.
- **Multi-turn & Objective 3 mechanics**:
  - `If It Bleeds, We Can Kill It` (Dutch Rare): stationed beside the player's Avatar across
    2 turns (`cannotDieTurnsRemaining = 2`), preventing death until discarded at the end of
    the next turn.
  - `The Choppa!` (id 114): boarded in the Combat Zone by spending 3 `*` Recruit
    (`Choppa: 3 *`), attaching the player's Avatar beneath `The Choppa!` (`escapedInChoppa`)
    for Minor Victory.
  - `Three Hundred City Blocks` (id 115): attaches beneath the Final Enemy (`One Ugly Mother`,
    id 121) and requires spending 4 `*` Recruit when defeating it.
  - `No Time to Bleed` (id 118): fought using Recruit (`*`) when outside the River.
- **Expanded `--verify-mechanics` suite** (`Verify.cpp`): added Part 3 (Issues A1–A9
  playthrough discrepancy fixes) and Part 4 (B-level & card-by-card review mechanics).
- `README.md` with an overview, setup and build steps, controls, the print pipeline and a
  data file reference.
- `CHANGELOG.md` (this file).
- Root `.gitignore` covering build trees, `node_modules`, generated assets and print PDFs,
  runtime logs, and copyrighted source material (card art, rulebook/card-list PDFs,
  extracted rules text).

### Changed
- **`[Class]` and `[Team]` trigger threshold (Issue A1)**: tracks per-round classes and teams
  (`roundHandClasses`, `roundHandTeams`) across hand, played area and coordinated cards;
  `[Class]` and `[Team]` abilities require at least 2 matching cards in the turn and trigger
  once per qualifying card.
- **`Brothers in Arms` scaling**: scales dynamically (0–3 triggers) with the number of *other*
  distinct Barracks characters (`Blain`, `Dillon`, `Dutch`, `Mac`) present across active turn
  cards.
- Moved the root-level scratch scripts into `tools/rules/`:
  - `scratch_extract.py` → `tools/rules/extract_rules_text.py`
  - `inspect_rules.py` → `tools/rules/inspect_rules.py`
  - `find_spaces.py` → `tools/rules/find_spaces.py`
- The rules helpers now resolve paths from the repository root instead of the current
  working directory, so they can be run from anywhere.
- Replaced the Vite template README in `archive/app-react/` with notes about the prototype.

### Fixed
- **Special Strike card rules (Issues A2–A8)**:
  - `Open Wound` (id 194): deals 0 Damage and prevents other Strikes on that player from being
    healed until `Open Wound` is healed first.
  - `Permanent Damage` (id 190): deals 3 Damage, heals to `discard_strikes`, and draws a
    replacement Strike when healed.
  - `Cover Me!` (id 191): deals 4 Damage and can be cancelled by teammates discarding 4+
    Attack from hand (without refilling their hands).
  - `Sacrifice` (id 192): deals 5 Damage, prompts to choose any living player, and eliminates
    the chosen player if their HP drops to 0 or below.
  - `Festering Wound` (id 193): deals 0 initial Damage, then discards at the start of the
    next Enemy Phase to draw 2 new Strikes.
  - `A Whole New Level Of Pain` (id 187): deals 1 Damage and eliminates all other
    non-immune players if its holder dies.
  - `Stunning Blow` (id 186): deals 1 Damage and draws 1 extra Strike for each Mark on the
    receiving player.
- **Catalog class icon & metadata audit (`game/data/catalog.json`, Issue A9)**: corrected
  `Strength` (Green Fist), `Survival` (Red), `Intel` (Yellow) and `Leadership` (Blue) class
  mappings and rules text across Barracks heroes (`Blain`, `Dillon`, `Dutch`, `Mac`),
  Avatars/Roles (`CIA Agent`, `Detective`, `Guerrilla`, `SWAT Officer`, `City Hunter`),
  Young Blood (`209`, `211`), Commanders (`226`, `229`), Killer Instincts (`231`, `233`) and
  Strikes (`Open Wound`).
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
