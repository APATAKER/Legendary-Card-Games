import { useEffect } from 'react';
import {
  Archive,
  Eye,
  FlipHorizontal2,
  GalleryHorizontal,
  Group,
  Hand,
  Pencil,
  RotateCw,
  Search,
  Shuffle,
} from 'lucide-react';
import { fitTableToViewport } from './Table';
import { useTable } from '../store';
import { useUI } from '../ui';

export function ContextMenu() {
  const menu = useUI((s) => s.menu);
  const st = useTable((s) => s.present);

  useEffect(() => {
    if (!menu) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') useUI.getState().set({ menu: null });
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [menu]);

  if (!menu) return null;

  const close = () => useUI.getState().set({ menu: null });
  const x = Math.min(menu.x, window.innerWidth - 230);
  const y = Math.min(menu.y, window.innerHeight - 310);

  if (menu.kind === 'pile') {
    const pile = st.piles.find((p) => p.id === menu.id);
    if (!pile) return null;
    const top = pile.cards[pile.cards.length - 1];
    const isStack = pile.cards.length > 1;
    const activeHandName = st.hands[st.activeHand]?.name ?? 'Hand';

    return (
      <div className="lc-menu" style={{ left: x, top: y }} data-nodrop onPointerDown={(e) => e.stopPropagation()}>
        <div className="lc-menu-head">
          {pile.label ? `${pile.label} (${pile.cards.length})` : isStack ? `Pile (${pile.cards.length} cards)` : 'Single Card'}
        </div>

        {top?.faceUp && (
          <button
            className="lc-menu-item"
            onClick={() => {
              useUI.getState().set({ inspect: top.def, menu: null });
            }}
          >
            <Eye className="w-3.5 h-3.5" />
            <span>Inspect Card</span>
            <kbd>Z</kbd>
          </button>
        )}

        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().flipPile(pile.id);
            if (isStack) useUI.getState().play(pile.id, 'flip');
            close();
          }}
        >
          <FlipHorizontal2 className="w-3.5 h-3.5" />
          <span>{isStack ? 'Flip Entire Pile' : 'Flip Card'}</span>
          <kbd>F</kbd>
        </button>

        {isStack && (
          <button
            className="lc-menu-item"
            onClick={() => {
              useTable.getState().flipTop(pile.id);
              close();
            }}
          >
            <FlipHorizontal2 className="w-3.5 h-3.5" />
            <span>Flip Top Card Only</span>
            <kbd>⇧F</kbd>
          </button>
        )}

        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().draw(pile.id, 1);
            close();
          }}
        >
          <Hand className="w-3.5 h-3.5" />
          <span>Draw 1 to {activeHandName}</span>
          <kbd>D</kbd>
        </button>

        {isStack && (
          <button
            className="lc-menu-item"
            onClick={() => {
              useTable.getState().draw(pile.id, 6);
              close();
            }}
          >
            <Hand className="w-3.5 h-3.5" />
            <span>Draw 6 to {activeHandName}</span>
            <kbd>6</kbd>
          </button>
        )}

        {isStack && (
          <>
            <div className="lc-menu-sep" />
            <button
              className="lc-menu-item"
              onClick={() => {
                useTable.getState().shuffle(pile.id);
                useUI.getState().play(pile.id, 'shuffle');
                useUI.getState().say(`Shuffled (${pile.cards.length} cards)`);
                close();
              }}
            >
              <Shuffle className="w-3.5 h-3.5" />
              <span>Shuffle Pile</span>
              <kbd>S</kbd>
            </button>

            <button
              className="lc-menu-item"
              onClick={() => {
                useUI.getState().set({ searchPile: pile.id, menu: null });
              }}
            >
              <Search className="w-3.5 h-3.5" />
              <span>Search / Browse Pile…</span>
            </button>

            <button
              className="lc-menu-item"
              onClick={() => {
                useTable.getState().spread(pile.id);
                close();
              }}
            >
              <GalleryHorizontal className="w-3.5 h-3.5" />
              <span>Spread Cards in Row</span>
            </button>

            <button
              className="lc-menu-item"
              onClick={() => {
                useTable.getState().spread(pile.id, true);
                close();
              }}
            >
              <GalleryHorizontal className="w-3.5 h-3.5" />
              <span>Spread All Face-Up</span>
            </button>
          </>
        )}

        <div className="lc-menu-sep" />
        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().rotate(pile.id);
            close();
          }}
        >
          <RotateCw className="w-3.5 h-3.5" />
          <span>Rotate 90°</span>
          <kbd>R</kbd>
        </button>

        <button
          className="lc-menu-item"
          onClick={() => {
            const next = window.prompt('Pile label (leave blank to clear):', pile.label ?? '');
            if (next !== null) useTable.getState().setLabel(pile.id, next);
            close();
          }}
        >
          <Pencil className="w-3.5 h-3.5" />
          <span>Label Pile…</span>
        </button>

        <button
          className="lc-menu-item is-danger"
          onClick={() => {
            useTable.getState().returnToBox([pile.id]);
            close();
          }}
        >
          <Archive className="w-3.5 h-3.5" />
          <span>Return to Box</span>
          <kbd>Del</kbd>
        </button>
      </div>
    );
  }

  if (menu.kind === 'selection') {
    const ids = useUI.getState().selection;
    return (
      <div className="lc-menu" style={{ left: x, top: y }} data-nodrop onPointerDown={(e) => e.stopPropagation()}>
        <div className="lc-menu-head">Selected ({ids.length} piles)</div>
        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().gather(ids, false);
            useUI.getState().set({ selection: [], menu: null });
          }}
        >
          <Group className="w-3.5 h-3.5" />
          <span>Gather into One Pile</span>
          <kbd>G</kbd>
        </button>
        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().gather(ids, true);
            useUI.getState().set({ selection: [], menu: null });
            useUI.getState().say('Gathered & shuffled');
          }}
        >
          <Shuffle className="w-3.5 h-3.5" />
          <span>Gather & Shuffle</span>
        </button>
        <button
          className="lc-menu-item"
          onClick={() => {
            useTable.getState().flipMany(ids);
            close();
          }}
        >
          <FlipHorizontal2 className="w-3.5 h-3.5" />
          <span>Flip Selected</span>
          <kbd>F</kbd>
        </button>
        <div className="lc-menu-sep" />
        <button
          className="lc-menu-item is-danger"
          onClick={() => {
            useTable.getState().returnToBox(ids);
            useUI.getState().set({ selection: [], menu: null });
          }}
        >
          <Archive className="w-3.5 h-3.5" />
          <span>Return Selected to Box</span>
          <kbd>Del</kbd>
        </button>
      </div>
    );
  }

  if (menu.kind === 'hand') {
    const hand = st.hands[st.activeHand];
    const card = hand?.cards[menu.index];
    if (!card) return null;
    return (
      <div className="lc-menu" style={{ left: x, top: y }} data-nodrop onPointerDown={(e) => e.stopPropagation()}>
        <div className="lc-menu-head">Hand Card</div>
        <button
          className="lc-menu-item"
          onClick={() => {
            useUI.getState().set({ inspect: card.def, menu: null });
          }}
        >
          <Eye className="w-3.5 h-3.5" />
          <span>Inspect Card</span>
          <kbd>Z</kbd>
        </button>
        <button
          className="lc-menu-item is-danger"
          onClick={() => {
            useTable.getState().move({ kind: 'hand', handIdx: st.activeHand, index: menu.index }, { kind: 'box' });
            close();
          }}
        >
          <Archive className="w-3.5 h-3.5" />
          <span>Return to Box</span>
        </button>
      </div>
    );
  }

  // menu.kind === 'table'
  return (
    <div className="lc-menu" style={{ left: x, top: y }} data-nodrop onPointerDown={(e) => e.stopPropagation()}>
      <div className="lc-menu-head">Tabletop</div>
      <button
        className="lc-menu-item"
        onClick={() => {
          fitTableToViewport();
          close();
        }}
      >
        <span>Fit Table to Screen</span>
        <kbd>0</kbd>
      </button>
      <button
        className="lc-menu-item"
        onClick={() => {
          useUI.getState().set({
            selection: st.piles.map((p) => p.id),
            menu: null,
          });
        }}
      >
        <span>Select All Piles on Table</span>
      </button>
    </div>
  );
}
