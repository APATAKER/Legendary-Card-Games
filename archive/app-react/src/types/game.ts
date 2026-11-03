export type CardType = 
  | 'Hero' 
  | 'Enemy' 
  | 'Prey' 
  | 'Strike' 
  | 'Avatar' 
  | 'Role' 
  | 'Hazard' 
  | 'Event' 
  | 'Objective' 
  | 'Goal' 
  | 'Commander';

export type RoleClass = 'Intel' | 'Leadership' | 'Strength' | 'Survival' | 'Tech';

export type Team = "Dutch's Team" | "Harrigan's Squad" | 'Jungle Hunter' | 'City Hunter' | 'Human' | 'Predator';

export type WildernessSpaceName = 'Underground' | 'Graveyard' | 'River' | 'Ruins' | 'Hills';

export interface CardDefinition {
  id: string;
  name: string;
  subtitle?: string;
  imagePath: string; // relative to public/ e.g. "/assets/raw/..."
  cardType: CardType;
  group: string;
  cost?: number;         // Recruit cost to buy
  recruit?: number;      // Recruit points generated
  attack?: number;       // Attack points generated
  enemyHealth?: number;  // Health / Attack required to kill
  strikeDamage?: number; // Damage if this is a Strike card
  roleClass?: RoleClass;
  team?: Team;
  keywords?: string[];   // e.g. ['Coordinate', 'Runner', 'Double Strike']
  rulesText?: string;
  flavorText?: string;
  // Dynamic calculation hook if needed
  dynamicAttack?: boolean;
}

export interface CardInstance {
  instanceId: string;
  definition: CardDefinition;
  isFaceDown?: boolean;
  isVirtualCopy?: boolean; // Rulebook p.16: Virtual copies (e.g. Coordinated cards) cannot be killed
  coordinatedFromPlayerId?: string; // Tracks lending player so physical card returns to their discard at end of turn
  attachedToInstanceId?: string; // e.g. Three Hundred City Blocks attached to Final Enemy
}

export interface WildernessSpace {
  name: WildernessSpaceName;
  scanCost: number;
  card: CardInstance | null;
  attachedGoal?: CardInstance | null; // e.g. Guerrilla Camp or Defensive Position attached to space
}

export interface Player {
  id: string;
  name: string;
  roleName: string;
  avatarImagePath: string;
  maxHealth: number;
  health: number; // calculated as maxHealth - total strike damage
  marks: number;
  hand: CardInstance[];
  deck: CardInstance[];
  discard: CardInstance[];
  inPlay: CardInstance[];
  strikes: CardInstance[];
  isDead: boolean;
  evacuatedToChoppa: boolean;
  coordinatedThisTurn?: boolean;
  cannotDieUntilTurnEnd?: boolean;
  cannotDieUntilEndOfNextTurn?: boolean; // Rulebook LEP-279: "You can't die until the end of your next turn"
}

export type TurnPhase = 
  | 'WILDERNESS' 
  | 'ACTION' 
  | 'STRIKE' 
  | 'CLEANUP';

export interface GameLogEntry {
  id: string;
  timestamp: string;
  message: string;
  type: 'info' | 'combat' | 'scan' | 'recruit' | 'warning' | 'victory' | 'defeat';
}

export interface GameState {
  gameMode: 'HumansScenario1';
  scenarioName: string;
  currentObjectiveIndex: number; // 0 = Obj 1, 1 = Obj 2, 2 = Obj 3
  objectivesCompleted: boolean[];
  activePlayerIndex: number;
  phase: TurnPhase;
  turnCount: number;
  
  // Resources for active player during Action Phase
  recruitPoints: number;
  attackPoints: number;
  
  // Board zones
  wildernessSpaces: WildernessSpace[];
  combatZone: CardInstance[];
  hq: (CardInstance | null)[]; // 5 slots
  barracksDeck: CardInstance[];
  commanderDeck: CardInstance[];
  enemyDeck: CardInstance[];
  enemyStrikeDeck: CardInstance[];
  discardedStrikes: CardInstance[];
  discardedEnemies: CardInstance[];
  discardedHazardsEvents: CardInstance[];
  
  // Operations / Goals in play
  operationsZone: CardInstance[];
  
  // Players
  players: Player[];
  
  // Active synergies & tracking
  classesPlayedThisTurn: Set<RoleClass>;
  barracksCharactersPlayedThisTurn: Set<string>; // 'Dutch', 'Blain', 'Mac', 'Dillon'
  coordinatedCountThisTurn?: number; // Counts cards coordinated to active player for Call for Backup
  enemyDeckReshuffledCount?: number; // Rulebook p.15: Reshuffle dead enemies once; 2nd depletion = defeat
  hazardCounter: number; // 1, 2, 3
  
  // Game outcomes
  isVictory: boolean;
  victoryType?: 'Major' | 'Minor'; // Major = Predator defeated, Minor = Evacuated
  isDefeat: boolean;
  defeatReason?: string;
  
  // Logs
  logs: GameLogEntry[];
  
  // UI interaction state
  inspectedCard: CardInstance | CardDefinition | null;
}
