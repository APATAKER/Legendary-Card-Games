import { useCallback, useEffect, useRef } from 'react';
import { CARD_H, CARD_W, matBounds } from '../layout';
import { pileRect } from '../drag';
import { useTable } from '../store';
import { screenToWorld, useUI, type View } from '../ui';
import { CardView, stackShadow } from './Card';
import { Mat } from './Mat';
import { PileView } from './PileView';

const MIN_ZOOM = 0.22;
const MAX_ZOOM = 1.85;

export function fitTableToViewport(el?: HTMLElement | null) {
  const host = el ?? document.querySelector<HTMLElement>('.lc-viewport');
  if (!host) return;
  const r = host.getBoundingClientRect();
  const ui = useUI.getState();
  const playerMats = useTable.getState().present.playerMats;
  const b = matBounds(playerMats);
  const leftPad = ui.boxOpen ? 330 : 24;
  const rightPad = 24;
  const topPad = 64;
  const bottomPad = ui.trayOpen ? 230 : 32;
  const availW = Math.max(320, r.width - leftPad - rightPad);
  const availH = Math.max(240, r.height - topPad - bottomPad);
  const zoom = Math.max(MIN_ZOOM, Math.min(0.95, Math.min(availW / b.w, availH / b.h)));
  const x = leftPad + (availW - b.w * zoom) / 2;
  const y = topPad + (availH - b.h * zoom) / 2;
  ui.set({ view: { x, y, zoom } });
}

export function zoomByFactor(factor: number, cx?: number, cy?: number) {
  const ui = useUI.getState();
  const v = ui.view;
  const sx = cx ?? window.innerWidth / 2;
  const sy = cy ?? window.innerHeight / 2;
  const nextZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, v.zoom * factor));
  const wx = (sx - v.x) / v.zoom;
  const wy = (sy - v.y) / v.zoom;
  ui.set({
    view: {
      zoom: nextZoom,
      x: sx - wx * nextZoom,
      y: sy - wy * nextZoom,
    },
  });
}

