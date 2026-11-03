import type { CardInstance, CardDefinition } from '../../types/game';
import { CardView } from '../UI/CardView';
import { AlertTriangle, Swords, Helicopter } from 'lucide-react';

interface CombatZoneProps {
  combatZone: CardInstance[];
  attackPoints: number;
  onFightEnemy: (instanceId: string, isCombatZone: boolean) => void;
  onEvacuateChoppa?: () => void;
  onInspectCard: (card: CardInstance | CardDefinition) => void;
}

export const CombatZone = ({
  combatZone,
  attackPoints,
  onFightEnemy,
  onEvacuateChoppa,
  onInspectCard,
}: CombatZoneProps) => {
  const enemiesCount = combatZone.filter((c) => c.definition.cardType === 'Enemy').length;

  return (
    <div
      className={`w-full rounded-2xl p-4 border transition-all duration-300 backdrop-blur-sm ${
        enemiesCount > 0
          ? 'bg-red-950/20 border-red-600/60 shadow-xl shadow-red-950/40'
          : 'bg-[#090e0b]/80 border-emerald-900/40'
      }`}
    >
      {/* Header */}
      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-red-900/40 pb-3 mb-4">
        <div className="flex items-center space-x-2">
          <AlertTriangle
            className={`w-5 h-5 ${
              enemiesCount > 0 ? 'text-red-500 animate-bounce' : 'text-neutral-500'
            }`}
          />
          <h3
            className={`font-mono font-bold text-base tracking-wider uppercase ${
              enemiesCount > 0 ? 'text-red-400' : 'text-neutral-400'
            }`}
          >
            Combat Zone (Direct Engagement)
          </h3>
        </div>
        <div className="text-xs font-mono">
          {enemiesCount > 0 ? (
            <span className="px-2 py-0.5 rounded bg-red-900 border border-red-500 text-red-200 font-bold animate-pulse">
              WARNING: {enemiesCount} {enemiesCount === 1 ? 'ENEMY' : 'ENEMIES'} WILL STRIKE AT TURN END!
            </span>
          ) : (
            <span className="text-neutral-500 font-mono">Perimeter Secure</span>
          )}
        </div>
      </div>

      {/* Cards in Combat Zone */}
      {combatZone.length === 0 ? (
        <div className="flex flex-col items-center justify-center p-8 text-neutral-600 font-mono text-xs">
          <Swords className="w-8 h-8 opacity-20 mb-2" />
          <span>No hostiles currently in direct combat zone.</span>
        </div>
      ) : (
        <div className="flex flex-wrap gap-4 items-center">
          {combatZone.map((card) => {
            const isEnemy = card.definition.cardType === 'Enemy';
            const isChoppa = card.definition.id === 'act3_the_choppa';
            const health = card.definition.enemyHealth || 0;
            const canFight = isEnemy && attackPoints >= health;
            const canEvac = isChoppa && attackPoints >= 3;

            return (
              <div key={card.instanceId} className="flex flex-col items-center gap-2">
                <CardView
                  card={card}
                  size="md"
                  onInspect={() => onInspectCard(card)}
                  onAction={() => {
                    if (canFight) onFightEnemy(card.instanceId, true);
                    else if (canEvac && onEvacuateChoppa) onEvacuateChoppa();
                  }}
                  actionLabel={
                    canFight
                      ? `Fight (${health} ATK)`
                      : canEvac
                      ? 'Board Choppa (3 ATK)'
                      : isEnemy
                      ? `Need ${health} ATK`
                      : undefined
                  }
                  actionDisabled={isEnemy ? !canFight : isChoppa ? !canEvac : true}
                  actionVariant={isChoppa ? 'recruit' : 'danger'}
                />
                {isChoppa && (
                  <div className="flex items-center gap-1 text-[11px] font-mono text-amber-400">
                    <Helicopter className="w-3.5 h-3.5" />
                    <span>Evac Extraction Zone</span>
                  </div>
                )}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};
