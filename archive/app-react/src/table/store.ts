// Table state: every physical action you can do with cards, with undo/redo + autosave.
// There are deliberately NO game rules here.
import { create } from 'zustand';
import { CARDS, GROUPS } from './catalog.generated';
import { CARD_W } from './layout';
import { origins } from './fx';
import type { CardInst, Hand, Pile, TableState } from './types';

const SAVE_KEY = 'legendary-table-v1';
const HISTORY_LIMIT = 150;

export const GROUP_BY_KEY = new Map(GROUPS.map((g) => [g.key, g]));

let idCounter = 0;
export const newId = (prefix = 'p') => `${prefix}${Date.now().toString(36)}${(idCounter++).toString(36)}`;

// ---------------------------------------------------------------- initial state
function freshBox(): Record<string, CardInst[]> {
  const box: Record<string, CardInst[]> = {};
  for (const g of GROUPS) {
    box[g.key] = [];
    for (const defId of g.cards) {
      const d = CARDS[defId];
      for (let i = 0; i < d.qty; i++) box[g.key].push({ uid: `c${defId}_${i}`, def: defId, faceUp: false });
    }
  }
  return box;
}

export function freshState(playerMats = 1): TableState {
  return {
    version: 1,
    piles: [],
    hands: Array.from({ length: playerMats }, (_, i) => ({ id: `h${i + 1}`, name: `Player ${i + 1}`, cards: [] })),
    activeHand: 0,
    box: freshBox(),
    playerMats,
  };
}

function loadSaved(): TableState | null {
  try {
    const raw = localStorage.getItem(SAVE_KEY);
    if (!raw) return null;
    return validate(JSON.parse(raw));
  } catch {
    return null;
  }
}

export function validate(s: unknown): TableState | null {
  const t = s as TableState;
  if (!t || t.version !== 1 || !Array.isArray(t.piles) || !Array.isArray(t.hands) || typeof t.box !== 'object') return null;
  return t;
}

// ---------------------------------------------------------------- move model
/** Where cards are picked up from. */
export type Source =
  | { kind: 'pile'; pileId: string; count: number } // top `count` cards (>= length means the whole pile)
  | { kind: 'piles'; pileIds: string[] } // several whole piles (a selection)
  | { kind: 'pileCard'; pileId: string; uid: string } // one specific card from inside a pile (search)
  | { kind: 'hand'; handIdx: number; index: number }
  | { kind: 'handAll'; handIdx: number }
  | { kind: 'box'; group: string; def?: number }; // whole box group, or one copy of a card

/** Where they're put down. */
export type Target =
  | { kind: 'table'; placements: { x: number; y: number; id?: string }[] } // one placement per moved item
  | { kind: 'pile'; pileId: string }
  | { kind: 'hand'; handIdx: number; index: number }
  | { kind: 'box' };

/** A bundle of cards that travels together (keeps its pile identity if it was a whole pile). */
export interface Item {
  cards: CardInst[];
  fromPileId?: string;
  rot: 0 | 90;
  label?: string;
  x?: number;
  y?: number;
}

