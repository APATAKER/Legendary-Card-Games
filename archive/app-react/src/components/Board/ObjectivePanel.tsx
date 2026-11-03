import type { CardInstance, CardDefinition } from '../../types/game';
import { OBJECTIVE_1, OBJECTIVE_2, OBJECTIVE_3, LOCATION_VAL_VERDE } from '../../data/cards';
import { CardView } from '../UI/CardView';
import { Target, CheckCircle2, Flag } from 'lucide-react';

interface ObjectivePanelProps {
  currentObjectiveIndex: number; // 0, 1, 2
  objectivesCompleted: boolean[];
  operationsZone: CardInstance[];
  onResolveGoal: (goalCardId: string) => void;
  onInspectCard: (card: CardInstance | CardDefinition) => void;
}

export const ObjectivePanel = ({
  currentObjectiveIndex,
  objectivesCompleted,
  operationsZone,
  onResolveGoal,
  onInspectCard,
}: ObjectivePanelProps) => {
  const objectives = [OBJECTIVE_1, OBJECTIVE_2, OBJECTIVE_3];
  const activeObj = objectives[currentObjectiveIndex];

  return (
    <div className="w-full bg-[#08120c]/90 border border-emerald-500/30 rounded-2xl p-4 shadow-xl backdrop-blur-sm">
      {/* Header */}
      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-emerald-900/60 pb-3 mb-3">
        <div className="flex items-center space-x-2">
          <Target className="w-5 h-5 text-emerald-400" />
          <h3 className="font-mono font-bold text-base text-emerald-300 tracking-wider uppercase">
            Mission Dossier & Objectives
          </h3>
        </div>
        <div className="text-xs font-mono text-emerald-500 flex items-center gap-1.5">
          <Flag className="w-3.5 h-3.5" />
          <span>Location: {LOCATION_VAL_VERDE.name}</span>
        </div>
      </div>

      {/* 3 Objectives Status Bars */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-3 mb-4">
        {objectives.map((obj, idx) => {
          const isDone = objectivesCompleted[idx];
          const isActive = currentObjectiveIndex === idx;

          return (
            <div
              key={obj.id}
              onClick={() => onInspectCard(obj)}
              className={`p-3 rounded-xl border flex items-center justify-between cursor-pointer transition-all ${
                isDone
                  ? 'border-emerald-500/60 bg-emerald-950/30 text-emerald-300'
                  : isActive
                  ? 'border-amber-500 bg-amber-950/30 text-amber-200 shadow-md shadow-amber-950/40'
                  : 'border-neutral-800 bg-neutral-950/40 text-neutral-500'
              }`}
            >
              <div className="flex items-center space-x-2.5 truncate">
                {isDone ? (
                  <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0" />
                ) : (
                  <span
                    className={`w-5 h-5 rounded-full flex items-center justify-center text-[10px] font-mono font-bold border shrink-0 ${
                      isActive
                        ? 'border-amber-400 text-amber-300 bg-amber-900/40 animate-pulse'
                        : 'border-neutral-700 text-neutral-600'
                    }`}
                  >
                    {idx + 1}
                  </span>
                )}
                <div className="truncate">
                  <span className="text-xs font-mono font-bold block truncate">
                    {obj.name}
                  </span>
                  <span className="text-[10px] opacity-75 font-mono block">
                    {isDone ? 'Completed' : isActive ? 'Active Objective' : 'Classified'}
                  </span>
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* Active Objective Briefing */}
      <div className="p-3 bg-neutral-950/70 border border-emerald-900/40 rounded-xl flex flex-col md:flex-row items-center justify-between gap-4">
        <div className="text-xs font-sans text-neutral-200 leading-relaxed">
          <strong className="text-emerald-400 font-mono block mb-0.5">
            Objective {currentObjectiveIndex + 1}: {activeObj.name}
          </strong>
          <span className="text-neutral-300">{activeObj.rulesText}</span>
        </div>

        {/* If Defensive Position in Operations Zone */}
        {operationsZone.length > 0 && (
          <div className="flex items-center gap-3 shrink-0">
            {operationsZone.map((opCard) => (
              <div key={opCard.instanceId} className="flex items-center gap-2">
                <CardView
                  card={opCard}
                  size="sm"
                  onInspect={() => onInspectCard(opCard)}
                  onAction={() => onResolveGoal(opCard.definition.id)}
                  actionLabel="Secure (6 REC)"
                  actionVariant="recruit"
                />
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
};
