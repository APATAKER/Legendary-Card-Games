import { memo, useMemo } from 'react';
import { buildZones, matBounds, playerPanels } from '../layout';
import { useTable } from '../store';
import { useUI } from '../ui';

export const Mat = memo(function Mat() {
  const playerMats = useTable((s) => s.present.playerMats);
  const hoverZoneId = useUI((s) => (s.drag?.hover?.kind === 'zone' ? s.drag.hover.id : null));

  const zones = useMemo(() => buildZones(playerMats), [playerMats]);
  const panels = useMemo(() => playerPanels(playerMats), [playerMats]);
  const bounds = useMemo(() => matBounds(playerMats), [playerMats]);

  return (
    <div className="lc-mat" style={{ width: bounds.w, height: bounds.h }}>
      {/* Main tactical playmat surface */}
      <div className="lc-mat-board">
        <div className="lc-mat-header">
          <span className="lc-mat-brand">LEGENDARY ENCOUNTERS // PREDATOR TABLETOP MAT</span>
          <span className="lc-mat-hint">
            WILDS FLOW: UNDERGROUND ➔ GRAVEYARD ➔ RIVER ➔ RUINS ➔ HILLS ➔ COMBAT ZONE
          </span>
        </div>
      </div>

      {/* Player mat backing panels */}
      {panels.map((p) => (
        <div
          key={p.n}
          className="lc-player-panel"
          style={{ left: p.x, top: p.y, width: p.w, height: p.h }}
        >
          <div className="lc-player-panel-title">PLAYER {p.n} PLAYMAT</div>
        </div>
      ))}

      {/* Individual slots and areas */}
      {zones.map((z) => {
        const active = hoverZoneId === z.id;
        const cls = ['lc-zone', `is-${z.kind}`, `tone-${z.tone ?? 'neutral'}`];
        if (active) cls.push('is-drop-hover');
        return (
          <div
            key={z.id}
            className={cls.join(' ')}
            style={{ left: z.x, top: z.y, width: z.w, height: z.h }}
          >
            <span className="lc-zone-label">{z.label}</span>
          </div>
        );
      })}
    </div>
  );
});