/** Remove the source cards from state. Returns the new state and the items picked up. */
export function take(s: TableState, src: Source): { s: TableState; items: Item[] } | null {
  switch (src.kind) {
    case 'pile': {
      const p = s.piles.find((q) => q.id === src.pileId);
      if (!p) return null;
      if (src.count >= p.cards.length) {
        return {
          s: { ...s, piles: s.piles.filter((q) => q.id !== p.id) },
          items: [{ cards: p.cards, fromPileId: p.id, rot: p.rot, label: p.label, x: p.x, y: p.y }],
        };
      }
      const keep = p.cards.slice(0, p.cards.length - src.count);
      const moved = p.cards.slice(p.cards.length - src.count);
      return {
        s: { ...s, piles: s.piles.map((q) => (q.id === p.id ? { ...q, cards: keep } : q)) },
        items: [{ cards: moved, rot: p.rot, x: p.x, y: p.y }],
      };
    }
    case 'piles': {
      const set = new Set(src.pileIds);
      const picked = s.piles.filter((q) => set.has(q.id));
      if (!picked.length) return null;
      return {
        s: { ...s, piles: s.piles.filter((q) => !set.has(q.id)) },
        items: picked.map((p) => ({ cards: p.cards, fromPileId: p.id, rot: p.rot, label: p.label, x: p.x, y: p.y })),
      };
    }
    case 'pileCard': {
      const p = s.piles.find((q) => q.id === src.pileId);
      const c = p?.cards.find((k) => k.uid === src.uid);
      if (!p || !c) return null;
      const rest = p.cards.filter((k) => k.uid !== src.uid);
      const piles = rest.length ? s.piles.map((q) => (q.id === p.id ? { ...q, cards: rest } : q)) : s.piles.filter((q) => q.id !== p.id);
      return { s: { ...s, piles }, items: [{ cards: [c], rot: 0 }] };
    }
    case 'hand': {
      const h = s.hands[src.handIdx];
      const c = h?.cards[src.index];
      if (!c) return null;
      const cards = h.cards.filter((_, i) => i !== src.index);
      return { s: { ...s, hands: s.hands.map((x, i) => (i === src.handIdx ? { ...x, cards } : x)) }, items: [{ cards: [c], rot: 0 }] };
    }
    case 'handAll': {
      const h = s.hands[src.handIdx];
      if (!h || !h.cards.length) return null;
      return { s: { ...s, hands: s.hands.map((x, i) => (i === src.handIdx ? { ...x, cards: [] } : x)) }, items: [{ cards: h.cards, rot: 0 }] };
    }
    case 'box': {
      const g = GROUP_BY_KEY.get(src.group);
      const inBox = s.box[src.group] ?? [];
      if (!g || !inBox.length) return null;
      if (src.def === undefined) {
        return {
          s: { ...s, box: { ...s.box, [src.group]: [] } },
          items: [{ cards: inBox.map((c) => ({ ...c, faceUp: g.faceUp })), rot: 0, label: g.label }],
        };
      }
      const idx = inBox.findIndex((c) => c.def === src.def);
      if (idx < 0) return null;
      const c = { ...inBox[idx], faceUp: true };
      return { s: { ...s, box: { ...s.box, [src.group]: inBox.filter((_, i) => i !== idx) } }, items: [{ cards: [c], rot: 0 }] };
    }
  }
}

/** Put items down at a target. */
export function put(s: TableState, items: Item[], dst: Target): TableState {
  switch (dst.kind) {
    case 'table': {
      const added: Pile[] = items.map((it, i) => {
        const pos = dst.placements[i] ?? dst.placements[dst.placements.length - 1];
        return { id: it.fromPileId ?? pos.id ?? newId(), x: pos.x, y: pos.y, cards: it.cards, rot: it.rot, label: it.label };
      });
      return { ...s, piles: [...s.piles, ...added] };
    }
    case 'pile': {
      const target = s.piles.find((p) => p.id === dst.pileId);
      if (!target) return put(s, items, { kind: 'table', placements: items.map((it) => ({ x: it.x ?? 0, y: it.y ?? 0 })) });
      const extra = items.flatMap((it) => it.cards);
      const merged = { ...target, cards: [...target.cards, ...extra] };
      // dropping onto a pile brings it to the top of the z-order
      return { ...s, piles: [...s.piles.filter((p) => p.id !== target.id), merged] };
    }
    case 'hand': {
      const cards = items.flatMap((it) => it.cards).map((c) => ({ ...c, faceUp: true }));
      return {
        ...s,
        hands: s.hands.map((h, i) => {
          if (i !== dst.handIdx) return h;
          const next = [...h.cards];
          next.splice(Math.max(0, Math.min(dst.index, next.length)), 0, ...cards);
          return { ...h, cards: next };
        }),
      };
    }
    case 'box': {
      const box = { ...s.box };
      for (const c of items.flatMap((it) => it.cards)) {
        const key = CARDS[c.def].group;
        box[key] = [...(box[key] ?? []), { ...c, faceUp: false }];
      }
      return { ...s, box };
    }
  }
}

