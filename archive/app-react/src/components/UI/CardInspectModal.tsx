import { useEffect } from 'react';
import type { CardInstance, CardDefinition } from '../../types/game';
import { X, Shield, Swords, Star, Coins, Sparkles, BookOpen } from 'lucide-react';

interface CardInspectModalProps {
  card: CardInstance | CardDefinition | null;
  onClose: () => void;
}

export const CardInspectModal = ({ card, onClose }: CardInspectModalProps) => {
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [onClose]);

  if (!card) return null;

  const def = 'definition' in card ? card.definition : card;

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-in fade-in duration-200"
      onClick={onClose}
    >
      <div
        className="relative max-w-2xl w-full bg-[#0a120e] border-2 border-emerald-500/60 rounded-2xl shadow-2xl shadow-emerald-950/80 overflow-hidden flex flex-col md:flex-row max-h-[90vh]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Close Button */}
        <button
          onClick={onClose}
          className="absolute top-3 right-3 z-20 p-2 rounded-full bg-black/80 hover:bg-emerald-600 border border-emerald-500/40 text-emerald-300 hover:text-white transition-colors"
        >
          <X className="w-5 h-5" />
        </button>

        {/* Left: Card Artwork */}
        <div className="md:w-1/2 p-6 flex items-center justify-center bg-black/50 border-b md:border-b-0 md:border-r border-emerald-900/50">
          <div className="relative w-64 h-96 rounded-xl overflow-hidden border-2 border-emerald-500/40 shadow-2xl shadow-black">
            <img
              src={def.imagePath}
              alt={def.name}
              className="w-full h-full object-cover"
            />
          </div>
        </div>

        {/* Right: Card Dossier & Mechanics */}
        <div className="md:w-1/2 p-6 flex flex-col justify-between overflow-y-auto font-sans text-neutral-200">
          <div>
            {/* Header / Name */}
            <div className="border-b border-emerald-800/60 pb-3 mb-4">
              <span className="text-xs font-mono uppercase tracking-widest text-emerald-500 block mb-1">
                {def.group}
              </span>
              <h2 className="text-2xl font-black text-white tracking-wide">
                {def.name}
              </h2>
              {def.subtitle && (
                <div className="flex items-center space-x-2 mt-1">
                  <span className="text-sm font-semibold text-emerald-400 font-mono">
                    {def.subtitle}
                  </span>
                  {def.roleClass && (
                    <span className="text-xs px-2 py-0.5 rounded-full bg-emerald-950 border border-emerald-600/50 text-emerald-300 font-mono">
                      {def.roleClass} Class
                    </span>
                  )}
                </div>
              )}
            </div>

            {/* Stat Counters Grid */}
            <div className="grid grid-cols-2 gap-2 mb-4">
              {def.cost !== undefined && def.cost > 0 && (
                <div className="flex items-center space-x-2 bg-yellow-950/50 border border-yellow-700/60 p-2 rounded-lg">
                  <Coins className="w-5 h-5 text-yellow-400" />
                  <div>
                    <span className="text-[10px] text-neutral-400 block uppercase font-mono">Cost</span>
                    <span className="text-lg font-bold text-yellow-300 font-mono">{def.cost} Recruit</span>
                  </div>
                </div>
              )}

              {def.attack !== undefined && def.attack > 0 && (
                <div className="flex items-center space-x-2 bg-red-950/50 border border-red-700/60 p-2 rounded-lg">
                  <Swords className="w-5 h-5 text-red-400" />
                  <div>
                    <span className="text-[10px] text-neutral-400 block uppercase font-mono">Attack</span>
                    <span className="text-lg font-bold text-red-300 font-mono">+{def.attack} Attack</span>
                  </div>
                </div>
              )}

              {def.recruit !== undefined && def.recruit > 0 && (
                <div className="flex items-center space-x-2 bg-amber-950/50 border border-amber-700/60 p-2 rounded-lg">
                  <Star className="w-5 h-5 text-amber-400" />
                  <div>
                    <span className="text-[10px] text-neutral-400 block uppercase font-mono">Recruit</span>
                    <span className="text-lg font-bold text-amber-300 font-mono">+{def.recruit} Points</span>
                  </div>
                </div>
              )}

              {def.enemyHealth !== undefined && (
                <div className="flex items-center space-x-2 bg-red-950/80 border border-red-500 p-2 rounded-lg col-span-2">
                  <Shield className="w-5 h-5 text-red-400" />
                  <div>
                    <span className="text-[10px] text-neutral-300 block uppercase font-mono">Enemy Toughness</span>
                    <span className="text-xl font-extrabold text-white font-mono">{def.enemyHealth} Attack Required</span>
                  </div>
                </div>
              )}

              {def.strikeDamage !== undefined && (
                <div className="flex items-center space-x-2 bg-red-950/80 border border-red-500 p-2 rounded-lg col-span-2">
                  <Swords className="w-5 h-5 text-red-400" />
                  <div>
                    <span className="text-[10px] text-neutral-300 block uppercase font-mono">Strike Damage</span>
                    <span className="text-xl font-extrabold text-red-300 font-mono">{def.strikeDamage} Damage</span>
                  </div>
                </div>
              )}
            </div>

            {/* Keywords */}
            {def.keywords && def.keywords.length > 0 && (
              <div className="mb-4">
                <span className="text-xs font-mono text-neutral-400 uppercase block mb-1.5 flex items-center gap-1">
                  <Sparkles className="w-3.5 h-3.5 text-emerald-400" /> Keywords:
                </span>
                <div className="flex flex-wrap gap-1.5">
                  {def.keywords.map((kw) => (
                    <span
                      key={kw}
                      className="px-2.5 py-1 rounded bg-emerald-900/60 border border-emerald-500/50 text-emerald-200 text-xs font-mono font-bold"
                    >
                      {kw}
                    </span>
                  ))}
                </div>
              </div>
            )}

            {/* Rules Text */}
            {def.rulesText && (
              <div className="bg-[#050c08] border border-emerald-800/40 p-3.5 rounded-xl">
                <span className="text-xs font-mono text-emerald-500 uppercase block mb-1 flex items-center gap-1">
                  <BookOpen className="w-3.5 h-3.5" /> Official Card Rules:
                </span>
                <p className="text-sm leading-relaxed text-neutral-100 font-sans">
                  {def.rulesText}
                </p>
              </div>
            )}
          </div>

          {/* Close Action */}
          <div className="mt-6 pt-3 border-t border-emerald-900/50 flex justify-end">
            <button
              onClick={onClose}
              className="px-5 py-2 rounded-lg bg-emerald-700 hover:bg-emerald-600 text-black font-mono font-bold text-sm uppercase tracking-wider transition-colors"
            >
              Close Dossier
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
