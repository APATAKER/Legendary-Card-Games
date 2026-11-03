import type { WildernessSpace, CardInstance, CardDefinition } from '../../types/game';
import { CardView } from '../UI/CardView';
import { Radar, Swords, ShieldAlert, ArrowLeft } from 'lucide-react';

interface WildernessTrackProps {
  spaces: WildernessSpace[];
  attackPoints: number;
  onScanSpace: (index: number) => void;
  onFightEnemy: (instanceId: string, isCombatZone: boolean) => void;
  onInspectCard: (card: CardInstance | CardDefinition) => void;
  onWipeGuerrillaCamp?: () => void;
  currentObjectiveIndex: number;
}

export const WildernessTrack = ({
  spaces,
  attackPoints,
  onScanSpace,
  onFightEnemy,
  onInspectCard,
  onWipeGuerrillaCamp,
  currentObjectiveIndex,
}: WildernessTrackProps) => {
  return (
    <div className="w-full bg-[#070e0a]/90 border border-emerald-500/30 rounded-2xl p-4 shadow-xl backdrop-blur-sm">
      {/* Track Header */}
      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-emerald-900/60 pb-3 mb-4">
        <div className="flex items-center space-x-2">
          <Radar className="w-5 h-5 text-emerald-400 animate-pulse" />
          <h3 className="font-mono font-bold text-base text-emerald-300 tracking-wider uppercase">
            The Wilderness Track (Val Verdean Jungle)
          </h3>
        </div>
        <div className="flex items-center space-x-2 text-xs font-mono text-emerald-500">
          <span className="flex items-center gap-1">
            <ArrowLeft className="w-3.5 h-3.5" /> Enemy Advance Direction (Right to Left)
          </span>
        </div>
      </div>

      {/* Spaces Grid (Underground on right, Hills on left) */}
      {/* Note: In official rules, Underground is entry space (right), cards advance left towards Hills (space 5) into Combat Zone */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-3">
        {spaces.map((space, idx) => {
          const card = space.card;
          const isFaceDown = card?.isFaceDown ?? false;
          const canScan = isFaceDown && attackPoints >= space.scanCost;
          const isEnemy = card && !isFaceDown && card.definition.cardType === 'Enemy';
          const canFight = isEnemy && attackPoints >= (card.definition.enemyHealth || 0);

          // Check if Guerrilla Camp is in this space
          const isGuerrillaCampHere = card && !isFaceDown && card.definition.id === 'act1_guerrilla_camp';
          const canWipeCamp = isGuerrillaCampHere && currentObjectiveIndex === 0 && attackPoints >= 1;

          return (
            <div
              key={space.name}
              className={`relative flex flex-col items-center justify-between rounded-xl p-2.5 border transition-all duration-300 ${
                space.card
                  ? 'border-emerald-600/60 bg-emerald-950/20 shadow-md shadow-black'
                  : 'border-dashed border-emerald-900/40 bg-neutral-950/40'
              }`}
            >
              {/* Space Badge */}
              <div className="w-full flex justify-between items-center mb-2 px-1 text-[11px] font-mono">
                <span className="font-bold text-emerald-400 uppercase tracking-wider">
                  {idx + 1}. {space.name}
                </span>
                <span className="text-emerald-500/80 bg-emerald-950/80 px-1.5 py-0.5 rounded border border-emerald-800/60">
                  Scan: {space.scanCost} <Swords className="w-2.5 h-2.5 inline" />
                </span>
              </div>

              {/* Card Container */}
              <div className="flex items-center justify-center my-1 min-h-[170px] w-full">
                {card ? (
                  <div className="flex flex-col items-center gap-1.5 w-full">
                    <CardView
                      card={card}
                      size="sm"
                      onInspect={() => onInspectCard(card)}
                      onAction={() => {
                        if (canScan) onScanSpace(idx);
                        else if (canFight) onFightEnemy(card.instanceId, false);
                        else if (canWipeCamp && onWipeGuerrillaCamp) onWipeGuerrillaCamp();
                      }}
                      actionLabel={
                        isFaceDown
                          ? `Scan (${space.scanCost} ATK)`
                          : canWipeCamp
                          ? 'Wipe Camp (1 ATK)'
                          : isEnemy
                          ? `Fight (${card.definition.enemyHealth} ATK)`
                          : undefined
                      }
                      actionDisabled={isFaceDown ? !canScan : canWipeCamp ? !canWipeCamp : isEnemy ? !canFight : true}
                      actionVariant={isFaceDown ? 'scan' : canWipeCamp ? 'recruit' : 'danger'}
                    />
                  </div>
                ) : (
                  <div className="flex flex-col items-center justify-center text-neutral-600 space-y-1 h-36">
                    <ShieldAlert className="w-8 h-8 opacity-20" />
                    <span className="text-[10px] font-mono tracking-widest uppercase">Clear Sector</span>
                  </div>
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
