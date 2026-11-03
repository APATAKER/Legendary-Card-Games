// Pointer-driven drag & drop for everything (table piles, hand cards, box, search window).
import { CARD_H, CARD_W, buildZones, slotCardPos } from './layout';
import { origins } from './fx';
import { newId, useTable, type Item, type Source, type Target } from './store';
import { screenToWorld, useUI, type DragState, type DropHover } from './ui';
import type { Pile, Zone } from './types';

export interface DragBegin {
  source: Source;
  items: Item[];
  fromScreen?: boolean; // source lives in screen space (hand/box/search): centre cards under the pointer
}

const SNAP_MARGIN = 36;

let zoneCache: { n: number; zones: Zone[] } = { n: -1, zones: [] };
function zones(): Zone[] {
  const n = useTable.getState().present.playerMats;
  if (zoneCache.n !== n) zoneCache = { n, zones: buildZones(n) };
  return zoneCache.zones;
}

export function pileRect(p: Pile) {
  if (p.rot === 90) {
    const cx = p.x + CARD_W / 2;
    const cy = p.y + CARD_H / 2;
    return { x: cx - CARD_H / 2, y: cy - CARD_W / 2, w: CARD_H, h: CARD_W };
  }
  return { x: p.x, y: p.y, w: CARD_W, h: CARD_H };
}

const inside = (r: { x: number; y: number; w: number; h: number }, x: number, y: number, m = 0) =>
  x >= r.x - m && x <= r.x + r.w + m && y >= r.y - m && y <= r.y + r.h + m;

function handInsertIndex(sx: number): number {
  const els = Array.from(document.querySelectorAll<HTMLElement>('[data-hand-card]'));
  for (const el of els) {
    const r = el.getBoundingClientRect();
    if (sx < r.left + r.width / 2) return Number(el.dataset.handCard);
  }
  return els.length;
}

/** Work out what would happen if the drag were released right now. */
export function resolveDrop(sx: number, sy: number, d: DragState): { hover: DropHover | null; target: Target | null } {
  const el = document.elementFromPoint(sx, sy) as HTMLElement | null;
  const table = useTable.getState().present;

  if (el?.closest('[data-drop="hand"]')) {
    const index = handInsertIndex(sx);
    return { hover: { kind: 'hand', index }, target: { kind: 'hand', handIdx: table.activeHand, index } };
  }
  if (el?.closest('[data-drop="box"]')) {
    if (d.source.kind === 'box') return { hover: null, target: null };
    return { hover: { kind: 'box' }, target: { kind: 'box' } };
  }
  if (el?.closest('[data-nodrop]')) return { hover: null, target: null };

  const prim = d.items[0];
  const px = (prim.x ?? 0) + d.dx;
  const py = (prim.y ?? 0) + d.dy;
  const cx = px + CARD_W / 2;
  const cy = py + CARD_H / 2;
  const hidden = new Set(d.hiddenPiles);

  // 1. onto another pile (topmost first)
  for (let i = table.piles.length - 1; i >= 0; i--) {
    const p = table.piles[i];
    if (hidden.has(p.id)) continue;
    if (inside(pileRect(p), cx, cy)) return { hover: { kind: 'pile', id: p.id }, target: { kind: 'pile', pileId: p.id } };
  }

  const free = d.items.map((it) => ({ x: (it.x ?? 0) + d.dx, y: (it.y ?? 0) + d.dy }));
  // 2. snap into an empty slot
  for (const z of zones()) {
    if (z.kind !== 'slot' || !inside(z, cx, cy, SNAP_MARGIN)) continue;
    const pos = slotCardPos(z);
    const sx2 = pos.x - px;
    const sy2 = pos.y - py;
    return { hover: { kind: 'zone', id: z.id }, target: { kind: 'table', placements: free.map((f) => ({ x: f.x + sx2, y: f.y + sy2 })) } };
  }
  // 3. free placement (highlight the area it lands in, if any)
  const area = zones().find((z) => z.kind === 'area' && inside(z, cx, cy));
  return { hover: area ? { kind: 'zone', id: area.id } : { kind: 'table' }, target: { kind: 'table', placements: free } };
}

const sameHover = (a: DropHover | null, b: DropHover | null) => JSON.stringify(a) === JSON.stringify(b);

