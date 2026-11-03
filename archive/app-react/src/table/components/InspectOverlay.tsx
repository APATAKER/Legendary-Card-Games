import { useEffect, useState } from 'react';
import { RotateCw, X } from 'lucide-react';
import { CARDS } from '../catalog.generated';
import { GROUP_BY_KEY, useTable } from '../store';
import { useUI } from '../ui';

export function InspectOverlay() {
  const inspect = useUI((s) => s.inspect);
  const hover = useUI((s) => s.hover);
  const previewOn = useUI((s) => s.previewOn);
  const drag = useUI((s) => s.drag);
  const st = useTable((s) => s.present);
  const [rotated, setRotated] = useState(false);

  // Resolve hovered card definition (only when face-up!)
  let hoveredDefId: number | null = null;
  if (hover && !drag) {
    if (hover.kind === 'pile') {
      const p = st.piles.find((q) => q.id === hover.id);
      const top = p?.cards[p.cards.length - 1];
      if (top?.faceUp) hoveredDefId = top.def;
    } else if (hover.kind === 'hand') {
      const h = st.hands[st.activeHand];
      const c = h?.cards[hover.index];
      if (c) hoveredDefId = c.def;
    } else if (hover.kind === 'def' && hover.faceUp) {
      hoveredDefId = hover.def;
    }
  }

  useEffect(() => {
    if (inspect !== null) {
      setRotated(CARDS[inspect]?.land ?? false);
    }
  }, [inspect]);

  useEffect(() => {
    if (inspect === null) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' || e.key === 'z' || e.key === 'Z') {
        useUI.getState().set({ inspect: null });
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [inspect]);

  const hoverDef = hoveredDefId !== null ? CARDS[hoveredDefId] : null;
  const fullDef = inspect !== null ? CARDS[inspect] : null;

  return (
    <>
      {/* Corner magnifier preview when hovering any face-up card */}
      {previewOn && hoverDef && inspect === null && (
        <div className="lc-hover-preview" aria-hidden>
          <img
            src={hoverDef.img}
            alt={hoverDef.name}
            className={hoverDef.land ? 'is-land' : undefined}
          />
          <div className="lc-hover-caption">
            <strong>{hoverDef.name}</strong>
            <span>{GROUP_BY_KEY.get(hoverDef.group)?.label}</span>
          </div>
        </div>
      )}

      {/* Full-screen high-res Inspect modal */}
      {fullDef && (
        <div
          className="lc-modal-backdrop"
          data-nodrop
          onClick={() => useUI.getState().set({ inspect: null })}
        >
          <div className="lc-inspect-dialog" onClick={(e) => e.stopPropagation()}>
            <div className="lc-inspect-bar">
              <div>
                <div className="lc-inspect-title">{fullDef.name}</div>
                <div className="lc-inspect-sub">
                  {GROUP_BY_KEY.get(fullDef.group)?.label} • {fullDef.qty} in box
                </div>
              </div>
              <div className="flex items-center gap-2">
                <button
                  className="lc-btn"
                  onClick={() => setRotated((r) => !r)}
                  title="Rotate image 90°"
                >
                  <RotateCw className="w-4 h-4" />
                  <span>Rotate</span>
                </button>
                <button
                  className="lc-btn"
                  onClick={() => useUI.getState().set({ inspect: null })}
                >
                  <X className="w-4 h-4" />
                  <span>Close (Esc)</span>
                </button>
              </div>
            </div>

            <div className="lc-inspect-stage">
              <img
                src={fullDef.img}
                alt={fullDef.name}
                className={rotated ? 'lc-inspect-img is-rotated' : 'lc-inspect-img'}
                draggable={false}
              />
            </div>
          </div>
        </div>
      )}
    </>
  );
}
