import { useRef, useEffect } from 'react';
import type { GameLogEntry } from '../../types/game';
import { Terminal, ShieldAlert, Swords, Star, Info, Trophy, Skull } from 'lucide-react';

interface CombatLogProps {
  logs: GameLogEntry[];
}

export const CombatLog = ({ logs }: CombatLogProps) => {
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [logs]);

  const getLogIcon = (type: GameLogEntry['type']) => {
    switch (type) {
      case 'combat':
        return <Swords className="w-3.5 h-3.5 text-red-400 shrink-0" />;
      case 'scan':
        return <ShieldAlert className="w-3.5 h-3.5 text-cyan-400 shrink-0" />;
      case 'recruit':
        return <Star className="w-3.5 h-3.5 text-amber-400 shrink-0" />;
      case 'victory':
        return <Trophy className="w-3.5 h-3.5 text-yellow-400 shrink-0" />;
      case 'defeat':
        return <Skull className="w-3.5 h-3.5 text-red-500 shrink-0" />;
      case 'warning':
        return <ShieldAlert className="w-3.5 h-3.5 text-orange-400 shrink-0" />;
      default:
        return <Info className="w-3.5 h-3.5 text-emerald-400 shrink-0" />;
    }
  };

  const getTextColor = (type: GameLogEntry['type']) => {
    switch (type) {
      case 'combat':
        return 'text-red-300';
      case 'scan':
        return 'text-cyan-300';
      case 'recruit':
        return 'text-amber-300';
      case 'victory':
        return 'text-yellow-200 font-bold';
      case 'defeat':
        return 'text-red-400 font-bold';
      case 'warning':
        return 'text-orange-300';
      default:
        return 'text-neutral-300';
    }
  };

  return (
    <div className="w-full bg-[#040806]/95 border border-emerald-900/60 rounded-xl p-3 shadow-lg flex flex-col h-48">
      {/* Header */}
      <div className="flex items-center space-x-2 border-b border-emerald-900/50 pb-2 mb-2">
        <Terminal className="w-4 h-4 text-emerald-400" />
        <span className="font-mono text-xs font-bold uppercase tracking-widest text-emerald-400">
          Tactical Operations Log
        </span>
      </div>

      {/* Log Feed */}
      <div ref={containerRef} className="flex-1 overflow-y-auto space-y-1.5 font-mono text-xs pr-2">
        {logs.map((log) => (
          <div
            key={log.id}
            className="flex items-start space-x-2 py-0.5 border-b border-neutral-900/60 leading-tight"
          >
            <span className="text-[10px] text-neutral-500 shrink-0">[{log.timestamp}]</span>
            {getLogIcon(log.type)}
            <span className={`${getTextColor(log.type)} break-words`}>{log.message}</span>
          </div>
        ))}
      </div>
    </div>
  );
};
