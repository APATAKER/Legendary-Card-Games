// Board-mat geometry (world units). Purely visual guides + snap targets.
import type { Rect, Zone } from './types';

export const CARD_W = 150;
export const CARD_H = 210;

const SW = CARD_W + 20; // slot width
const SH = CARD_H + 20; // slot height
const G = 22; // gap between columns
const X0 = 60;
const STEP = SW + G;
const ROW_GAP = 74;

const col = (i: number) => X0 + i * STEP;
const span = (n: number) => n * SW + (n - 1) * G;

const R1 = 110;
const R2 = R1 + SH + ROW_GAP;
const R3 = R2 + SH + ROW_GAP;
const PLAYER_Y0 = R3 + SH + 120;
const PLAYER_STEP = SH + 110;

export const MAT_WIDTH = col(9) + SW + X0;

function slot(id: string, label: string, c: number, y: number, tone: Zone['tone'] = 'neutral'): Zone {
  return { id, label, kind: 'slot', x: col(c), y, w: SW, h: SH, tone };
}
function area(id: string, label: string, c: number, cols: number, y: number, tone: Zone['tone'] = 'neutral'): Zone {
  return { id, label, kind: 'area', x: col(c), y, w: span(cols), h: SH, tone };
}

export function playerMatY(p: number) {
  return PLAYER_Y0 + p * PLAYER_STEP;
}

export function buildZones(playerMats: number): Zone[] {
  const z: Zone[] = [
    // Row 1: the Wilds
    area('combat', 'Combat Zone', 0, 3, R1, 'danger'),
    slot('hills', 'Hills', 3, R1, 'wilds'),
    slot('ruins', 'Ruins', 4, R1, 'wilds'),
    slot('river', 'River', 5, R1, 'wilds'),
    slot('graveyard', 'Graveyard', 6, R1, 'wilds'),
    slot('underground', 'Underground', 7, R1, 'wilds'),
    slot('enemy', 'Enemy Deck', 8, R1, 'danger'),
    slot('enemy-discard', 'Enemy Discard', 9, R1, 'danger'),
    // Row 2
    slot('location', 'Location', 0, R2),
    slot('objectives', 'Objectives', 1, R2),
    area('operations', 'Operations', 2, 2, R2),
    area('staging-a', 'Staging', 4, 3, R2),
    slot('kill', 'Kill Pile', 7, R2),
    slot('strikes', 'Strike Deck', 8, R2, 'danger'),
    slot('strike-discard', 'Strike Discard', 9, R2, 'danger'),
    // Row 3: HQ
    area('staging-b', 'Staging', 0, 3, R3),
    slot('hq-1', 'HQ', 3, R3, 'hq'),
    slot('hq-2', 'HQ', 4, R3, 'hq'),
    slot('hq-3', 'HQ', 5, R3, 'hq'),
    slot('hq-4', 'HQ', 6, R3, 'hq'),
    slot('hq-5', 'HQ', 7, R3, 'hq'),
    slot('barracks', 'Barracks', 8, R3, 'hq'),
    slot('commanders', 'Commanders', 9, R3, 'hq'),
  ];
  for (let p = 0; p < playerMats; p++) {
    const y = playerMatY(p);
    const n = p + 1;
    z.push(
      slot(`p${n}-avatar`, 'Avatar', 0, y, 'player'),
      slot(`p${n}-deck`, 'Deck', 1, y, 'player'),
      slot(`p${n}-discard`, 'Discard', 2, y, 'player'),
      area(`p${n}-play`, 'Played Cards', 3, 4, y, 'player'),
      area(`p${n}-strikes`, 'Strikes Taken', 7, 2, y, 'player'),
      slot(`p${n}-extra`, 'Trophies / Marks', 9, y, 'player'),
    );
  }
  return z;
}

/** Player-mat backing panels (drawn behind that player's zones). */
export function playerPanels(playerMats: number): (Rect & { n: number })[] {
  return Array.from({ length: playerMats }, (_, p) => ({
    n: p + 1,
    x: X0 - 24,
    y: playerMatY(p) - 52,
    w: MAT_WIDTH - 2 * (X0 - 24),
    h: SH + 76,
  }));
}

export function matBounds(playerMats: number): Rect {
  const bottom = playerMats > 0 ? playerMatY(playerMats - 1) + SH + 60 : R3 + SH + 60;
  return { x: 0, y: 0, w: MAT_WIDTH, h: bottom };
}

/** Top-left position that centers a card inside a zone slot. */
export function slotCardPos(z: Zone) {
  return { x: z.x + (z.w - CARD_W) / 2, y: z.y + (z.h - CARD_H) / 2 };
}
