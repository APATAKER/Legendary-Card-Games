import { memo } from 'react';
import { CARDS } from '../catalog.generated';
import type { CardInst } from '../types';

const BACK = (
  <div className="lc-back-art">
    <div className="lc-back-frame">
      <svg className="lc-back-tri" viewBox="0 0 100 90" aria-hidden>
        <circle cx="50" cy="12" r="7" />
        <circle cx="14" cy="76" r="7" />
        <circle cx="86" cy="76" r="7" />
      </svg>
      <div className="lc-back-title">
        <span>Legendary</span>
        <b>Encounters</b>
      </div>
      <div className="lc-back-sub">Predator</div>
    </div>
  </div>
);

export function CardBack() {
  return BACK;
}

/** A single card with a real 3D flip. Size is driven by `width` (height = 1.4 × width). */
export const CardView = memo(function CardView({ card, width }: { card: CardInst; width: number }) {
  const def = CARDS[card.def];
  return (
    <div className="lc-card" style={{ width, height: width * 1.4, fontSize: width / 10 }}>
      <div className={card.faceUp ? 'lc-card-inner' : 'lc-card-inner is-down'}>
        <div className="lc-face lc-front">
          <img src={def.thumb} className={def.land ? 'is-land' : undefined} alt={def.name} draggable={false} decoding="async" />
        </div>
        <div className="lc-face lc-back">{BACK}</div>
      </div>
    </div>
  );
});

/** Box-shadow layers that make a pile look as thick as it really is. */
export function stackShadow(n: number, lifted = false): string {
  const depth = n > 1 ? Math.min(12, Math.ceil(n / 3)) : 0;
  const layers: string[] = [];
  for (let i = 1; i <= depth; i++) {
    layers.push(`${(i * 0.55).toFixed(2)}px ${(i * 0.95).toFixed(2)}px 0 ${i % 2 ? '#e7e2d6' : '#9d988c'}`);
  }
  layers.push(
    lifted
      ? `${10 + depth}px ${26 + depth * 1.5}px 34px rgba(0,0,0,.55)`
      : `${1 + depth * 0.6}px ${3 + depth}px ${8 + depth}px rgba(0,0,0,.55)`,
  );
  return layers.join(', ');
}
