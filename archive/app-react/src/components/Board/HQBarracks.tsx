import type { CardInstance, CardDefinition } from '../../types/game';
import { CardView } from '../UI/CardView';
import { Users, Award } from 'lucide-react';

interface HQBarracksProps {
  hq: (CardInstance | null)[];
  recruitPoints: number;
  barracksDeckCount: number;
  commanderDeckCount: number;
  onRecruitHQCard: (index: number) => void;
  onRecruitCommander: () => void;
  onInspectCard: (card: CardInstance | CardDefinition) => void;
}

export const HQBarracks = ({
  hq,
  recruitPoints,
  barracksDeckCount,
  commanderDeckCount,
  onRecruitHQCard,
  onRecruitCommander,
  onInspectCard,
}: HQBarracksProps) => {
  return (
    <div className="w-full bg-[#0a110d]/90 border border-emerald-500/30 rounded-2xl p-4 shadow-xl backdrop-blur-sm">
      {/* Header */}
      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-emerald-900/60 pb-3 mb-4">
        <div className="flex items-center space-x-2">
          <Users className="w-5 h-5 text-amber-400" />
          <h3 className="font-mono font-bold text-base text-amber-300 tracking-wider uppercase">
            Headquarters & Barracks (Available Recruits)
          </h3>
        </div>
        <div className="flex items-center space-x-3 text-xs font-mono">
          <span className="text-emerald-400">
            Barracks Deck: <strong className="text-white">{barracksDeckCount}</strong>
          </span>
          <span className="text-amber-400">
            Commanders: <strong className="text-white">{commanderDeckCount}</strong>
          </span>
        </div>
      </div>

      {/* Slots grid + Commander Stack */}
      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-6 gap-3 items-start">
        {/* HQ Slots 1 to 5 */}
        {hq.map((card, idx) => {
          if (!card) {
            return (
              <div
                key={`empty_slot_${idx}`}
                className="w-full h-56 rounded-xl border-2 border-dashed border-neutral-800 flex flex-col items-center justify-center text-neutral-600 font-mono text-xs"
              >
                <span>Slot {idx + 1}</span>
                <span className="text-[10px]">Depleted</span>
              </div>
            );
          }

          const cost = card.definition.cost || 0;
          const canAfford = recruitPoints >= cost;

          return (
            <div key={card.instanceId} className="flex flex-col items-center">
              <CardView
                card={card}
                size="md"
                onInspect={() => onInspectCard(card)}
                onAction={() => onRecruitHQCard(idx)}
                actionLabel={`Hire (${cost} REC)`}
                actionDisabled={!canAfford}
                actionVariant="recruit"
              />
            </div>
          );
        })}

        {/* Commander Stack (Slot 6) */}
        <div className="flex flex-col items-center">
          <div
            className="w-40 h-56 rounded-xl border-2 border-amber-500/60 bg-gradient-to-br from-amber-950/40 via-neutral-950 to-black p-3 flex flex-col justify-between items-center shadow-lg shadow-black group cursor-pointer hover:border-amber-400 hover:scale-105 transition-all"
            onClick={onRecruitCommander}
          >
            <div className="w-full flex justify-between items-center text-[10px] font-mono text-amber-400">
              <span>COMMAND</span>
              <Award className="w-4 h-4 text-amber-400" />
            </div>

            <div className="flex flex-col items-center text-center space-y-1.5">
              <div className="w-12 h-12 rounded-full border border-amber-500/50 bg-amber-900/30 flex items-center justify-center text-amber-300">
                <Users className="w-6 h-6" />
              </div>
              <span className="font-bold text-xs text-white uppercase tracking-wider">
                Commander Phillips
              </span>
              <span className="text-[10px] text-amber-300/80 font-mono">
                +2 Recruit • Coordinate
              </span>
            </div>

            <button
              onClick={(e) => {
                e.stopPropagation();
                onRecruitCommander();
              }}
              disabled={recruitPoints < 3 || commanderDeckCount === 0}
              className={`w-full py-1 rounded font-mono font-bold text-xs uppercase tracking-wider transition-colors ${
                recruitPoints >= 3 && commanderDeckCount > 0
                  ? 'bg-amber-500 hover:bg-amber-400 text-black shadow'
                  : 'bg-neutral-800 text-neutral-500 cursor-not-allowed border border-neutral-700'
              }`}
            >
              Hire (3 REC)
            </button>
          </div>
          <span className="text-[10px] font-mono text-neutral-400 mt-1">
            Reserve: {commanderDeckCount} Left
          </span>
        </div>
      </div>
    </div>
  );
};
