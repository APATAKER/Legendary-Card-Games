import type { CardInstance, CardDefinition } from '../../types/game';
import { Eye, Shield, Swords, Star, Coins } from 'lucide-react';

interface CardViewProps {
  card: CardInstance | CardDefinition;
  isFaceDown?: boolean;
  onInspect?: () => void;
  onAction?: () => void;
  actionLabel?: string;
  actionDisabled?: boolean;
  actionVariant?: 'default' | 'danger' | 'recruit' | 'scan';
  size?: 'sm' | 'md' | 'lg';
  className?: string;
}

export const CardView = ({
  card,
  isFaceDown = false,
  onInspect,
  onAction,
  actionLabel,
  actionDisabled = false,
  actionVariant = 'default',
  size = 'md',
  className = '',
}: CardViewProps) => {
  const def = 'definition' in card ? card.definition : card;
  const faceDown = 'isFaceDown' in card ? card.isFaceDown : isFaceDown;

  const sizeClasses = {
    sm: 'w-28 h-40 text-xs',
    md: 'w-40 h-56 text-sm',
    lg: 'w-52 h-72 text-base',
  };

  const variantStyles = {
    default: 'bg-emerald-600 hover:bg-emerald-500 text-black',
    danger: 'bg-red-600 hover:bg-red-500 text-white animate-pulse',
    recruit: 'bg-amber-500 hover:bg-amber-400 text-black',
    scan: 'bg-cyan-600 hover:bg-cyan-500 text-black',
  };

  // If face down, render tactical camouflage back
  if (faceDown) {
    return (
      <div
        className={`relative ${sizeClasses[size]} rounded-xl border-2 border-emerald-800/80 bg-gradient-to-br from-[#0c1813] via-[#09120e] to-[#050b08] p-2 flex flex-col justify-between items-center shadow-lg shadow-black/80 select-none group cursor-pointer transition-all duration-300 hover:border-emerald-400 hover:scale-[1.03] ${className}`}
        onClick={onAction || onInspect}
      >
        <div className="w-full flex justify-between items-center text-[10px] text-emerald-500 font-mono tracking-wider">
          <span>CLASSIFIED</span>
          <span className="w-2 h-2 rounded-full bg-emerald-500 animate-ping" />
        </div>

        <div className="flex flex-col items-center justify-center space-y-2">
          <div className="w-12 h-12 rounded-full border border-emerald-500/40 flex items-center justify-center bg-emerald-950/40 text-emerald-400">
            <Eye className="w-6 h-6 animate-pulse" />
          </div>
          <span className="font-mono text-emerald-400/90 tracking-widest text-[11px] uppercase text-center font-bold">
            UNSCANNED
          </span>
          <span className="text-[10px] text-emerald-600 font-mono">HIDDEN PRESENCE</span>
        </div>

        {actionLabel && (
          <button
            onClick={(e) => {
              e.stopPropagation();
              if (onAction && !actionDisabled) onAction();
            }}
            disabled={actionDisabled}
            className={`w-full py-1 rounded font-mono font-bold text-xs uppercase tracking-wider transition-colors ${
              actionDisabled
                ? 'bg-neutral-800 text-neutral-500 cursor-not-allowed border border-neutral-700'
                : variantStyles[actionVariant]
            }`}
          >
            {actionLabel}
          </button>
        )}
      </div>
    );
  }

  // Face-up card display with asset image
  return (
    <div
      className={`relative ${sizeClasses[size]} rounded-xl overflow-hidden border border-emerald-500/40 bg-neutral-950 flex flex-col justify-between shadow-lg shadow-black/90 select-none group cursor-pointer transition-all duration-300 hover:border-emerald-400 hover:shadow-emerald-950/60 hover:scale-[1.03] ${className}`}
      onClick={onInspect}
    >
      {/* Background card image */}
      <img
        src={def.imagePath}
        alt={def.name}
        className="absolute inset-0 w-full h-full object-cover object-top transition-transform duration-500 group-hover:scale-105"
        onError={(e) => {
          // Fallback if image path has issue
          (e.target as HTMLElement).style.display = 'none';
        }}
      />

      {/* Dark gradient overlay at bottom for controls/badges */}
      <div className="absolute inset-0 bg-gradient-to-t from-black/95 via-black/20 to-black/60 pointer-events-none" />

      {/* Top Header Bar */}
      <div className="relative z-10 p-1.5 flex justify-between items-start">
        <div className="flex flex-col">
          <span className="font-bold text-xs leading-tight text-white drop-shadow-[0_2px_4px_rgba(0,0,0,0.9)] truncate max-w-[110px]">
            {def.name}
          </span>
          {def.subtitle && (
            <span className="text-[10px] text-emerald-400 font-mono drop-shadow font-semibold">
              {def.subtitle}
            </span>
          )}
        </div>

        {/* Inspect eye icon on hover */}
        <button
          onClick={(e) => {
            e.stopPropagation();
            if (onInspect) onInspect();
          }}
          className="opacity-0 group-hover:opacity-100 transition-opacity p-1 bg-black/80 hover:bg-emerald-600 rounded text-white"
          title="Inspect Card"
        >
          <Eye className="w-3.5 h-3.5" />
        </button>
      </div>

      {/* Badges / Stats Overlay */}
      <div className="relative z-10 p-1.5 flex flex-col space-y-1">
        {/* Resource Indicators */}
        <div className="flex justify-between items-center">
          {/* Attack stat */}
          {(def.attack !== undefined && def.attack > 0) && (
            <div className="flex items-center space-x-0.5 bg-red-950/90 border border-red-500/70 text-red-300 px-1.5 py-0.5 rounded text-[11px] font-bold font-mono">
              <Swords className="w-3 h-3 text-red-400" />
              <span>+{def.attack}</span>
            </div>
          )}

          {/* Recruit stat */}
          {(def.recruit !== undefined && def.recruit > 0) && (
            <div className="flex items-center space-x-0.5 bg-amber-950/90 border border-amber-500/70 text-amber-300 px-1.5 py-0.5 rounded text-[11px] font-bold font-mono">
              <Star className="w-3 h-3 text-amber-400" />
              <span>+{def.recruit}</span>
            </div>
          )}

          {/* Enemy Health / Defense */}
          {(def.enemyHealth !== undefined) && (
            <div className="flex items-center space-x-0.5 bg-red-900/90 border border-red-400 text-white px-2 py-0.5 rounded text-xs font-black font-mono shadow-sm">
              <Shield className="w-3 h-3 text-red-300" />
              <span>{def.enemyHealth}</span>
            </div>
          )}

          {/* Cost to recruit */}
          {(def.cost !== undefined && def.cost > 0) && (
            <div className="flex items-center space-x-0.5 bg-yellow-950/90 border border-yellow-500/80 text-yellow-300 px-1.5 py-0.5 rounded text-[11px] font-bold font-mono ml-auto">
              <Coins className="w-3 h-3 text-yellow-400" />
              <span>{def.cost}</span>
            </div>
          )}
        </div>

        {/* Action Button */}
        {actionLabel && (
          <button
            onClick={(e) => {
              e.stopPropagation();
              if (onAction && !actionDisabled) onAction();
            }}
            disabled={actionDisabled}
            className={`w-full py-1 rounded font-mono font-bold text-xs uppercase tracking-wider shadow transition-all ${
              actionDisabled
                ? 'bg-neutral-800/90 text-neutral-500 cursor-not-allowed border border-neutral-700'
                : variantStyles[actionVariant]
            }`}
          >
            {actionLabel}
          </button>
        )}
      </div>
    </div>
  );
};