export function Table() {
  const piles = useTable((s) => s.present.piles);
  const view = useUI((s) => s.view);
  const drag = useUI((s) => s.drag);
  const marquee = useUI((s) => s.marquee);
  const spaceDown = useUI((s) => s.spaceDown);
  const vpRef = useRef<HTMLDivElement>(null);

  // Fit board nicely on initial mount
  useEffect(() => {
    fitTableToViewport(vpRef.current);
  }, []);

  // Wheel zoom centered at pointer
  useEffect(() => {
    const el = vpRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if ((e.target as HTMLElement | null)?.closest('[data-nodrop]')) return;
      e.preventDefault();
      const factor = Math.exp(-e.deltaY * 0.0014);
      zoomByFactor(factor, e.clientX, e.clientY);
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, []);

  const startPan = useCallback((sx0: number, sy0: number, v0: View) => {
    const move = (ev: PointerEvent) => {
      useUI.getState().set({
        view: {
          ...v0,
          x: v0.x + (ev.clientX - sx0),
          y: v0.y + (ev.clientY - sy0),
        },
      });
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      document.body.classList.remove('is-panning');
    };
    document.body.classList.add('is-panning');
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  }, []);

  const startMarquee = useCallback((sx0: number, sy0: number, additive: boolean) => {
    const w0 = screenToWorld(sx0, sy0);
    const baseSel = additive ? new Set(useUI.getState().selection) : new Set<string>();
    let dragged = false;

    const move = (ev: PointerEvent) => {
      if (!dragged && Math.hypot(ev.clientX - sx0, ev.clientY - sy0) < 5) return;
      dragged = true;
      const w1 = screenToWorld(ev.clientX, ev.clientY);
      const rect = {
        x: Math.min(w0.x, w1.x),
        y: Math.min(w0.y, w1.y),
        w: Math.abs(w1.x - w0.x),
        h: Math.abs(w1.y - w0.y),
      };
      const allPiles = useTable.getState().present.piles;
      const hit = new Set(baseSel);
      for (const p of allPiles) {
        const pr = pileRect(p);
        const overlaps =
          pr.x < rect.x + rect.w &&
          pr.x + pr.w > rect.x &&
          pr.y < rect.y + rect.h &&
          pr.y + pr.h > rect.y;
        if (overlaps) hit.add(p.id);
      }
      useUI.getState().set({ marquee: rect, selection: [...hit] });
    };

    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      if (!dragged && !additive) {
        useUI.getState().set({ selection: [], marquee: null });
      } else {
        useUI.getState().set({ marquee: null });
      }
    };

    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
  }, []);

  const onPointerDown = (e: React.PointerEvent) => {
    if ((e.target as HTMLElement).closest('[data-pile], [data-nodrop]')) return;
    const ui = useUI.getState();
    if (ui.menu) ui.set({ menu: null });

    // Middle mouse button, or Space + Left drag, or Shift/Ctrl-less drag?
    // Wait: let's support Left-drag on empty table to PAN (or Shift+Left-drag for Marquee box selection,
    // and also allow Marquee with Shift or right-drag, or let's make Left-drag pan if Space/Middle, and Marquee on Left-drag OR vice versa).
    // Let's check what's most natural:
    // Left-drag on empty mat -> Marquee select when Shift is held, OR pan when dragging empty felt?
    // Wait: in our plan we noted: "Drag a selection box on empty table to pick several cards" AND "Drag empty table (or hold middle mouse / Space) to pan."
    // How can both work seamlessly?
    // - Middle mouse button or Space+drag always pans.
    // - Right-button drag pans too!
    // - Left-drag on empty table: if Shift is held -> Marquee box select; otherwise -> Pan the table! (And we can also have a toolbar/shortcut hint so both pan and marquee are effortless).
    // Wait: what if someone drags a selection box without Shift? Let's make Left-drag on the Mat do Marquee select if they hold Shift, OR let's support BOTH:
    // Actually, if Left-drag pans by default and Shift+Left-drag draws a selection box (and Middle/Space+drag always pans even over cards), let's check if people also try Left-drag for selection.
    if (e.button === 1 || (e.button === 0 && ui.spaceDown)) {
      e.preventDefault();
      startPan(e.clientX, e.clientY, ui.view);
      return;
    }
    if (e.button === 0) {
      if (e.shiftKey || e.ctrlKey || e.metaKey) {
        startMarquee(e.clientX, e.clientY, e.ctrlKey || e.metaKey);
      } else {
        // Pan the table, and if it was just a click without moving, clear selection
        const sx0 = e.clientX;
        const sy0 = e.clientY;
        startPan(sx0, sy0, ui.view);
        const clearOnClick = (ev: PointerEvent) => {
          window.removeEventListener('pointerup', clearOnClick);
          if (Math.hypot(ev.clientX - sx0, ev.clientY - sy0) < 5 && useUI.getState().selection.length) {
            useUI.getState().set({ selection: [] });
          }
        };
        window.addEventListener('pointerup', clearOnClick);
      }
    }
  };

  const onContextMenu = (e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest('[data-pile], [data-nodrop]')) return;
    e.preventDefault();
    const ui = useUI.getState();
    ui.set({
      menu: ui.selection.length > 1
        ? { kind: 'selection', x: e.clientX, y: e.clientY }
        : { kind: 'table', x: e.clientX, y: e.clientY },
    });
  };

  return (
    <div
      ref={vpRef}
      className={spaceDown ? 'lc-viewport is-space-pan' : 'lc-viewport'}
      onPointerDown={onPointerDown}
      onContextMenu={onContextMenu}
    >
      <div
        className="lc-world"
        style={{
          transform: `translate3d(${view.x}px, ${view.y}px, 0) scale(${view.zoom})`,
        }}
      >
        <Mat />

        {piles.map((p) => (
          <PileView key={p.id} pile={p} />
        ))}

        {/* Floating cards being dragged */}
        {drag &&
          drag.items.map((it, idx) => {
            const top = it.cards[it.cards.length - 1];
            if (!top) return null;
            const x = (it.x ?? 0) + drag.dx;
            const y = (it.y ?? 0) + drag.dy;
            const n = it.cards.length;
            return (
              <div
                key={idx}
                className="lc-drag-ghost"
                style={{
                  left: x,
                  top: y,
                  width: CARD_W,
                  height: CARD_H,
                  transform: `scale(1.06) rotate(${(it.rot || 0) + drag.tilt}deg)`,
                }}
              >
                <div className="lc-stack" style={{ boxShadow: stackShadow(n, true) }}>
                  <CardView card={top} width={CARD_W} />
                </div>
                {n > 1 && <div className="lc-count">{n}</div>}
              </div>
            );
          })}

        {/* Marquee selection rectangle */}
        {marquee && (
          <div
            className="lc-marquee"
            style={{
              left: marquee.x,
              top: marquee.y,
              width: marquee.w,
              height: marquee.h,
            }}
          />
        )}
      </div>
    </div>
  );
}
