import { useState } from 'react';
import type { GameState, CardInstance, CardDefinition } from './types/game';
import { 
  setupInitialGame, 
  playCard, 
  scanSpace, 
  fightEnemy, 
  recruitHQCard, 
  recruitCommander, 
  endTurn, 
  resolveGoalAction 
} from './engine/gameEngine';
import { ROLE_PRESETS } from './data/cards';
import { WildernessTrack } from './components/Board/WildernessTrack';
import { CombatZone } from './components/Board/CombatZone';
import { HQBarracks } from './components/Board/HQBarracks';
import { ObjectivePanel } from './components/Board/ObjectivePanel';
import { PlayerArea } from './components/Player/PlayerArea';
import { CombatLog } from './components/UI/CombatLog';
import { CardInspectModal } from './components/UI/CardInspectModal';
import { GameOverModal } from './components/UI/GameOverModal';
import { 
  playScanSound, 
  playCombatSound, 
  playCardPlaySound, 
  playRecruitSound, 
  playThreatAlert 
} from './utils/audio';
import { 
  Volume2, 
  VolumeX, 
  RotateCcw, 
  BookOpen, 
  Crosshair, 
  Users, 
  User 
} from 'lucide-react';

export function App() {
  const [gameState, setGameState] = useState<GameState>(() => setupInitialGame(1, ['lieutenant']));
  const [inspectedCard, setInspectedCard] = useState<CardInstance | CardDefinition | null>(null);
  const [soundEnabled, setSoundEnabled] = useState(true);
  const [showManual, setShowManual] = useState(false);
  const [showSetupModal, setShowSetupModal] = useState(false);

  // Sound wrappers
  const triggerAudio = (fn: () => void) => {
    if (soundEnabled) fn();
  };

  // Actions
  const handlePlayCard = (instanceId: string) => {
    triggerAudio(playCardPlaySound);
    setGameState((prev) => playCard(prev, instanceId));
  };

  const handlePlayAllCards = () => {
    triggerAudio(playCardPlaySound);
    setGameState((prev) => {
      let state = { ...prev };
      const player = state.players[state.activePlayerIndex];
      const handCopy = [...player.hand];
      for (const c of handCopy) {
        state = playCard(state, c.instanceId);
      }
      return state;
    });
  };

  const handleScanSpace = (index: number) => {
    triggerAudio(playScanSound);
    setGameState((prev) => scanSpace(prev, index));
  };

  const handleFightEnemy = (instanceId: string, isCombatZone: boolean) => {
    triggerAudio(playCombatSound);
    setGameState((prev) => fightEnemy(prev, instanceId, isCombatZone));
  };

  const handleRecruitHQCard = (index: number) => {
    triggerAudio(playRecruitSound);
    setGameState((prev) => recruitHQCard(prev, index));
  };

  const handleRecruitCommander = () => {
    triggerAudio(playRecruitSound);
    setGameState((prev) => recruitCommander(prev,));
  };

  const handleResolveGoal = (goalCardId: string) => {
    triggerAudio(playCombatSound);
    setGameState((prev) => resolveGoalAction(prev, goalCardId));
  };

  const handleEndTurn = () => {
    const hasCombatZoneEnemies = gameState.combatZone.some((c) => c.definition.cardType === 'Enemy');
    if (hasCombatZoneEnemies) {
      triggerAudio(playThreatAlert);
    } else {
      triggerAudio(playCardPlaySound);
    }
    setGameState((prev) => endTurn(prev));
  };

  const handleStartNewGame = (playerCount: 1 | 2, roles: string[]) => {
    setGameState(setupInitialGame(playerCount, roles));
    setShowSetupModal(false);
  };

  const activePlayer = gameState.players[gameState.activePlayerIndex];

  return (
    <div className="min-h-screen bg-[#030704] text-neutral-100 flex flex-col font-sans relative selection:bg-emerald-500 selection:text-black">
      {/* Top Tactical Navigation Bar */}
      <header className="sticky top-0 z-40 bg-[#060c07]/95 border-b border-emerald-500/30 px-4 py-2.5 backdrop-blur-md flex flex-wrap items-center justify-between gap-3 shadow-lg shadow-black/80">
        <div className="flex items-center space-x-3">
          <div className="w-8 h-8 rounded-lg bg-emerald-950 border border-emerald-500/60 flex items-center justify-center text-emerald-400">
            <Crosshair className="w-5 h-5 animate-spin" style={{ animationDuration: '10s' }} />
          </div>
          <div>
            <h1 className="text-base md:text-lg font-black font-mono tracking-wider text-emerald-400 uppercase leading-tight">
              Legendary Encounters: Predator (1987)
            </h1>
            <span className="text-[11px] font-mono text-neutral-400 block">
              Co-Op Humans • Scenario 1: The Val Verdean Jungle
            </span>
          </div>
        </div>

        {/* Tactical Controls & Info */}
        <div className="flex items-center space-x-2 md:space-x-3">
          {/* Turn Indicator */}
          <div className="px-3 py-1 rounded-lg bg-emerald-950/80 border border-emerald-800 text-xs font-mono">
            <span className="text-emerald-500">Turn:</span>{' '}
            <strong className="text-white font-bold">{gameState.turnCount}</strong>
          </div>

          {/* Active Player Tag */}
          <div className="px-3 py-1 rounded-lg bg-emerald-950/80 border border-emerald-800 text-xs font-mono hidden sm:block">
            <span className="text-emerald-500">Operative:</span>{' '}
            <strong className="text-emerald-300 font-bold">{activePlayer.name} ({activePlayer.roleName})</strong>
          </div>

          {/* New Game Button */}
          <button
            onClick={() => setShowSetupModal(true)}
            className="p-1.5 md:px-3 md:py-1 rounded-lg bg-emerald-950 hover:bg-emerald-800 border border-emerald-600/60 text-emerald-300 font-mono text-xs uppercase font-bold flex items-center gap-1.5 transition-colors"
            title="Configure New Mission"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            <span className="hidden md:inline">Mission Setup</span>
          </button>

          {/* Field Manual Rules Button */}
          <button
            onClick={() => setShowManual(true)}
            className="p-1.5 md:px-3 md:py-1 rounded-lg bg-neutral-900 hover:bg-neutral-800 border border-neutral-700 text-neutral-300 font-mono text-xs uppercase flex items-center gap-1.5 transition-colors"
            title="Field Manual"
          >
            <BookOpen className="w-3.5 h-3.5 text-amber-400" />
            <span className="hidden md:inline">Field Manual</span>
          </button>

          {/* Sound Toggle */}
          <button
            onClick={() => setSoundEnabled(!soundEnabled)}
            className="p-2 rounded-lg bg-neutral-900 hover:bg-neutral-800 border border-neutral-700 text-neutral-300 transition-colors"
            title={soundEnabled ? 'Mute Audio' : 'Enable Audio'}
          >
            {soundEnabled ? <Volume2 className="w-4 h-4 text-emerald-400" /> : <VolumeX className="w-4 h-4 text-neutral-500" />}
          </button>
        </div>
      </header>

      {/* Main Tactical Battlefield */}
      <main className="flex-1 max-w-[1700px] w-full mx-auto p-3 md:p-5 flex flex-col space-y-4">
        {/* Row 1: Objective Dossier */}
        <ObjectivePanel
          currentObjectiveIndex={gameState.currentObjectiveIndex}
          objectivesCompleted={gameState.objectivesCompleted}
          operationsZone={gameState.operationsZone}
          onResolveGoal={handleResolveGoal}
          onInspectCard={setInspectedCard}
        />

        {/* Row 2: Wilderness Track (Underground -> Hills) */}
        <WildernessTrack
          spaces={gameState.wildernessSpaces}
          attackPoints={gameState.attackPoints}
          onScanSpace={handleScanSpace}
          onFightEnemy={handleFightEnemy}
          onInspectCard={setInspectedCard}
          onWipeGuerrillaCamp={() => handleResolveGoal('act1_guerrilla_camp')}
          currentObjectiveIndex={gameState.currentObjectiveIndex}
        />

        {/* Row 3: Combat Zone (Direct Contact hostiles) */}
        <CombatZone
          combatZone={gameState.combatZone}
          attackPoints={gameState.attackPoints}
          onFightEnemy={handleFightEnemy}
          onEvacuateChoppa={() => handleResolveGoal('act3_the_choppa')}
          onInspectCard={setInspectedCard}
        />

        {/* Row 4: HQ & Barracks (Available Recruits) */}
        <HQBarracks
          hq={gameState.hq}
          recruitPoints={gameState.recruitPoints}
          barracksDeckCount={gameState.barracksDeck.length}
          commanderDeckCount={gameState.commanderDeck.length}
          onRecruitHQCard={handleRecruitHQCard}
          onRecruitCommander={handleRecruitCommander}
          onInspectCard={setInspectedCard}
        />

        {/* Row 5: Active Player Terminal & Hand Tray */}
        <PlayerArea
          player={activePlayer}
          recruitPoints={gameState.recruitPoints}
          attackPoints={gameState.attackPoints}
          onPlayCard={handlePlayCard}
          onPlayAllCards={handlePlayAllCards}
          onEndTurn={handleEndTurn}
          onInspectCard={setInspectedCard}
          turnCount={gameState.turnCount}
        />

        {/* Row 6: Tactical Operations Combat Log */}
        <CombatLog logs={gameState.logs} />
      </main>

      {/* Card Inspection Modal */}
      <CardInspectModal
        card={inspectedCard}
        onClose={() => setInspectedCard(null)}
      />

      {/* Game Over Modal */}
      <GameOverModal
        isVictory={gameState.isVictory}
        victoryType={gameState.victoryType}
        isDefeat={gameState.isDefeat}
        defeatReason={gameState.defeatReason}
        turnCount={gameState.turnCount}
        objectivesCleared={gameState.objectivesCompleted.filter(Boolean).length}
        onRestart={() => setGameState(setupInitialGame(1, ['lieutenant']))}
      />

      {/* Field Manual Modal */}
      {showManual && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-in fade-in"
          onClick={() => setShowManual(false)}
        >
          <div
            className="max-w-2xl w-full bg-[#08120c] border border-emerald-500/60 rounded-2xl p-6 shadow-2xl text-neutral-200 overflow-y-auto max-h-[85vh] font-sans space-y-4"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="flex justify-between items-center border-b border-emerald-800 pb-3">
              <h2 className="text-xl font-bold font-mono text-emerald-400 uppercase tracking-wide flex items-center gap-2">
                <BookOpen className="w-5 h-5" /> Predator Field Manual (Official Rules)
              </h2>
              <button
                onClick={() => setShowManual(false)}
                className="text-neutral-400 hover:text-white font-mono text-sm"
              >
                ✕ Close
              </button>
            </div>

            <div className="space-y-3 text-xs leading-relaxed">
              <section className="bg-black/50 p-3 rounded-lg border border-neutral-800">
                <h4 className="font-bold text-emerald-300 font-mono text-sm mb-1">Turn Sequence</h4>
                <ol className="list-decimal pl-4 space-y-1 text-neutral-300">
                  <li><strong>Wilderness Phase:</strong> Top Enemy deck card enters Underground face down, pushing existing cards forward toward the Hills.</li>
                  <li><strong>Action Phase:</strong> Play cards from hand to generate Recruit & Attack. Spend Recruit to buy characters from HQ. Spend Attack to scan hidden cards or fight enemies.</li>
                  <li><strong>Strike Phase:</strong> Each enemy in Combat Zone strikes you! Draw from Enemy Strike deck.</li>
                  <li><strong>Cleanup Phase:</strong> Discard cards played & hand. Draw 6 new cards.</li>
                </ol>
              </section>

              <section className="bg-black/50 p-3 rounded-lg border border-neutral-800">
                <h4 className="font-bold text-amber-300 font-mono text-sm mb-1">The 3 Objectives (Scenario 1)</h4>
                <ul className="list-disc pl-4 space-y-1 text-neutral-300">
                  <li><strong>Objective 1 (Expendable Assets):</strong> Locate Guerrilla Camp in the Wilds. Clear the space and pay 1 Attack to wipe it out.</li>
                  <li><strong>Objective 2 (Flares, Frags, Claymores):</strong> Locate Defensive Position. In Operations, while Hills are clear, team pays 6 printed Recruit to attach to Hills.</li>
                  <li><strong>Objective 3 (Get to the Choppa!):</strong> Either defeat the final Predator (One Ugly Mother — 10 Attack) for a Major Victory, or evacuate all surviving operatives to The Choppa for a Minor Victory!</li>
                </ul>
              </section>

              <section className="bg-black/50 p-3 rounded-lg border border-neutral-800">
                <h4 className="font-bold text-red-300 font-mono text-sm mb-1">Keywords</h4>
                <ul className="list-disc pl-4 space-y-1 text-neutral-300">
                  <li><strong>Coordinate:</strong> Allows a teammate to play a virtual copy of your card on their turn while you draw a replacement card.</li>
                  <li><strong>Brothers in Arms:</strong> Bonus attack for each distinct teammate character played this turn.</li>
                  <li><strong>Marks:</strong> Gained from Hazards. The Predator tracks marked players with deadly focus.</li>
                </ul>
              </section>
            </div>
          </div>
        </div>
      )}

      {/* Mission Setup Modal */}
      {showSetupModal && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-in fade-in"
          onClick={() => setShowSetupModal(false)}
        >
          <div
            className="max-w-md w-full bg-[#08120c] border-2 border-emerald-500 rounded-2xl p-6 shadow-2xl text-neutral-200 font-sans space-y-5"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="border-b border-emerald-800 pb-3">
              <h2 className="text-xl font-bold font-mono text-emerald-400 uppercase tracking-wide">
                Mission Configuration
              </h2>
              <p className="text-xs text-neutral-400 mt-1 font-mono">
                Select operative loadout and fireteam composition:
              </p>
            </div>

            <div className="space-y-4">
              <div>
                <label className="text-xs font-mono uppercase text-emerald-500 block mb-2">
                  Player Setup:
                </label>
                <div className="grid grid-cols-2 gap-3">
                  <button
                    onClick={() => handleStartNewGame(1, ['lieutenant'])}
                    className="p-3 rounded-xl border border-emerald-500 bg-emerald-950/40 text-left hover:border-emerald-400 hover:scale-[1.02] transition-all"
                  >
                    <div className="flex items-center space-x-2 text-emerald-400 font-bold text-sm font-mono">
                      <User className="w-4 h-4" />
                      <span>Solo Operative</span>
                    </div>
                    <span className="text-[10px] text-neutral-400 mt-1 block font-mono">
                      1 Fireteam Hand (Standard Solo)
                    </span>
                  </button>

                  <button
                    onClick={() => handleStartNewGame(2, ['lieutenant', 'tracker'])}
                    className="p-3 rounded-xl border border-amber-500/60 bg-amber-950/20 text-left hover:border-amber-400 hover:scale-[1.02] transition-all"
                  >
                    <div className="flex items-center space-x-2 text-amber-300 font-bold text-sm font-mono">
                      <Users className="w-4 h-4" />
                      <span>2-Player Co-Op</span>
                    </div>
                    <span className="text-[10px] text-neutral-400 mt-1 block font-mono">
                      Pass & Play (Lieutenant + Tracker)
                    </span>
                  </button>
                </div>
              </div>

              <div>
                <label className="text-xs font-mono uppercase text-emerald-500 block mb-2">
                  Role Presets:
                </label>
                <div className="grid grid-cols-2 gap-2 text-xs font-mono">
                  {ROLE_PRESETS.map((r) => (
                    <div
                      key={r.roleId}
                      className="p-2 rounded-lg bg-neutral-900 border border-neutral-700 flex items-center space-x-2"
                    >
                      <img src={r.avatarImagePath} alt={r.roleName} className="w-7 h-9 rounded object-cover" />
                      <div>
                        <span className="font-bold text-white block">{r.roleName}</span>
                        <span className="text-[10px] text-emerald-400">Health: {r.health}</span>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </div>

            <div className="flex justify-end pt-2">
              <button
                onClick={() => setShowSetupModal(false)}
                className="px-4 py-2 rounded-lg bg-neutral-800 text-neutral-400 hover:text-white font-mono text-xs uppercase"
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default App;
