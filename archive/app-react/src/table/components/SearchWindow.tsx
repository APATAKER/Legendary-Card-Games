import { useState } from 'react';
import { ArrowDownToLine, ArrowUpToLine, Eye, Hand, Search, Shuffle, X } from 'lucide-react';
import { CARDS } from '../catalog.generated';
import { beginDrag } from '../drag';
import { take, useTable, type Source } from '../store';
import { useUI } from '../ui';

export function SearchWindow() {
  const pileId = useUI((s) => s.searchPile);
  const st = useTable((s) => s.present);
  const [query, setQuery] = useState('');
  const [revealAll, setRevealAll] = useState(true);

  if (!pileId) return null;
  const pile = st.piles.find((p) => p.id === pileId);
  if (!pile) return null;

  // Show top-of-deck first
  const topFirst = [...pile.cards].reverse();
  const q = query.trim().toLowerCase();
  const filtered = topFirst.filter((c) => !q || CARDS[c.def].name.toLowerCase().includes(q));

  const close = (doShuffle: boolean) => {
    if (doShuffle) {
      useTable.getState().shuffle(pile.id);
      useUI.getState().play(pile.id, 'shuffle');
      useUI.getState().say(`Shuffled (${pile.cards.length} cards)`);
    }
    useUI.getState().set({ searchPile: null });
  };

  const startCardDrag = (e: React.PointerEvent, uid: string) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    beginDrag(e, () => {
      const cur = useTable.getState().present;
      const src: Source = { kind: 'pileCard', pileId: pile.id, uid };
      const t = take(cur, src);
      if (!t) return null;
      // Reveal card when pulled out of a searched pile
      const items = t.items.map((it) => ({
        ...it,
        cards: it.cards.map((c) => ({ ...c, faceUp: true })),
      }));
      return { source: src, items, fromScreen: true };
    });
  };

  return (
    <div className="lc-search-window" data-nodrop onPointerDown={(e) => e.stopPropagation()}>
      <div className="lc-search-head">
        <div>
          <div className="lc-search-title">
            {pile.label ?? 'Deck / Pile'} — {pile.cards.length} cards (Top → Bottom)
          </div>
          <div className="lc-search-sub">
            Drag any card directly onto the table or into your hand.
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            className="lc-btn"
            onClick={() => setRevealAll((r) => !r)}
          >
            <Eye className="w-3.5 h-3.5" />
            <span>{revealAll ? 'Hide Face-Down' : 'Reveal All'}</span>
          </button>
          <button
            className="lc-btn is-accent"
            onClick={() => close(true)}
            title="Shuffle this pile and close window"
          >
            <Shuffle className="w-3.5 h-3.5" />
            <span>Shuffle & Close</span>
          </button>
          <button className="lc-btn" onClick={() => close(false)}>
            <X className="w-4 h-4" />
            <span>Close</span>
          </button>
        </div>
      </div>

      <div className="lc-search-filter">
        <Search className="w-4 h-4 text-neutral-400" />
        <input
          type="text"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder="Filter cards in this pile..."
        />
      </div>

      <div className="lc-search-grid">
        {filtered.map((c, idx) => {
          const def = CARDS[c.def];
          const showFace = revealAll || c.faceUp;
          return (
            <div
              key={c.uid}
              className="lc-search-card"
              onPointerDown={(e) => startCardDrag(e, c.uid)}
            >
              <div className="lc-search-card-pos">#{idx + 1} from top</div>
              <div className="lc-search-card-art">
                {showFace ? (
                  <img src={def.thumb} alt={def.name} draggable={false} />
                ) : (
                  <div className="lc-search-facedown">Face Down</div>
                )}
              </div>
              <div className="lc-search-card-name">{showFace ? def.name : 'Hidden Card'}</div>
              <div className="lc-search-card-actions" onPointerDown={(e) => e.stopPropagation()}>
                <button
                  title="Draw to active hand"
                  onClick={() =>
                    useTable.getState().move(
                      { kind: 'pileCard', pileId: pile.id, uid: c.uid },
                      { kind: 'hand', handIdx: st.activeHand, index: Infinity },
                    )
                  }
                >
                  <Hand className="w-3.5 h-3.5" />
                </button>
                <button
                  title="Put on top of pile"
                  onClick={() => useTable.getState().moveInPile(pile.id, c.uid, 'top')}
                >
                  <ArrowUpToLine className="w-3.5 h-3.5" />
                </button>
                <button
                  title="Put on bottom of pile"
                  onClick={() => useTable.getState().moveInPile(pile.id, c.uid, 'bottom')}
                >
                  <ArrowDownToLine className="w-3.5 h-3.5" />
                </button>
                {showFace && (
                  <button
                    title="Inspect full-res"
                    onClick={() => useUI.getState().set({ inspect: c.def })}
                  >
                    <Eye className="w-3.5 h-3.5" />
                  </button>
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