/**
 * Call from onPointerDown. The drag only starts once the pointer moves a few pixels;
 * otherwise `onClick` fires. `make` builds the picked-up items lazily at that moment.
 */
export function beginDrag(e: { clientX: number; clientY: number }, make: () => DragBegin | null, onClick?: (ev: PointerEvent) => void) {
  const sx0 = e.clientX;
  const sy0 = e.clientY;
  let started = false;
  let w0 = { x: 0, y: 0 };
  let lastX = sx0;
  let tilt = 0;
  let raf = 0;
  let pending: PointerEvent | null = null;

  const cleanup = () => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', cancel);
    window.removeEventListener('keydown', key);
    cancelAnimationFrame(raf);
    document.body.classList.remove('is-dragging');
  };
  const cancel = () => {
    cleanup();
    if (started) useUI.getState().set({ drag: null });
  };
  const key = (ev: KeyboardEvent) => {
    if (ev.key === 'Escape') cancel();
  };

  const flush = () => {
    raf = 0;
    const ev = pending;
    pending = null;
    const ui = useUI.getState();
    if (!ev || !ui.drag) return;
    const w = screenToWorld(ev.clientX, ev.clientY);
    const vx = ev.clientX - lastX;
    lastX = ev.clientX;
    tilt = tilt * 0.75 + Math.max(-9, Math.min(9, vx * 0.5)) * 0.25;
    const next: DragState = { ...ui.drag, dx: w.x - w0.x, dy: w.y - w0.y, tilt };
    const r = resolveDrop(ev.clientX, ev.clientY, next);
    next.target = r.target;
    next.hover = sameHover(ui.drag.hover, r.hover) ? ui.drag.hover : r.hover;
    ui.set({ drag: next });
  };

  const move = (ev: PointerEvent) => {
    if (!started) {
      if (Math.hypot(ev.clientX - sx0, ev.clientY - sy0) < 5) return;
      const b = make();
      if (!b || !b.items.length) return cleanup();
      started = true;
      w0 = screenToWorld(sx0, sy0);
      let items = b.items;
      if (b.fromScreen) {
        items = items.map((it, i) => ({ ...it, x: w0.x - CARD_W / 2 + i * 18, y: w0.y - CARD_H / 2 + i * 18 }));
      }
      const src = b.source;
      const hiddenPiles =
        src.kind === 'piles'
          ? src.pileIds
          : src.kind === 'pile' && items[0]?.fromPileId
            ? [src.pileId]
            : [];
      const lift = src.kind === 'pile' && !items[0]?.fromPileId ? { pileId: src.pileId, count: src.count } : null;
      useUI.getState().set({
        drag: { source: src, items, dx: 0, dy: 0, tilt: 0, hover: null, target: null, hiddenPiles, lift, fromScreen: !!b.fromScreen },
        menu: null,
        hover: null,
      });
      document.body.classList.add('is-dragging');
    }
    pending = ev;
    if (!raf) raf = requestAnimationFrame(flush);
  };

  const up = (ev: PointerEvent) => {
    cleanup();
    if (!started) {
      onClick?.(ev);
      return;
    }
    const ui = useUI.getState();
    const d = ui.drag;
    ui.set({ drag: null });
    if (!d) return;
    const w = screenToWorld(ev.clientX, ev.clientY);
    const fin: DragState = { ...d, dx: w.x - w0.x, dy: w.y - w0.y };
    let { target } = resolveDrop(ev.clientX, ev.clientY, fin);
    if (!target) return;

    if (target.kind === 'table') {
      // give new piles ids now so they can animate from the release point
      const placements = target.placements.map((p, i) => {
        const it = d.items[i];
        const id = it?.fromPileId ?? newId();
        if (it) origins.set(id, { x: (it.x ?? 0) + fin.dx, y: (it.y ?? 0) + fin.dy, lifted: true });
        return { ...p, id };
      });
      target = { ...target, placements };
    }
    const ok = useTable.getState().move(d.source, target);
    if (ok && target.kind === 'pile') ui.play(target.pileId, 'bump');
    if (ok && d.source.kind === 'piles') ui.set({ selection: target.kind === 'table' ? d.source.pileIds : [] });
  };

  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', cancel);
  window.addEventListener('keydown', key);
}
