import { Plus, PanelBottomClose, PanelBottomOpen, Layers, X } from 'lucide-react';
import { beginDrag } from '../drag';
import { CARD_W, playerMatY } from '../layout';
import { take, useTable, type Source } from '../store';
import { useUI } from '../ui';
import { CardView } from './Card';

const HAND_CARD_W = 116;

export function HandTray() {
  const hands = useTable((s) => s.present.hands);
  const activeHand = useTable((s) => s.present.activeHand);
  const trayOpen = useUI((s) => s.trayOpen);
  const dropIndex = useUI((s) => (s.drag?.hover?.kind === 'hand' ? s.drag.hover.index : null));

  const hand = hands[activeHand] ?? hands[0];
  const cards = hand?.cards ?? [];

  if (!trayOpen) {
    return (
      <button
        data-nodrop
        className="lc-tray-collapsed"
        onClick={() => useUI.getState().set({ trayOpen: true })}
        title="Show Hand Tray"
      >
        <PanelBottomOpen className="w-4 h-4" />
        <span>
          {hand?.name ?? 'Hand'} ({cards.length})
        </span>
      </button>
    );
  }

  const startCardDrag = (e: React.PointerEvent, index: number) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    const ui = useUI.getState();
    if (ui.menu) ui.set({ menu: null });
    beginDrag(
      e,
      () => {
        const st = useTable.getState().present;
        const src: Source = { kind: 'hand', handIdx: st.activeHand, index };
        const t = take(st, src);
        return t ? { source: src, items: t.items, fromScreen: true } : null;
      },
      () => {
        // Click on hand card: inspect on double click handled separately
      },
    );
  };

  const dumpHandToTable = () => {
    if (!cards.length) return;
    const st = useTable.getState();
    const pIdx = Math.min(st.present.activeHand, Math.max(0, st.present.playerMats - 1));
    const y = playerMatY(pIdx) + 10;
    const x = 60 + 2 * (CARD_W + 42) + 10; // Player Discard slot area
    st.move(
      { kind: 'handAll', handIdx: st.present.activeHand },
      { kind: 'table', placements: [{ x, y }] },
    );
    useUI.getState().say(`Moved ${cards.length} card${cards.length > 1 ? 's' : ''} from ${hand.name} to table`);
  };

  return (
    <div className={dropIndex !== null ? 'lc-tray is-drop-hover' : 'lc-tray'} data-drop="hand" data-nodrop>
      {/* Top tab bar */}
      <div className="lc-tray-bar">
        <div className="lc-tray-tabs">
          {hands.map((h, i) => (
            <div
              key={h.id}
              className={i === activeHand ? 'lc-tray-tab is-active' : 'lc-tray-tab'}
              onClick={() => useTable.getState().setActiveHand(i)}
              onDoubleClick={() => {
                const next = window.prompt('Rename hand:', h.name);
                if (next !== null) useTable.getState().renameHand(i, next);
              }}
              title="Click to switch hand • Double-click to rename"
            >
              <span>{h.name}</span>
              <span className="lc-tray-badge">{h.cards.length}</span>
              {hands.length > 1 && (
                <button
                  className="lc-tray-tab-close"
                  onClick={(e) => {
                    e.stopPropagation();
                    useTable.getState().removeHand(i);
                  }}
                  title="Remove this hand (cards return to box)"
                >
                  <X className="w-3 h-3" />
                </button>
              )}
            </div>
          ))}
          <button
            className="lc-tray-add"
            onClick={() => useTable.getState().addHand()}
            title="Add another player hand"
          >
            <Plus className="w-3.5 h-3.5" />
          </button>
        </div>

        <div className="lc-tray-actions">
          {cards.length > 0 && (
            <button
              className="lc-tray-btn"
              onClick={dumpHandToTable}
              title="Gather all cards in this hand into a pile on the table"
            >
              <Layers className="w-3.5 h-3.5" />
              <span>All to Table</span>
            </button>
          )}
          <button
            className="lc-tray-btn"
            onClick={() => useUI.getState().set({ trayOpen: false })}
            title="Minimize Hand Tray"
          >
            <PanelBottomClose className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* Fanned card row */}
      <div className="lc-tray-cards">
        {cards.length === 0 ? (
          <div className="lc-tray-empty">
            Drag cards here, or hover any deck on the table and press <kbd>D</kbd> / <kbd>1</kbd>–<kbd>9</kbd> to draw
          </div>
        ) : (
          cards.map((c, i) => {
            const mid = (cards.length - 1) / 2;
            const offset = i - mid;
            const angle = Math.max(-14, Math.min(14, offset * 2.2));
            const liftY = Math.abs(offset) * Math.abs(offset) * 0.9;
            const overlap = cards.length > 8 ? Math.max(-52, -18 - (cards.length - 8) * 4) : -12;
            return (
              <div
                key={c.uid}
                data-hand-card={i}
                className="lc-hand-slot"
                style={{
                  marginLeft: i === 0 ? 0 : overlap,
                  transform: `translateY(${liftY.toFixed(1)}px) rotate(${angle.toFixed(2)}deg)`,
                  zIndex: i + 1,
                }}
                onPointerDown={(e) => startCardDrag(e, i)}
                onDoubleClick={() => useUI.getState().set({ inspect: c.def })}
                onContextMenu={(e) => {
                  e.preventDefault();
                  e.stopPropagation();
                  useUI.getState().set({ menu: { kind: 'hand', x: e.clientX, y: e.clientY, index: i } });
                }}
                onPointerEnter={() => {
                  const ui = useUI.getState();
                  if (!ui.drag) ui.set({ hover: { kind: 'hand', index: i } });
                }}
                onPointerLeave={() => {
                  const ui = useUI.getState();
                  if (ui.hover?.kind === 'hand' && ui.hover.index === i) ui.set({ hover: null });
                }}
              >
                <CardView card={c} width={HAND_CARD_W} />
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}