function shuffled<T>(arr: T[]): T[] {
  const a = [...arr];
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

const mapPile = (s: TableState, id: string, f: (p: Pile) => Pile | null): TableState => {
  const piles: Pile[] = [];
  for (const p of s.piles) {
    if (p.id !== id) piles.push(p);
    else {
      const n = f(p);
      if (n && n.cards.length) piles.push(n);
    }
  }
  return { ...s, piles };
};

// ---------------------------------------------------------------- store
interface Store {
  present: TableState;
  past: TableState[];
  future: TableState[];
  commit: (next: TableState) => void;
  undo: () => void;
  redo: () => void;

  move: (src: Source, dst: Target) => boolean;
  flipPile: (id: string) => void;
  flipTop: (id: string) => void;
  shuffle: (id: string) => void;
  draw: (id: string, n: number) => void;
  rotate: (id: string) => void;
  spread: (id: string, faceUp?: boolean) => void;
  gather: (ids: string[], shuffle?: boolean) => void;
  flipMany: (ids: string[]) => void;
  returnToBox: (ids: string[]) => void;
  setLabel: (id: string, label: string) => void;
  moveInPile: (pileId: string, uid: string, where: 'top' | 'bottom') => void;

  setActiveHand: (i: number) => void;
  addHand: () => void;
  renameHand: (i: number, name: string) => void;
  removeHand: (i: number) => void;
  setPlayerMats: (n: number) => void;
  reset: () => void;
  load: (s: TableState) => void;
}

export const useTable = create<Store>((set, get) => ({
  present: loadSaved() ?? freshState(),
  past: [],
  future: [],

  commit: (next) =>
    set((st) => (next === st.present ? st : { present: next, past: [...st.past, st.present].slice(-HISTORY_LIMIT), future: [] })),
  undo: () =>
    set((st) => (st.past.length ? { present: st.past[st.past.length - 1], past: st.past.slice(0, -1), future: [st.present, ...st.future] } : st)),
  redo: () =>
    set((st) => (st.future.length ? { present: st.future[0], past: [...st.past, st.present], future: st.future.slice(1) } : st)),

  move: (src, dst) => {
    let s = get().present;
    // reordering inside the same hand: account for the removed slot
    if (src.kind === 'hand' && dst.kind === 'hand' && src.handIdx === dst.handIdx && src.index < dst.index) {
      dst = { ...dst, index: dst.index - 1 };
    }
    if (src.kind === 'pile' && dst.kind === 'pile' && src.pileId === dst.pileId) {
      const p = s.piles.find((q) => q.id === src.pileId);
      if (p && src.count >= p.cards.length) return false; // dropped a whole pile onto itself
    }
    const t = take(s, src);
    if (!t) return false;
    s = put(t.s, t.items, dst);
    get().commit(s);
    return true;
  },

  flipPile: (id) =>
    get().commit(mapPile(get().present, id, (p) => ({ ...p, cards: [...p.cards].reverse().map((c) => ({ ...c, faceUp: !c.faceUp })) }))),
  flipTop: (id) =>
    get().commit(
      mapPile(get().present, id, (p) => ({
        ...p,
        cards: p.cards.map((c, i) => (i === p.cards.length - 1 ? { ...c, faceUp: !c.faceUp } : c)),
      })),
    ),
  shuffle: (id) => get().commit(mapPile(get().present, id, (p) => ({ ...p, cards: shuffled(p.cards) }))),
  draw: (id, n) => {
    const s = get().present;
    const p = s.piles.find((q) => q.id === id);
    if (!p || n <= 0) return;
    get().move({ kind: 'pile', pileId: id, count: Math.min(n, p.cards.length) }, { kind: 'hand', handIdx: s.activeHand, index: Infinity });
  },
  rotate: (id) => get().commit(mapPile(get().present, id, (p) => ({ ...p, rot: p.rot === 0 ? 90 : 0 }))),
  spread: (id, faceUp) => {
    const s = get().present;
    const p = s.piles.find((q) => q.id === id);
    if (!p || p.cards.length < 2) return;
    const step = CARD_W * 0.42;
    // top card ends up rightmost, like fanning a pile out with your hand
    const fanned: Pile[] = p.cards.map((c, i) => {
      const id = i === 0 ? p.id : newId();
      origins.set(id, { x: p.x, y: p.y });
      return {
        id,
        x: p.x + i * step,
        y: p.y,
        cards: [faceUp === undefined ? c : { ...c, faceUp }],
        rot: 0,
        label: i === 0 ? p.label : undefined,
      };
    });
    get().commit({ ...s, piles: [...s.piles.filter((q) => q.id !== id), ...fanned] });
  },
  gather: (ids, doShuffle) => {
    const s = get().present;
    const set = new Set(ids);
    const picked = s.piles.filter((p) => set.has(p.id)); // z-order: bottom first
    if (!picked.length) return;
    const base = picked[0];
    let cards = picked.flatMap((p) => p.cards);
    if (doShuffle) cards = shuffled(cards);
    const merged: Pile = { ...base, cards, label: picked.find((p) => p.label)?.label };
    get().commit({ ...s, piles: [...s.piles.filter((p) => !set.has(p.id)), merged] });
  },
  flipMany: (ids) => {
    const set = new Set(ids);
    const s = get().present;
    get().commit({
      ...s,
      piles: s.piles.map((p) => (set.has(p.id) ? { ...p, cards: [...p.cards].reverse().map((c) => ({ ...c, faceUp: !c.faceUp })) } : p)),
    });
  },
  returnToBox: (ids) => {
    const t = take(get().present, { kind: 'piles', pileIds: ids });
    if (t) get().commit(put(t.s, t.items, { kind: 'box' }));
  },
  setLabel: (id, label) => get().commit(mapPile(get().present, id, (p) => ({ ...p, label: label.trim() || undefined }))),
  moveInPile: (pileId, uid, where) =>
    get().commit(
      mapPile(get().present, pileId, (p) => {
        const c = p.cards.find((k) => k.uid === uid);
        if (!c) return p;
        const rest = p.cards.filter((k) => k.uid !== uid);
        return { ...p, cards: where === 'top' ? [...rest, c] : [c, ...rest] };
      }),
    ),

  setActiveHand: (i) => set((st) => ({ present: { ...st.present, activeHand: i } })), // not an undo step
  addHand: () => {
    const s = get().present;
    const n = s.hands.length + 1;
    const h: Hand = { id: newId('h'), name: `Player ${n}`, cards: [] };
    get().commit({ ...s, hands: [...s.hands, h], activeHand: s.hands.length });
  },
  renameHand: (i, name) => {
    const s = get().present;
    get().commit({ ...s, hands: s.hands.map((h, k) => (k === i ? { ...h, name: name.trim() || h.name } : h)) });
  },
  removeHand: (i) => {
    const s = get().present;
    if (s.hands.length <= 1) return;
    const h = s.hands[i];
    let next: TableState = { ...s, hands: s.hands.filter((_, k) => k !== i), activeHand: 0 };
    if (h.cards.length) next = put(next, [{ cards: h.cards, rot: 0 }], { kind: 'box' });
    get().commit(next);
  },
  setPlayerMats: (n) => {
    const s = get().present;
    get().commit({ ...s, playerMats: Math.max(0, Math.min(5, n)) });
  },
  reset: () => get().commit(freshState(get().present.playerMats || 1)),
  load: (s) => get().commit(s),
}));

// ---------------------------------------------------------------- autosave
let saveTimer: number | undefined;
useTable.subscribe((st, prev) => {
  if (st.present === prev.present) return;
  window.clearTimeout(saveTimer);
  saveTimer = window.setTimeout(() => {
    try {
      localStorage.setItem(SAVE_KEY, JSON.stringify(st.present));
    } catch {
      /* storage full / private mode: ignore */
    }
  }, 300);
});
