---
name: bug-fix-workflow
description: >-
  Standardized end-to-end workflow for triaging, fixing, verifying, documenting,
  and committing in-game reported bugs (BUG-XXXX) in Legendary Encounters: Predator.
  Activate this skill whenever the user reports new bugs, asks to fix open bugs
  in game/logs/bug_reports.{md,jsonl}, or asks to run the bug-fix process.
---

# Bug-Fix Process (`Legendary Encounters: Predator`)

Follow this 7-stage process strictly for every bug report (`BUG-XXXX`). Never skip verification, asset synchronization, `CHANGELOG.md` updates, or the pre-commit user approval gate.

---

## Stage 1: Triage & Root-Cause Discovery

1. **Inspect the Issue Tracker**:
   - Read `game/logs/bug_reports.md` and `game/logs/bug_reports.jsonl` to identify all `"OPEN"` bug reports (`BUG-XXXX`).
   - Review the **User Notes**, **Tactical State Snapshot**, **Board Zones Snapshot**, and **Recent Move History (15 events)** captured at the moment the bug occurred.
2. **Code Discovery via Knowledge Graph**:
   - Use `codebase-memory-mcp` tools (`search_graph`, `get_code_snippet`, `trace_path`, `search_code`) to locate the relevant methods in `game/src/game/Table.cpp`, `game/src/app/Overlay.cpp`, and `game/src/app/App.cpp`.
3. **Full Sibling Group / Act Scope Expansion**:
   - Whenever a bug report reveals that a card or an Objective Act is unimplemented or missing metadata in `game/data/catalog.json` / `Table.cpp`, **do not** patch only the single reported card.
   - Inspect the card artwork in `game/assets/cards/<section>/<group>/` for **all sibling cards in that same character group or Objective Act**, enrich their full metadata in `game/data/catalog.json`, and implement their rules/abilities together so subsequent draws in the same scenario do not immediately trigger follow-up bugs.

---

## Stage 2: Implementation & Runtime Asset Sync

1. **Catalog Source of Truth & Runtime Sync**:
   - Edit card metadata in `game/data/catalog.json`.
   - **CRITICAL**: The executable loads `game/assets/catalog.json` at runtime (not `game/data/catalog.json`). Whenever `game/data/catalog.json` is modified, immediately copy it to `game/assets/catalog.json` (or run `py tools/build_assets.py`).
2. **Engine & UI Implementation**:
   - Implement rule logic and state transitions in `game/src/game/Table.h` and `game/src/game/Table.cpp`.
   - Implement modals, HUD indicators, text wrapping, and board badges in `game/src/app/App.h`, `game/src/app/App.cpp`, and `game/src/app/Overlay.cpp`.

---

## Stage 3: Automated Regression Tests (`Verify.cpp`)

1. Add deterministic regression tests in `game/src/game/Verify.cpp` under the Reported Bug Regression Suite.
2. Label every `check(...)` assertion clearly with the bug code (e.g., `"BUG-0005 (...): ..."`), covering:
   - The exact failure condition reported by the user.
   - Any newly enriched/implemented sibling cards or Objective Act mechanics.

---

## Stage 4: Build & Headless Verification Gate

1. **Compile**:
   ```powershell
   ./build.ps1
   ```
   (Run from `game/` or invoke `powershell -ExecutionPolicy Bypass -File game/build.ps1`.)
2. **Run Mechanics Verification**:
   ```powershell
   ./game/build/Debug/LegendaryEncounters.exe --verify-mechanics
   ```
   Confirm `0 FAILED` in the output and in `logs/verify_mechanics.txt`.
3. **Run Smoke Test**:
   ```powershell
   ./game/build/Debug/LegendaryEncounters.exe --smoke-test
   ```
   Confirm a clean `0` exit code.

---

## Stage 5: Issue Tracker Resolution & `CHANGELOG.md` Update (Mandatory)

1. **Mark Bug(s) `RESOLVED`**:
   - Update `"status": "OPEN"` to `"status": "RESOLVED"` in `game/logs/bug_reports.jsonl`.
   - Update `game/logs/bug_reports.md`:
     - Increment `Resolved` count and decrement `Open` count in the header.
     - Change `` `OPEN` `` to `` `RESOLVED` `` in both the summary table row and the detailed `## [BUG-XXXX]` section header.
2. **Update `CHANGELOG.md` (Never Skip)**:
    - Under `## [Unreleased]` in root `CHANGELOG.md`, add or update entries under `### Fixed`, `### Added`, or `### Changed`.
    - **Entries are grouped by date**: use a `### YYYY-MM-DD` sub-heading (the day the work was done, from the system clock / `git log --date=short`) with `#### Added` / `#### Changed` / `#### Fixed` beneath it. Add to today's heading if it exists, otherwise create it above the previous date, so progress over time is visible.
    - Explicitly cite the `BUG-XXXX` ID(s), card names/IDs, and mechanics fixed or added.
3. **False-positive / working-as-intended reports**:
    - If the card text or rulebook shows the behaviour is correct, confirm with the user (do not guess), then mark the report `RESOLVED` with a `### Resolution` note (and a `resolution` field in the `.jsonl`) stating *Not a bug — working as intended* and why.
    - Still log it in `CHANGELOG.md` (`Fixed` entry: "closed as not a bug") and add a `BUG-XXXX (Working as intended)` regression check if cheap; improve UI/log text if the behaviour was confusing.

---

## Stage 6: User Testing & Approval Gate (Pre-Commit)

1. **Stop Before Committing**:
   - Present a concise summary of the root cause, the fix, the `--verify-mechanics` pass count, and the `CHANGELOG.md` updates.
   - **Wait for the user to test in-game and explicitly approve** before running `git commit`.

---

## Stage 7: Atomic Git Commit

1. Once the user approves:
   - Read and follow `C:\Users\dhilip\.gemini\skills\commit-message-generator.md` for the commit message format (`<type>(<scope>): <summary>`).
   - Use the user's selected Git identity (`Personal`: `--author="APATAKER <therealthelip@gmail.com>"` or `Work`: `--author="dhilipraja-ultiweb <dhilipraja.nagarajan@ultiweb.co.in>"`).
   - Create clean, atomic commit(s) for each resolved bug (or tightly coupled bug group) with its `Verify.cpp` regression test and `CHANGELOG.md` entry included.
