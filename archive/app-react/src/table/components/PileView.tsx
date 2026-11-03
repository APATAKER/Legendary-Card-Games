import { memo, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { beginDrag } from '../drag';
import { origins } from '../fx';
import { CARD_H, CARD_W } from '../layout';
import { take, useTable, type Source } from '../store';
import { useUI } from '../ui';
import type { Pile } from '../types';
import { CardBack, CardView, stackShadow } from './Card';

export const PileView = memo(function PileView({ pile }: { pile: Pile }) {
  const hidden = useUI((s) => !!s.drag?.hiddenPiles.includes(pile.id));
  const liftCount = useUI((s) => (s.drag?.lift?.pileId === pile.id ? s.drag.lift.count : 0));
  const isTarget = useUI((s) => s.drag?.hover?.kind === 'pile' && s.drag.hover.id === pile.id);
  const selected = useUI((s) => s.selection.includes(pile.id));
  const fx = useUI((s) => s.fx[pile.id]);
  const ref = useRef<HTMLDivElement>(null);
  const stackRef = useRef<HTMLDivElement>(null);
  const prev = useRef<{ x: number; y: number } | null>(null);
  const [riffle, setRiffle] = useState(0);

  // Glide / settle animation whenever the pile changes position.
  useLayoutEffect(() => {
    const el = ref.current;
    const o = origins.get(pile.id);
    const from = o ?? prev.current;
    prev.current = { x: pile.x, y: pile.y };
    if (o) origins.delete(pile.id);
    if (!el || !from) return;
    const dx = from.x - pile.x;
    const dy = from.y - pile.y;
    if (!dx && !dy && !o?.lifted) return;
    el.animate(
      [
        { translate: `${dx}px ${dy}px`, scale: o?.lifted ? '1.06' : '1' },
        { translate: '0px 0px', scale: '1' },
      ],
      { duration: o?.lifted ? 190 : 260, easing: 'cubic-bezier(.2,.8,.3,1)' },
    );
  }, [pile.x, pile.y, pile.id]);

  // One-shot effects (shuffle riffle, whole-pile flip, drop bump).
  useEffect(() => {
    const el = stackRef.current;
    if (!fx || !el) return;
    if (fx.kind === 'shuffle') {
      setRiffle(fx.n);
      el.animate([{ scale: '1' }, { scale: '0.96' }, { scale: '1.02' }, { scale: '1' }], { duration: 620, easing: 'ease-out' });
      const t = window.setTimeout(() => setRiffle(0), 700);
      return () => window.clearTimeout(t);
    }
    if (fx.kind === 'flip') el.animate([{ transform: 'scaleX(1)' }, { transform: 'scaleX(0.02)' }, { transform: 'scaleX(1)' }], { duration: 320, easing: 'ease-in-out' });
    if (fx.kind === 'bump') el.animate([{ scale: '1' }, { scale: '0.97' }, { scale: '1' }], { duration: 160, easing: 'ease-out' });
  }, [fx]);

  const visible = liftCount ? pile.cards.slice(0, pile.cards.length - liftCount) : pile.cards;
  const top = visible[visible.length - 1];
  const n = visible.length;

  const onPointerDown = (e: React.PointerEvent) => {
    const ui = useUI.getState();
    if (e.button !== 0 || ui.spaceDown) return; // middle-drag / space-drag pans the table
    e.stopPropagation();
    if (ui.menu) ui.set({ menu: null });
    const multi = ui.selection.length > 1 && ui.selection.includes(pile.id);
    const whole = e.shiftKey || multi;
    beginDrag(
      e,
      () => {
        const st = useTable.getState().present;
        const sel = new Set(useUI.getState().selection);
        const src: Source = multi
          ? { kind: 'piles', pileIds: st.piles.filter((p) => sel.has(p.id)).map((p) => p.id) }
          : { kind: 'pile', pileId: pile.id, count: whole ? Infinity : 1 };
        const t = take(st, src);
        return t ? { source: src, items: t.items } : null;
      },
      (ev) => {
        const u = useUI.getState();
        if (ev.ctrlKey || ev.metaKey || ev.shiftKey) {
          u.set({ selection: u.selection.includes(pile.id) ? u.selection.filter((x) => x !== pile.id) : [...u.selection, pile.id] });
        } else if (u.selection.length) u.set({ selection: [] });
      },
    );
  };

  const onDoubleClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    useTable.getState().flipPile(pile.id);
    if (pile.cards.length > 1) useUI.getState().play(pile.id, 'flip');
  };

  const onContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const ui = useUI.getState();
    const multi = ui.selection.length > 1 && ui.selection.includes(pile.id);
    ui.set({ menu: multi ? { kind: 'selection', x: e.clientX, y: e.clientY } : { kind: 'pile', x: e.clientX, y: e.clientY, id: pile.id }, hover: null });
  };

  const cls = ['lc-pile'];
  if (selected) cls.push('is-selected');
  if (isTarget) cls.push('is-target');

  return (
    <div
      ref={ref}
      className={cls.join(' ')}
      data-pile={pile.id}
      style={{
        left: pile.x,
        top: pile.y,
        width: CARD_W,
        height: CARD_H,
        transform: pile.rot ? 'rotate(90deg)' : undefined,
        visibility: hidden || !top ? 'hidden' : undefined,
      }}
      onPointerDown={onPointerDown}
      onDoubleClick={onDoubleClick}
      onContextMenu={onContextMenu}
      onPointerEnter={() => {
        const ui = useUI.getState();
        if (!ui.drag) ui.set({ hover: { kind: 'pile', id: pile.id } });
      }}
      onPointerLeave={() => {
        const ui = useUI.getState();
        if (ui.hover?.kind === 'pile' && ui.hover.id === pile.id) ui.set({ hover: null });
      }}
    >
      <div ref={stackRef} className="lc-stack" style={{ boxShadow: stackShadow(n) }}>
        {top && <CardView key={top.uid} card={top} width={CARD_W} />}
      </div>
      {riffle > 0 && (
        <div className="lc-riffle" key={riffle}>
          <div className="lc-riffle-half left">
            <CardBack />
          </div>
          <div className="lc-riffle-half right">
            <CardBack />
          </div>
        </div>
      )}
      {n > 1 && <div className="lc-count">{n}</div>}
      {pile.label && <div className="lc-label">{pile.label}</div>}
    </div>
  );
});
