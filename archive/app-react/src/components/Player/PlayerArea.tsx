import type { Player, CardInstance, CardDefinition } from '../../types/game';
import { CardView } from '../UI/CardView';
import { 
  Heart, 
  Swords, 
  Star, 
  Layers, 
  Trash2, 
  Skull, 
  Target, 
  FastForward,
  Activity
} from 'lucide-react';

interface PlayerAreaProps {
  player: Player;
  recruitPoints: number;
  attackPoints: number;
  onPlayCard: (instanceId: string) => void;
  onPlayAllCards: () => void;
  onEndTurn: () => void;
  onInspectCard: (card: CardInstance | CardDefinition) => void;
  turnCount: number;
}

export const PlayerArea = ({
  player,
  recruitPoints,
  attackPoints,
  onPlayCard,
  onPlayAllCards,
  onEndTurn,
  onInspectCard,
  turnCount,
}: PlayerAreaProps) => {
  const healthPercent = Math.max(0, Math.min(100, (player.health / player.maxHealth) * 100));

  return (
    <div className="w-full bg-[#060c08]/95 border-2 border-emerald-500/40 rounded-2xl p-4 shadow-2xl backdrop-blur-md flex flex-col space-y-4">
      {/* Top HUD: Player Profile & Resource Counters */}
      <div className="flex flex-wrap items-center justify-between gap-4 border-b border-emerald-900/60 pb-4">
        {/* Left: Avatar & Health */}
        <div className="flex items-center space-x-3.5">
          {/* Avatar Picture */}
          <div
            className="relative w-16 h-20 rounded-xl overflow-hidden border-2 border-emerald-500/70 shadow-lg shadow-black cursor-pointer group"
            onClick={() => onInspectCard({
              id: 'avatar',
              name: player.roleName,
              imagePath: player.avatarImagePath,
              cardType: 'Avatar',
              group: 'Avatars',
              rulesText: `Role: ${player.roleName}. Max Health: ${player.maxHealth}.`,
            })}
          >
            <img
              src={player.avatarImagePath}
              alt={player.roleName}
              className="w-full h-full object-cover group-hover:scale-105 transition-transform"
            />
            <div className="absolute inset-0 bg-gradient-to-t from-black/80 via-transparent to-transparent" />
            <span className="absolute bottom-1 inset-x-0 text-center font-mono text-[9px] text-emerald-300 font-bold truncate px-1">
              {player.roleName}
            </span>
          </div>

          {/* Name & Health Bar */}
          <div className="flex flex-col space-y-1.5 min-w-[160px]">
            <div className="flex items-center space-x-2">
              <span className="font-bold text-white text-sm tracking-wide">
                {player.name}
              </span>
              <span className="px-1.5 py-0.5 rounded bg-emerald-950/80 border border-emerald-500/50 text-emerald-300 font-mono text-[10px] font-bold">
                Turn {turnCount}
              </span>
              {player.marks > 0 && (
                <span className="flex items-center gap-0.5 px-1.5 py-0.5 rounded bg-red-950 border border-red-500 text-red-300 font-mono text-[10px] font-bold">
                  <Target className="w-3 h-3" /> {player.marks} Marks
                </span>
              )}
            </div>

            {/* Health Meter */}
            <div className="flex items-center space-x-2">
              <div className="flex-1 h-3.5 bg-neutral-900 rounded-full overflow-hidden border border-emerald-900/60 p-0.5">
                <div
                  className={`h-full rounded-full transition-all duration-500 ${
                    healthPercent > 50
                      ? 'bg-gradient-to-r from-emerald-500 to-green-400'
                      : healthPercent > 25
                      ? 'bg-gradient-to-r from-amber-500 to-yellow-400'
                      : 'bg-gradient-to-r from-red-600 to-red-400 animate-pulse'
                  }`}
                  style={{ width: `${healthPercent}%` }}
                />
              </div>
              <span className="text-xs font-mono font-bold text-emerald-300 flex items-center gap-1">
                <Heart className="w-3.5 h-3.5 text-red-500" /> {player.health}/{player.maxHealth}
              </span>
            </div>

            {/* Deck & Discard Counts */}
            <div className="flex items-center space-x-3 text-[11px] font-mono text-neutral-400">
              <span className="flex items-center gap-1">
                <Layers className="w-3 h-3 text-emerald-400" /> Deck: {player.deck.length}
              </span>
              <span className="flex items-center gap-1">
                <Trash2 className="w-3 h-3 text-neutral-500" /> Discard: {player.discard.length}
              </span>
              <span className="flex items-center gap-1">
                <Skull className="w-3 h-3 text-red-400" /> Strikes: {player.strikes.length}
              </span>
            </div>
          </div>
        </div>

        {/* Center: Tactical Resource Display */}
        <div className="flex items-center space-x-4">
          {/* Recruit Points */}
          <div className="flex items-center space-x-2.5 bg-amber-950/40 border-2 border-amber-500/60 px-4 py-2 rounded-xl shadow-lg shadow-amber-950/30">
            <Star className="w-7 h-7 text-amber-400 fill-amber-400/20" />
            <div className="flex flex-col">
              <span className="text-[10px] font-mono uppercase tracking-widest text-amber-300/80">
                Recruit Points
              </span>
              <span className="text-2xl font-black font-mono text-amber-300">
                {recruitPoints}
              </span>
            </div>
          </div>

          {/* Attack Points */}
          <div className="flex items-center space-x-2.5 bg-red-950/40 border-2 border-red-500/60 px-4 py-2 rounded-xl shadow-lg shadow-red-950/30">
            <Swords className="w-7 h-7 text-red-400 fill-red-400/20" />
            <div className="flex flex-col">
              <span className="text-[10px] font-mono uppercase tracking-widest text-red-300/80">
                Attack Points
              </span>
              <span className="text-2xl font-black font-mono text-red-300">
                {attackPoints}
              </span>
            </div>
          </div>
        </div>

        {/* Right: Turn Controls */}
        <div className="flex items-center space-x-2">
          {player.hand.length > 0 && (
            <button
              onClick={onPlayAllCards}
              className="px-3.5 py-2 rounded-xl bg-emerald-950/80 hover:bg-emerald-800 border border-emerald-500/50 text-emerald-300 hover:text-white font-mono font-bold text-xs uppercase tracking-wider transition-all flex items-center gap-1.5"
            >
              <FastForward className="w-4 h-4" /> Play All Hand
            </button>
          )}

          <button
            onClick={onEndTurn}
            className="px-5 py-2.5 rounded-xl bg-gradient-to-r from-red-700 to-red-600 hover:from-red-600 hover:to-red-500 text-white font-mono font-black text-sm uppercase tracking-wider shadow-lg shadow-red-950/60 border border-red-400 flex items-center gap-2 transition-all hover:scale-105 active:scale-95"
          >
            <span>End Turn</span>
            <span className="text-[11px] font-normal opacity-80">(Strike Phase)</span>
          </button>
        </div>
      </div>

      {/* Cards Deployed In-Play Area */}
      {player.inPlay.length > 0 && (
        <div className="p-3 bg-neutral-950/60 border border-emerald-900/40 rounded-xl">
          <div className="flex items-center justify-between mb-2">
            <span className="text-xs font-mono uppercase tracking-wider text-emerald-400 font-bold flex items-center gap-1.5">
              <Activity className="w-3.5 h-3.5" /> Deployed This Turn ({player.inPlay.length})
            </span>
          </div>
          <div className="flex flex-wrap gap-2.5 items-center">
            {player.inPlay.map((card) => (
              <CardView
                key={card.instanceId}
                card={card}
                size="sm"
                onInspect={() => onInspectCard(card)}
              />
            ))}
          </div>
        </div>
      )}

      {/* Hand Tray */}
      <div>
        <div className="flex items-center justify-between mb-2 px-1">
          <span className="text-xs font-mono uppercase tracking-wider text-emerald-300 font-bold">
            Tactical Hand ({player.hand.length} Cards)
          </span>
          <span className="text-[11px] font-mono text-neutral-400">
            Click 'Deploy' to play cards and generate Recruit & Attack
          </span>
        </div>

        {player.hand.length === 0 ? (
          <div className="p-8 rounded-xl border border-dashed border-neutral-800 text-center text-neutral-600 font-mono text-xs">
            Hand empty! Spend accumulated Recruit & Attack or click 'End Turn' to draw 6 new cards.
          </div>
        ) : (
          <div className="flex flex-wrap gap-3 items-center">
            {player.hand.map((card) => (
              <div key={card.instanceId} className="flex flex-col items-center">
                <CardView
                  card={card}
                  size="md"
                  onInspect={() => onInspectCard(card)}
                  onAction={() => onPlayCard(card.instanceId)}
                  actionLabel="Deploy"
                  actionVariant="default"
                />
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
};
