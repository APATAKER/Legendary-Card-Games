import { useEffect } from 'react';
import confetti from 'canvas-confetti';
import { Trophy, Skull, RotateCcw, Helicopter } from 'lucide-react';

interface GameOverModalProps {
  isVictory: boolean;
  victoryType?: 'Major' | 'Minor';
  isDefeat: boolean;
  defeatReason?: string;
  turnCount: number;
  objectivesCleared: number;
  onRestart: () => void;
}

export const GameOverModal = ({
  isVictory,
  victoryType,
  isDefeat,
  defeatReason,
  turnCount,
  objectivesCleared,
  onRestart,
}: GameOverModalProps) => {
  useEffect(() => {
    if (isVictory) {
      confetti({
        particleCount: 120,
        spread: 80,
        origin: { y: 0.6 },
        colors: ['#00ff88', '#ffaa00', '#00e5ff'],
      });
    }
  }, [isVictory]);

  if (!isVictory && !isDefeat) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/90 backdrop-blur-md animate-in fade-in duration-300">
      <div
        className={`max-w-lg w-full rounded-2xl p-6 border-2 shadow-2xl flex flex-col items-center text-center space-y-4 ${
          isVictory
            ? 'bg-[#09150e] border-emerald-500 shadow-emerald-950/80'
            : 'bg-[#150a0a] border-red-600 shadow-red-950/80'
        }`}
      >
        {/* Icon */}
        <div
          className={`w-20 h-20 rounded-full flex items-center justify-center border-2 ${
            isVictory
              ? 'bg-emerald-950/80 border-emerald-400 text-emerald-300 shadow-lg shadow-emerald-500/20'
              : 'bg-red-950/80 border-red-500 text-red-400 shadow-lg shadow-red-500/20'
          }`}
        >
          {isVictory ? (
            victoryType === 'Major' ? (
              <Trophy className="w-10 h-10 animate-bounce" />
            ) : (
              <Helicopter className="w-10 h-10 animate-bounce" />
            )
          ) : (
            <Skull className="w-10 h-10 animate-pulse" />
          )}
        </div>

        {/* Title */}
        <div>
          <h2 className="text-3xl font-black tracking-wider uppercase font-mono text-white">
            {isVictory
              ? victoryType === 'Major'
                ? 'MAJOR VICTORY!'
                : 'MINOR VICTORY!'
              : 'MISSION FAILED - KIA'}
          </h2>
          <p
            className={`font-mono text-sm mt-1 ${
              isVictory ? 'text-emerald-400' : 'text-red-400'
            }`}
          >
            {isVictory
              ? victoryType === 'Major'
                ? 'The Jungle Hunter is dead! If it bleeds, we can kill it!'
                : 'All surviving operatives evacuated to The Choppa!'
              : defeatReason || 'All fireteam operatives were wiped out by the Predator.'}
          </p>
        </div>

        {/* Mission Stats */}
        <div className="w-full bg-black/60 border border-neutral-800 rounded-xl p-3 grid grid-cols-2 gap-2 text-xs font-mono">
          <div className="p-2 bg-neutral-950/80 rounded border border-neutral-800">
            <span className="text-neutral-500 block uppercase">Turns Survived</span>
            <span className="text-base font-bold text-white">{turnCount} Turns</span>
          </div>
          <div className="p-2 bg-neutral-950/80 rounded border border-neutral-800">
            <span className="text-neutral-500 block uppercase">Objectives Completed</span>
            <span className="text-base font-bold text-emerald-400">{objectivesCleared} / 3</span>
          </div>
        </div>

        {/* Play Again Button */}
        <button
          onClick={onRestart}
          className={`w-full py-3 rounded-xl font-mono font-bold text-sm uppercase tracking-wider shadow-lg flex items-center justify-center gap-2 transition-transform hover:scale-105 active:scale-95 ${
            isVictory
              ? 'bg-emerald-500 hover:bg-emerald-400 text-black shadow-emerald-500/30'
              : 'bg-red-600 hover:bg-red-500 text-white shadow-red-600/30'
          }`}
        >
          <RotateCcw className="w-4 h-4" /> Start New Operation
        </button>
      </div>
    </div>
  );
};
