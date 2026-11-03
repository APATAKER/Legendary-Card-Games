# Archived: React web prototype

> [!WARNING]
> This prototype is **archived**. It was replaced by the native C++ game in [`game/`](../../game)
> on 2026-10-20 and is kept for reference only. See the root [README](../../README.md) and
> [CHANGELOG](../../CHANGELOG.md).

A browser version of **Legendary Encounters: Predator**, built with React 19, Vite 8,
TypeScript and Zustand.

| Mode | URL | Source |
| --- | --- | --- |
| Tabletop sandbox (default) | `/` | `src/table/` |
| Classic automated engine | `/?classic` | `src/App.tsx`, `src/engine/gameEngine.ts` |

## Running

```powershell
cmd /c mklink /J public\assets ..\..\predator\assets   # full-res card art (git-ignored)
npm install
node scripts/build-catalog.mjs                         # public/thumbs + src/table/catalog.generated.ts
npm run dev
```

`build-catalog.mjs` reads `../../predator/data/card_quantities.csv` and needs the card art
in `predator/assets/raw/` (fetch it with `predator/scripts/download_cards.py`).
