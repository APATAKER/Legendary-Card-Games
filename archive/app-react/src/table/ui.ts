// Ephemeral UI state (camera, drag, hover, menus). Never saved, never undoable.
import { create } from 'zustand';
import type { Item, Source, Target } from './store';
import type { Rect } from './types';

export interface View {
  x: number;
  y: number;
  zoom: number;
}

export type DropHover =
  | { kind: 'pile'; id: string }
  | { kind: 'zone'; id: string }
  | { kind: 'hand'; index: number }
  | { kind: 'box' }
  | { kind: 'table' };

export interface DragState {
  source: Source;
  items: Item[]; // positions (x,y) are world coords at drag start
  dx: number; // world-space offset applied to every item
  dy: number;
  tilt: number;
  hover: DropHover | null;
  target: Target | null;
  hiddenPiles: string[]; // whole piles being carried (hidden on the table)
  lift: { pileId: string; count: number } | null; // top cards lifted off a pile
  fromScreen: boolean; // came from hand / box / search (not from the table)
}

export type HoverRef = { kind: 'pile'; id: string } | { kind: 'hand'; index: number } | { kind: 'def'; def: number; faceUp: boolean };

export type MenuState =
  | { kind: 'pile'; x: number; y: number; id: string }
  | { kind: 'selection'; x: number; y: number }
  | { kind: 'table'; x: number; y: number }
  | { kind: 'hand'; x: number; y: number; index: number };

interface UI {
  view: View;
  drag: DragState | null;
  hover: HoverRef | null;
  menu: MenuState | null;
  inspect: number | null; // CardDef id shown full screen
  searchPile: string | null;
  selection: string[];
  marquee: Rect | null;
  boxOpen: boolean;
  trayOpen: boolean;
  previewOn: boolean;
  helpOpen: boolean;
  spaceDown: boolean;
  fx: Record<string, { kind: 'shuffle' | 'flip' | 'bump'; n: number }>;
  toast: { msg: string; n: number } | null;
  set: (p: Partial<UI>) => void;
  play: (pileId: string, kind: 'shuffle' | 'flip' | 'bump') => void;
  say: (msg: string) => void;
}

const firstVisit = typeof localStorage !== 'undefined' && !localStorage.getItem('legendary-table-seen-help');

export const useUI = create<UI>((set, get) => ({
  view: { x: 40, y: 60, zoom: 0.5 },
  drag: null,
  hover: null,
  menu: null,
  inspect: null,
  searchPile: null,
  selection: [],
  marquee: null,
  boxOpen: true,
  trayOpen: true,
  previewOn: true,
  helpOpen: firstVisit,
  spaceDown: false,
  fx: {},
  toast: null,
  set: (p) => set(p),
  play: (pileId, kind) => set({ fx: { ...get().fx, [pileId]: { kind, n: (get().fx[pileId]?.n ?? 0) + 1 } } }),
  say: (msg) => set({ toast: { msg, n: (get().toast?.n ?? 0) + 1 } }),
}));

export const screenToWorld = (sx: number, sy: number, v: View = useUI.getState().view) => ({
  x: (sx - v.x) / v.zoom,
  y: (sy - v.y) / v.zoom,
});
