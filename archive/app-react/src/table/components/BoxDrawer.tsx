import { useMemo, useState } from 'react';
import { ChevronDown, ChevronRight, Package, Search, X, Eye } from 'lucide-react';
import { CARDS, GROUPS, SECTIONS } from '../catalog.generated';
import { beginDrag } from '../drag';
import { CARD_H, CARD_W } from '../layout';
import { take, useTable, type Source } from '../store';
import { screenToWorld, useUI } from '../ui';

export function BoxDrawer() {
  const boxOpen = useUI((s) => s.boxOpen);
  const isDropBox = useUI((s) => s.drag?.hover?.kind === 'box');
  const box = useTable((s) => s.present.box);

  const [query, setQuery] = useState('');
  const [openSections, setOpenSections] = useState<Record<string, boolean>>(() =>
    Object.fromEntries(SECTIONS.map((s, i) => [s.key, i < 4])),
  );
  const [expandedGroup, setExpandedGroup] = useState<string | null>(null);

  const q = query.trim().toLowerCase();

  const totalRemaining = useMemo(
    () => Object.values(box).reduce((sum, list) => sum + list.length, 0),
    [box],
  );

  if (!boxOpen) return null;

  const spawnToTableCenter = (src: Source) => {
    const vp = document.querySelector<HTMLElement>('.lc-viewport');
    const r = vp?.getBoundingClientRect();
    const sx = r ? r.left + r.width * 0.55 : window.innerWidth * 0.55;
    const sy = r ? r.top + r.height * 0.42 : window.innerHeight * 0.42;
    const w = screenToWorld(sx, sy);
    const jitter = () => (Math.random() - 0.5) * 28;
    useTable.getState().move(src, {
      kind: 'table',
      placements: [{ x: w.x - CARD_W / 2 + jitter(), y: w.y - CARD_H / 2 + jitter() }],
    });
  };

  const startBoxDrag = (e: React.PointerEvent, src: Source) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    beginDrag(
      e,
      () => {
        const st = useTable.getState().present;
        const t = take(st, src);
        return t ? { source: src, items: t.items, fromScreen: true } : null;
      },
      () => {
        // Clicking without dragging doesn't auto-spawn so user can expand/inspect calmly
      },
    );
  };

  return (
    <aside
      className={isDropBox ? 'lc-box is-drop-hover' : 'lc-box'}
      data-drop="box"
      data-nodrop
    >
      <div className="lc-box-head">
        <div className="lc-box-title">
          <Package className="w-4 h-4 text-emerald-400" />
          <span>GAME BOX</span>
          <span className="lc-box-count">{totalRemaining} cards</span>
        </div>
        <button
          className="lc-icon-btn"
          onClick={() => useUI.getState().set({ boxOpen: false })}
          title="Close Box Drawer (B)"
        >
          <X className="w-4 h-4" />
        </button>
      </div>

      <div className="lc-box-search">
        <Search className="w-3.5 h-3.5 text-neutral-500" />
        <input
          type="text"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder="Filter decks or cards..."
        />
        {query && (
          <button onClick={() => setQuery('')} className="text-neutral-500 hover:text-white">
            <X className="w-3.5 h-3.5" />
          </button>
        )}
      </div>

      <div className="lc-box-hint">
        Drag any deck or single card onto the table. Drop table cards here to put them back in the box.
      </div>

      <div className="lc-box-scroll">
        {SECTIONS.map((sec) => {
          const secGroups = GROUPS.filter((g) => {
            if (g.section !== sec.key) return false;
            if (!q) return true;
            if (g.label.toLowerCase().includes(q)) return true;
            return g.cards.some((id) => CARDS[id].name.toLowerCase().includes(q));
          });
          if (!secGroups.length) return null;
          const isOpen = q ? true : !!openSections[sec.key];

          return (
            <div key={sec.key} className="lc-box-sec">
              <button
                className="lc-box-sec-head"
                onClick={() => setOpenSections((p) => ({ ...p, [sec.key]: !isOpen }))}
              >
                {isOpen ? <ChevronDown className="w-3.5 h-3.5" /> : <ChevronRight className="w-3.5 h-3.5" />}
                <span>{sec.label}</span>
              </button>

              {isOpen && (
                <div className="lc-box-sec-body">
                  {secGroups.map((g) => {
                    const remaining = box[g.key] ?? [];
                    const total = g.cards.reduce((n, id) => n + CARDS[id].qty, 0);
                    const isExpanded = expandedGroup === g.key || (q.length > 1 && g.cards.some((id) => CARDS[id].name.toLowerCase().includes(q)));
                    const previewDef = CARDS[g.cards[0]];

                    return (
                      <div key={g.key} className="lc-box-group">
                        <div
                          className={remaining.length ? 'lc-box-group-row' : 'lc-box-group-row is-empty'}
                          onPointerDown={(e) => {
                            if (remaining.length) startBoxDrag(e, { kind: 'box', group: g.key });
                          }}
                        >
                          <img
                            src={previewDef.thumb}
                            alt=""
                            className="lc-box-thumb"
                            draggable={false}
                          />
                          <div
                            className="lc-box-group-info"
                            onClick={() => setExpandedGroup(expandedGroup === g.key ? null : g.key)}
                          >
                            <div className="lc-box-group-name">{g.label}</div>
                            <div className="lc-box-group-meta">
                              {remaining.length} / {total} in box •{' '}
                              <span className="underline">view cards</span>
                            </div>
                          </div>
                          {remaining.length > 0 && (
                            <button
                              className="lc-box-deal-btn"
                              onPointerDown={(e) => e.stopPropagation()}
                              onClick={() => spawnToTableCenter({ kind: 'box', group: g.key })}
                              title="Pull whole pile onto the table"
                            >
                              Pull
                            </button>
                          )}
                        </div>

                        {isExpanded && (
                          <div className="lc-box-cards">
                            {g.cards.map((defId) => {
                              const def = CARDS[defId];
                              if (q && !g.label.toLowerCase().includes(q) && !def.name.toLowerCase().includes(q)) {
                                return null;
                              }
                              const countInBox = remaining.filter((c) => c.def === defId).length;
                              return (
                                <div
                                  key={defId}
                                  className={countInBox ? 'lc-box-card-item' : 'lc-box-card-item is-empty'}
                                  onPointerDown={(e) => {
                                    if (countInBox) startBoxDrag(e, { kind: 'box', group: g.key, def: defId });
                                  }}
                                  onPointerEnter={() => {
                                    const ui = useUI.getState();
                                    if (!ui.drag) ui.set({ hover: { kind: 'def', def: defId, faceUp: true } });
                                  }}
                                  onPointerLeave={() => {
                                    const ui = useUI.getState();
                                    if (ui.hover?.kind === 'def' && ui.hover.def === defId) ui.set({ hover: null });
                                  }}
                                >
                                  <div className="lc-box-card-img-wrap">
                                    <img src={def.thumb} alt={def.name} draggable={false} />
                                    <button
                                      className="lc-box-card-zoom"
                                      onPointerDown={(e) => e.stopPropagation()}
                                      onClick={() => useUI.getState().set({ inspect: defId })}
                                      title="Inspect full-res card"
                                    >
                                      <Eye className="w-3 h-3" />
                                    </button>
                                  </div>
                                  <div className="lc-box-card-caption">
                                    <span className="truncate">{def.name}</span>
                                    <span className="lc-box-card-qty">×{countInBox}</span>
                                  </div>
                                </div>
                              );
                            })}
                          </div>
                        )}
                      </div>
                    );
                  })}
                </div>
              )}
            </div>
          );
        })}
      </div>
    </aside>
  );
}
