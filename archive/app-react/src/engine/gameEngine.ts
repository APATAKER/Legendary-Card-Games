import type { 
  GameState, 
  Player, 
  CardInstance, 
  WildernessSpace, 
  GameLogEntry, 
  CardDefinition,
  RoleClass 
} from '../types/game';
import { 
  STARTER_CARDS, 
  ROLE_PRESETS, 
  HERO_BARRACKS_CARDS, 
  COMMANDER_CARDS, 
  ACT_1_CARDS, 
  ACT_2_CARDS, 
  ACT_3_CARDS, 
  YOUNG_BLOOD_CARD, 
  ENEMY_STRIKE_CARDS 
} from '../data/cards';

// Helper: generate unique instance ID
let instanceCounter = 1;
export function createInstance(def: CardDefinition, isFaceDown = false): CardInstance {
  return {
    instanceId: `inst_${def.id}_${instanceCounter++}_${Math.random().toString(36).substring(2, 6)}`,
    definition: def,
    isFaceDown,
  };
}

// Helper: Fisher-Yates shuffle
export function shuffle<T>(array: T[]): T[] {
  const arr = [...array];
  for (let i = arr.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}

// Log helper
function addLog(logs: GameLogEntry[], message: string, type: GameLogEntry['type'] = 'info'): GameLogEntry[] {
  const time = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  return [
    {
      id: `log_${Date.now()}_${Math.random().toString(36).substring(2, 6)}`,
      timestamp: time,
      message,
      type,
    },
    ...logs.slice(0, 99), // keep last 100 logs
  ];
}

// INITIALIZE GAME
export function setupInitialGame(playerCount: 1 | 2 = 1, selectedRoleIds: string[] = ['lieutenant']): GameState {
  instanceCounter = 1;

  // 1. Build Barracks Deck (56 cards: Dutch, Blain, Mac, Dillon)
  const barracksPool: CardInstance[] = [];
  HERO_BARRACKS_CARDS.forEach((card) => {
    let count = 5; // Default for commons
    if (card.id.includes('uncommon')) count = 3;
    if (card.id.includes('rare')) count = 1;
    for (let i = 0; i < count; i++) {
      barracksPool.push(createInstance(card, false));
    }
  });
  const shuffledBarracks = shuffle(barracksPool);

  // 2. Initial HQ (5 cards)
  const hq: (CardInstance | null)[] = [];
  for (let i = 0; i < 5; i++) {
    hq.push(shuffledBarracks.pop() || null);
  }

  // 3. Commander Deck (10 cards: 2 of each 5 classes)
  const commanderPool: CardInstance[] = [];
  COMMANDER_CARDS.forEach((c) => {
    commanderPool.push(createInstance(c));
    commanderPool.push(createInstance(c));
  });
  const shuffledCommanders = shuffle(commanderPool);

  // 4. Enemy Strike Deck (40 cards)
  const strikePool: CardInstance[] = [];
  ENEMY_STRIKE_CARDS.forEach(({ def, count }) => {
    for (let i = 0; i < count; i++) {
      strikePool.push(createInstance(def));
    }
  });
  const shuffledStrikes = shuffle(strikePool);

  // 5. Enemy Deck (Act 3 bottom, Act 2 middle, Act 1 top + Young Blood Chart per Rulebook p.6)
  // 1P: 0/0/0, 2P: 0/1/2, 3P: 2/3/4, 4-5P: 4/5/6
  const ybChart: Record<number, [number, number, number]> = {
    1: [0, 0, 0],
    2: [0, 1, 2],
    3: [2, 3, 4],
    4: [4, 5, 6],
    5: [4, 5, 6],
  };
  const ybCounts = ybChart[playerCount] || [0, 0, 0];

  // Act 3
  const act3Pool: CardInstance[] = [];
  ACT_3_CARDS.forEach((c) => {
    let count = 1;
    if (c.id === 'act3_not_a_single_track') count = 2;
    if (c.id === 'act3_now_it_wants_us') count = 2;
    if (c.id === 'act3_event') count = 2;
    for (let i = 0; i < count; i++) act3Pool.push(createInstance(c, true));
  });
  for (let i = 0; i < ybCounts[2]; i++) act3Pool.push(createInstance(YOUNG_BLOOD_CARD, true));
  const act3Shuffled = shuffle(act3Pool);

  // Act 2
  const act2Pool: CardInstance[] = [];
  ACT_2_CARDS.forEach((c) => {
    let count = 1;
    if (c.id === 'act2_like_a_hunter') count = 2;
    if (c.id === 'act2_it_changed_colors') count = 3;
    if (c.id === 'act2_event') count = 2;
    for (let i = 0; i < count; i++) act2Pool.push(createInstance(c, true));
  });
  for (let i = 0; i < ybCounts[1]; i++) act2Pool.push(createInstance(YOUNG_BLOOD_CARD, true));
  const act2Shuffled = shuffle(act2Pool);

  // Act 1
  const act1Pool: CardInstance[] = [];
  ACT_1_CARDS.forEach((c) => {
    let count = 1;
    if (c.id === 'act1_val_verde_guerrilla') count = 4;
    if (c.id === 'act1_event') count = 2;
    for (let i = 0; i < count; i++) act1Pool.push(createInstance(c, true));
  });
  for (let i = 0; i < ybCounts[0]; i++) act1Pool.push(createInstance(YOUNG_BLOOD_CARD, true));
  const act1Shuffled = shuffle(act1Pool);

  // Final Enemy deck: Act 1 on top of Act 2 on top of Act 3
  const fullEnemyDeck = [...act1Shuffled, ...act2Shuffled, ...act3Shuffled];

  // 6. Wilderness Spaces
  const wildernessSpaces: WildernessSpace[] = [
    { name: 'Underground', scanCost: 3, card: null },
    { name: 'Graveyard', scanCost: 3, card: null },
    { name: 'River', scanCost: 2, card: null },
    { name: 'Ruins', scanCost: 2, card: null },
    { name: 'Hills', scanCost: 1, card: null },
  ];

  // 7. Players
  const players: Player[] = [];
  for (let p = 0; p < playerCount; p++) {
    const roleId = selectedRoleIds[p] || (p === 0 ? 'lieutenant' : 'tracker');
    const rolePreset = ROLE_PRESETS.find((r) => r.roleId === roleId) || ROLE_PRESETS[0];

    const playerDeckPool: CardInstance[] = [];
    // 7 Experience
    for (let i = 0; i < 7; i++) playerDeckPool.push(createInstance(STARTER_CARDS[0]));
    // 5 Brute Strength
    for (let i = 0; i < 5; i++) playerDeckPool.push(createInstance(STARTER_CARDS[1]));
    // 1 Role card
    playerDeckPool.push(createInstance(rolePreset.roleCardDefinition));

    const shuffledPlayerDeck = shuffle(playerDeckPool);
    const hand: CardInstance[] = [];
    for (let i = 0; i < 6; i++) {
      const top = shuffledPlayerDeck.pop();
      if (top) hand.push(top);
    }

    players.push({
      id: `player_${p + 1}`,
      name: playerCount === 1 ? 'Solo Operative' : `Fireteam Alpha - Player ${p + 1}`,
      roleName: rolePreset.roleName,
      avatarImagePath: rolePreset.avatarImagePath,
      maxHealth: rolePreset.health,
      health: rolePreset.health,
      marks: 0,
      hand,
      deck: shuffledPlayerDeck,
      discard: [],
      inPlay: [],
      strikes: [],
      isDead: false,
      evacuatedToChoppa: false,
    });
  }

  let state: GameState = {
    gameMode: 'HumansScenario1',
    scenarioName: 'Predator (1987) — The Val Verdean Jungle',
    currentObjectiveIndex: 0,
    objectivesCompleted: [false, false, false],
    activePlayerIndex: 0,
    phase: 'ACTION', // We advance Wilderness on turn 1 automatically
    turnCount: 1,
    recruitPoints: 0,
    attackPoints: 0,
    wildernessSpaces,
    combatZone: [],
    hq,
    barracksDeck: shuffledBarracks,
    commanderDeck: shuffledCommanders,
    enemyDeck: fullEnemyDeck,
    enemyStrikeDeck: shuffledStrikes,
    discardedStrikes: [],
    discardedEnemies: [],
    discardedHazardsEvents: [],
    operationsZone: [],
    players,
    classesPlayedThisTurn: new Set<RoleClass>(),
    barracksCharactersPlayedThisTurn: new Set<string>(),
    hazardCounter: 1,
    isVictory: false,
    isDefeat: false,
    logs: [],
    inspectedCard: null,
  };

  state.logs = addLog(
    state.logs,
    `Mission started: Scenario 1 — Predator (1987). Objective: Expendable Assets!`,
    'info'
  );

  // Automatically advance Wilderness for Turn 1
  return advanceWilderness(state);
}

// ADVANCE WILDERNESS (ENEMY PHASE)
export function advanceWilderness(state: GameState): GameState {
  if (state.isVictory || state.isDefeat) return state;

  let newState = { ...state };
  let newLogs = [...newState.logs];
  let newEnemyDeck = [...newState.enemyDeck];
  let newSpaces = newState.wildernessSpaces.map((s) => ({ ...s }));
  let newCombatZone = [...newState.combatZone];
  let newDiscardedHazardsEvents = [...newState.discardedHazardsEvents];
  let newPlayers = newState.players.map((p) => ({ ...p, strikes: [...p.strikes] }));
  const activePlayer = newPlayers[newState.activePlayerIndex];

  if (newEnemyDeck.length === 0) {
    // Rulebook p.15: Shuffle dead Enemies once to form a new Enemy deck; if it runs out a 2nd time, defeat!
    const reshuffleCount = newState.enemyDeckReshuffledCount || 0;
    if (reshuffleCount === 0 && newState.discardedEnemies.length > 0) {
      newEnemyDeck = shuffle(
        newState.discardedEnemies.map((c) => ({ ...c, isFaceDown: true }))
      );
      newState.discardedEnemies = [];
      newState.enemyDeckReshuffledCount = 1;
      newLogs = addLog(
        newLogs,
        `[Enemy Deck Depleted] Shuffled Dead Enemies to form a new Enemy Deck (Rulebook p.15)!`,
        'warning'
      );
    } else {
      newLogs = addLog(
        newLogs,
        `[Time Is Up!] The Enemy Deck has run out a second time. The Predator kills everyone!`,
        'defeat'
      );
      return {
        ...newState,
        isDefeat: true,
        defeatReason: 'Enemy Deck ran out a second time (Rulebook p.15).',
        logs: newLogs,
      };
    }
  }

  // Draw top card from Enemy deck (face down)
  const incomingCard = newEnemyDeck.shift()!;
  incomingCard.isFaceDown = true;

  newLogs = addLog(
    newLogs,
    `[Wilderness] A hidden presence moves into Underground!`,
    'warning'
  );

  // Push chain:
  // Space 4: Hills
  // Space 3: Ruins
  // Space 2: River
  // Space 1: Graveyard
  // Space 0: Underground
  let pushedCard: CardInstance | null = incomingCard;

  for (let i = 0; i < 5; i++) {
    const currentOccupant = newSpaces[i].card;
    newSpaces[i].card = pushedCard;
    pushedCard = currentOccupant;
    if (!pushedCard) break; // space was empty, chain stops!
  }

  // If a card was pushed past Hills (index 4), it enters Combat Zone!
  if (pushedCard) {
    pushedCard.isFaceDown = false; // Flips immediately
    newLogs = addLog(
      newLogs,
      `[Combat Alert] ${pushedCard.definition.name} broke through into the Combat Zone!`,
      'combat'
    );

    // Check if pushed card is Event / Hazard / Goal
    if (pushedCard.definition.cardType === 'Event') {
      newLogs = addLog(newLogs, `Event triggered in Combat Zone: ${pushedCard.definition.name}`, 'warning');
      newDiscardedHazardsEvents.push(pushedCard);
      // Trigger Event
      newState = triggerObjectiveEvent({
        ...newState,
        wildernessSpaces: newSpaces,
        combatZone: newCombatZone,
        enemyDeck: newEnemyDeck,
        logs: newLogs,
      });
      newSpaces = newState.wildernessSpaces;
      newCombatZone = newState.combatZone;
      newLogs = newState.logs;
    } else if (pushedCard.definition.cardType === 'Hazard') {
      newLogs = addLog(newLogs, `Hazard revealed in Combat Zone! Active player is Marked!`, 'warning');
      activePlayer.marks += 1;
      newDiscardedHazardsEvents.push(pushedCard);
    } else if (pushedCard.definition.id === 'act3_the_choppa') {
      newLogs = addLog(newLogs, `The Choppa has landed in the Combat Zone! Evacuation ready!`, 'victory');
      newCombatZone.push(pushedCard);
    } else {
      newCombatZone.push(pushedCard);
    }
  }

  // Rulebook p.14 Runner Check: At the end of the Enemy Phase, a face-up Runner in the Wilds
  // moves 1 space toward Hills, pushing any card in that space as normal.
  for (let i = 4; i >= 0; i--) {
    const c = newSpaces[i].card;
    if (c && !c.isFaceDown && c.definition.keywords?.includes('Runner')) {
      newSpaces[i].card = null;
      let runnerMoving: CardInstance | null = c;
      for (let j = i + 1; j < 5; j++) {
        const occ = newSpaces[j].card;
        newSpaces[j].card = runnerMoving;
        runnerMoving = occ;
        if (!runnerMoving) break;
      }
      if (runnerMoving) {
        runnerMoving.isFaceDown = false;
        newCombatZone.push(runnerMoving);
      }
      newLogs = addLog(newLogs, `[Runner] ${c.definition.name} advanced 1 space toward the Hills!`, 'warning');
    }
  }

  return {
    ...newState,
    wildernessSpaces: newSpaces,
    combatZone: newCombatZone,
    enemyDeck: newEnemyDeck,
    discardedHazardsEvents: newDiscardedHazardsEvents,
    players: newPlayers,
    logs: newLogs,
    phase: 'ACTION',
  };
}

// PLAY CARD FROM HAND
export function playCard(state: GameState, instanceId: string): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  const player = state.players[state.activePlayerIndex];
  const cardIndex = player.hand.findIndex((c) => c.instanceId === instanceId);
  if (cardIndex === -1) return state;

  const card = player.hand[cardIndex];
  const newHand = [...player.hand];
  newHand.splice(cardIndex, 1);

  const newInPlay = [...player.inPlay, card];
  let newRecruit = state.recruitPoints + (card.definition.recruit || 0);
  let newAttack = state.attackPoints + (card.definition.attack || 0);
  let newLogs = [...state.logs];
  let newClasses = new Set(state.classesPlayedThisTurn);
  let newCharacters = new Set(state.barracksCharactersPlayedThisTurn);

  if (card.definition.roleClass) {
    newClasses.add(card.definition.roleClass);
  }
  // Rulebook p.13: Only Barracks characters (not Role cards, Commanders, or Starters) count toward Brothers in Arms
  if (card.definition.cardType === 'Hero' && (card.definition.cost || 0) > 0 && card.definition.subtitle) {
    newCharacters.add(card.definition.subtitle);
  }

  newLogs = addLog(
    newLogs,
    `[Play] ${player.name} played "${card.definition.name}" (+${card.definition.recruit || 0} Recruit, +${card.definition.attack || 0} Attack).`,
    'info'
  );

  // Synergy logic
  // Brothers in Arms: Bad Idea (Dutch) / Dug In (Blain) / Before Anybody Knows (Dillon)
  const otherCharsCount = Array.from(newCharacters).filter((c) => c !== card.definition.subtitle).length;
  if (card.definition.id === 'dutch_uncommon_bad_idea') {
    // +2 Attack for each other Barracks Character played this turn
    const bonus = otherCharsCount * 2;
    newAttack += bonus;
    if (bonus > 0) {
      newLogs = addLog(newLogs, `[Brothers in Arms] Gained +${bonus} Attack for ${otherCharsCount} other Barracks character(s)!`, 'info');
    }
  }

  if (card.definition.id === 'dillon_uncommon_before_anybody_knows') {
    // +2 Recruit for each other Barracks Character played this turn
    const bonus = otherCharsCount * 2;
    newRecruit += bonus;
    if (bonus > 0) {
      newLogs = addLog(newLogs, `[Brothers in Arms] Gained +${bonus} Recruit for ${otherCharsCount} other Barracks character(s)!`, 'recruit');
    }
  }

  // Knock Knock (Dutch): -1 Attack to all enemies in Combat Zone
  if (card.definition.id === 'dutch_common_knock_knock') {
    newLogs = addLog(newLogs, `[Tactical] Enemies in Combat Zone weakened by 1 this turn.`, 'info');
  }

  // If It Bleeds, We Can Kill It (Dutch Rare LEP-279): You can't die until the end of your next turn
  if (card.definition.id === 'dutch_rare_if_it_bleeds') {
    player.cannotDieUntilTurnEnd = true;
    player.cannotDieUntilEndOfNextTurn = true;
    newLogs = addLog(
      newLogs,
      `[If It Bleeds, We Can Kill It] ${player.name} cannot die until the end of their next turn!`,
      'victory'
    );
  }

  // Ain't Got Time to Bleed (Blain): Flip top 2 strikes into discard and gain that much Attack
  let newStrikes = [...state.enemyStrikeDeck];
  let newDiscardedStrikes = [...state.discardedStrikes];
  if (card.definition.id === 'blain_rare_time_to_bleed') {
    let flippedDamage = 0;
    for (let k = 0; k < 2; k++) {
      const topStrike = newStrikes.shift();
      if (topStrike) {
        flippedDamage += (topStrike.definition.strikeDamage || 0);
        newDiscardedStrikes.push(topStrike);
      }
    }
    newAttack += flippedDamage;
    newLogs = addLog(newLogs, `[Ain't Got Time To Bleed] Flipped 2 strikes! Gained +${flippedDamage} Attack!`, 'combat');
  }

  // Sexual Tyrannosaurus (Blain): Heal Strike with least damage if Strength class played earlier
  if (card.definition.id === 'blain_common_sexual_tyrannosaurus' && state.classesPlayedThisTurn.has('Strength')) {
    if (player.strikes.length > 0) {
      const sortedStrikes = [...player.strikes].sort((a, b) => (a.definition.strikeDamage || 0) - (b.definition.strikeDamage || 0));
      const healed = sortedStrikes.shift()!;
      newDiscardedStrikes.push(healed);
      newLogs = addLog(newLogs, `[Heal] Healed Strike "${healed.definition.name}" (${healed.definition.strikeDamage || 0} dmg)!`, 'recruit');
      player.strikes = sortedStrikes;
      player.health = Math.min(player.maxHealth, player.maxHealth - player.strikes.reduce((sum, s) => sum + (s.definition.strikeDamage || 0), 0));
    }
  }

  const updatedPlayers = state.players.map((p, idx) => {
    if (idx === state.activePlayerIndex) {
      return {
        ...p,
        hand: newHand,
        inPlay: newInPlay,
        health: p.health,
      };
    }
    return p;
  });

  return {
    ...state,
    recruitPoints: newRecruit,
    attackPoints: newAttack,
    enemyStrikeDeck: newStrikes,
    discardedStrikes: newDiscardedStrikes,
    players: updatedPlayers,
    classesPlayedThisTurn: newClasses,
    barracksCharactersPlayedThisTurn: newCharacters,
    logs: newLogs,
  };
}

// SCAN WILDERNESS SPACE
export function scanSpace(state: GameState, spaceIndex: number): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  const space = state.wildernessSpaces[spaceIndex];
  if (!space || !space.card || !space.card.isFaceDown) return state;

  // Scan cost calculation (Not a Single Track adds +2 Attack)
  let scanCost = space.scanCost;
  const hasNotASingleTrack = state.combatZone.some((c) => c.definition.id === 'act3_not_a_single_track') ||
    state.wildernessSpaces.some((s) => s.card && !s.card.isFaceDown && s.card.definition.id === 'act3_not_a_single_track');
  if (hasNotASingleTrack) {
    scanCost += 2;
  }

  if (state.attackPoints < scanCost) {
    return {
      ...state,
      logs: addLog(state.logs, `Insufficient Attack! Scanning ${space.name} requires ${scanCost} Attack.`, 'warning'),
    };
  }

  let newState = { ...state, attackPoints: state.attackPoints - scanCost };
  let newLogs = addLog(newState.logs, `[Scan] ${space.name} scanned for ${scanCost} Attack!`, 'scan');
  let newSpaces = newState.wildernessSpaces.map((s, idx) => {
    if (idx === spaceIndex && s.card) {
      return {
        ...s,
        card: {
          ...s.card,
          isFaceDown: false,
        },
      };
    }
    return { ...s };
  });

  const revealedCard = newSpaces[spaceIndex].card!;
  newLogs = addLog(newLogs, `Revealed: [${revealedCard.definition.name}] in ${space.name}!`, 'info');

  // Reveal triggers
  if (revealedCard.definition.cardType === 'Event') {
    newLogs = addLog(newLogs, `Event triggered: ${revealedCard.definition.name}`, 'warning');
    newState.discardedHazardsEvents.push(revealedCard);
    newSpaces[spaceIndex].card = null;
    newState = triggerObjectiveEvent({
      ...newState,
      wildernessSpaces: newSpaces,
      logs: newLogs,
    });
    return newState;
  }

  if (revealedCard.definition.cardType === 'Hazard') {
    const activePlayer = newState.players[newState.activePlayerIndex];
    activePlayer.marks += 1;
    newLogs = addLog(newLogs, `Hazard revealed! ${activePlayer.name} has been Marked by the Predator!`, 'warning');
    newState.discardedHazardsEvents.push(revealedCard);
    newSpaces[spaceIndex].card = null;
    return {
      ...newState,
      wildernessSpaces: newSpaces,
      logs: newLogs,
    };
  }

  if (revealedCard.definition.id === 'act1_guerrilla_camp') {
    newLogs = addLog(newLogs, `[Objective 1] Guerrilla Camp located! Clear this space and pay 1 Attack to wipe it out!`, 'victory');
  }

  if (revealedCard.definition.id === 'act2_defensive_position') {
    newLogs = addLog(newLogs, `[Objective 2] Defensive Position located! Moved to Operations. Discard 6 printed Recruit to secure Hills!`, 'info');
    newState.operationsZone = [...newState.operationsZone, revealedCard];
    newSpaces[spaceIndex].card = null;
  }

  if (revealedCard.definition.id === 'act3_the_choppa') {
    newLogs = addLog(newLogs, `[Objective 3] THE CHOPPA IS HERE! Moved to Combat Zone for evacuation!`, 'victory');
    newState.combatZone = [...newState.combatZone, revealedCard];
    newSpaces[spaceIndex].card = null;
  }

  return {
    ...newState,
    wildernessSpaces: newSpaces,
    logs: newLogs,
  };
}

// FIGHT ENEMY
export function fightEnemy(state: GameState, targetInstanceId: string, isCombatZone: boolean): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  let targetCard: CardInstance | null = null;
  let targetIndex = -1;
  let targetSpaceIndex = -1;

  if (isCombatZone) {
    targetIndex = state.combatZone.findIndex((c) => c.instanceId === targetInstanceId);
    if (targetIndex !== -1) targetCard = state.combatZone[targetIndex];
  } else {
    for (let i = 0; i < state.wildernessSpaces.length; i++) {
      if (state.wildernessSpaces[i].card?.instanceId === targetInstanceId) {
        targetCard = state.wildernessSpaces[i].card;
        targetSpaceIndex = i;
        break;
      }
    }
  }

  if (!targetCard || targetCard.isFaceDown) return state;

  // Rulebook p.12: Final Enemy can only be fought once Objective 2 is completed
  if (targetCard.definition.id === 'act3_one_ugly_mother' && !state.objectivesCompleted[1]) {
    return {
      ...state,
      logs: addLog(
        state.logs,
        `[Rulebook p.12] Cannot fight Final Enemy (${targetCard.definition.name}) until Objective 2 is completed!`,
        'warning'
      ),
    };
  }

  // Calculate required attack
  let requiredAttack = targetCard.definition.enemyHealth || 0;

  // Soviet Advisor ongoing: +1 Attack to Val Verde Guerrillas
  const hasSovietAdvisor = state.combatZone.some((c) => c.definition.id === 'act1_soviet_advisor') ||
    state.wildernessSpaces.some((s) => s.card && !s.card.isFaceDown && s.card.definition.id === 'act1_soviet_advisor');
  if (targetCard.definition.id === 'act1_val_verde_guerrilla' && hasSovietAdvisor) {
    requiredAttack += 1;
  }

  // It Changed Colors: -2 Attack for each different Class played this turn
  if (targetCard.definition.id === 'act2_it_changed_colors') {
    const classBonus = state.classesPlayedThisTurn.size * 2;
    requiredAttack = Math.max(1, (targetCard.definition.enemyHealth || 8) - classBonus);
  }

  // Knock Knock reduction in Combat Zone
  const hasKnockKnock = state.players[state.activePlayerIndex].inPlay.some((c) => c.definition.id === 'dutch_common_knock_knock');
  if (isCombatZone && hasKnockKnock) {
    requiredAttack = Math.max(1, requiredAttack - 1);
  }

  // Check resource
  if (state.attackPoints < requiredAttack) {
    return {
      ...state,
      logs: addLog(state.logs, `Cannot fight ${targetCard.definition.name}: requires ${requiredAttack} Attack, you have ${state.attackPoints}.`, 'warning'),
    };
  }

  let newState = {
    ...state,
    attackPoints: state.attackPoints - requiredAttack,
  };
  let newLogs = addLog(newState.logs, `[Combat] Defeated ${targetCard.definition.name} (${requiredAttack} Attack spent)!`, 'combat');
  let newCombatZone = [...newState.combatZone];
  let newSpaces = newState.wildernessSpaces.map((s) => ({ ...s }));
  let newDiscardedEnemies = [...newState.discardedEnemies, targetCard];

  if (isCombatZone) {
    newCombatZone.splice(targetIndex, 1);
  } else if (targetSpaceIndex !== -1) {
    newSpaces[targetSpaceIndex].card = null;
  }

  // Check Death Trigger: Val Verde Guerrilla moves Guerrilla Camp
  if (targetCard.definition.id === 'act1_val_verde_guerrilla') {
    const campSpaceIdx = newSpaces.findIndex((s) => s.card?.definition.id === 'act1_guerrilla_camp');
    if (campSpaceIdx !== -1 && campSpaceIdx > 0) {
      // Move one space left (towards Hills) if space is clear
      if (!newSpaces[campSpaceIdx - 1].card) {
        newSpaces[campSpaceIdx - 1].card = newSpaces[campSpaceIdx].card;
        newSpaces[campSpaceIdx].card = null;
        newLogs = addLog(newLogs, `[Death Ability] Guerrilla Camp was moved 1 space to the left!`, 'info');
      }
    }
  }

  // Check Final Enemy Death: One Ugly Mother
  if (targetCard.definition.id === 'act3_one_ugly_mother') {
    const activePlayer = newState.players[newState.activePlayerIndex];
    if (activePlayer.marks > 0) {
      activePlayer.marks -= 1;
      newLogs = addLog(newLogs, `[Hunter Honor] You destroyed 1 Mark instead of killing the Predator! He remains alive!`, 'warning');
      // Predator survives!
      if (isCombatZone) newCombatZone.push(targetCard);
      else if (targetSpaceIndex !== -1) newSpaces[targetSpaceIndex].card = targetCard;
      return {
        ...newState,
        wildernessSpaces: newSpaces,
        combatZone: newCombatZone,
        logs: newLogs,
      };
    } else {
      // MAJOR VICTORY!
      newLogs = addLog(newLogs, `[MAJOR VICTORY!] You killed One Ugly Mother! The Predator has fallen!`, 'victory');
      return {
        ...newState,
        isVictory: true,
        victoryType: 'Major',
        wildernessSpaces: newSpaces,
        combatZone: newCombatZone,
        logs: newLogs,
      };
    }
  }

  return {
    ...newState,
    wildernessSpaces: newSpaces,
    combatZone: newCombatZone,
    discardedEnemies: newDiscardedEnemies,
    logs: newLogs,
  };
}

// RESOLVE GOAL ACTION
export function resolveGoalAction(state: GameState, goalCardId: string): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  let newState = { ...state };
  let newLogs = [...newState.logs];

  // Objective 1: Wipe Out Guerrilla Camp
  if (goalCardId === 'act1_guerrilla_camp') {
    if (state.currentObjectiveIndex !== 0) return state;
    if (state.attackPoints < 1) {
      return {
        ...state,
        logs: addLog(state.logs, `Wiping out Guerrilla Camp requires 1 Attack.`, 'warning'),
      };
    }

    const campSpaceIdx = state.wildernessSpaces.findIndex((s) => s.card?.definition.id === 'act1_guerrilla_camp');
    if (campSpaceIdx === -1) return state;

    const newSpaces = state.wildernessSpaces.map((s, idx) => {
      if (idx === campSpaceIdx) return { ...s, card: null };
      return { ...s };
    });

    newLogs = addLog(newLogs, `[OBJECTIVE 1 COMPLETE] Guerrilla Camp has been wiped out! Moving to Objective 2: Flares, Frags, and Claymores!`, 'victory');
    return {
      ...newState,
      attackPoints: state.attackPoints - 1,
      wildernessSpaces: newSpaces,
      currentObjectiveIndex: 1,
      objectivesCompleted: [true, false, false],
      logs: newLogs,
    };
  }

  // Objective 2: Defensive Position in Operations
  if (goalCardId === 'act2_defensive_position') {
    if (state.currentObjectiveIndex !== 1) return state;
    const hillsSpace = state.wildernessSpaces[4]; // Hills is index 4
    if (hillsSpace.card !== null) {
      return {
        ...state,
        logs: addLog(state.logs, `Cannot secure Defensive Position while Hills space is occupied! Clear the Hills first.`, 'warning'),
      };
    }

    // Check if player has 6 printed recruit points in hand or in play
    const player = state.players[state.activePlayerIndex];
    const totalPrintedRecruit = [...player.hand, ...player.inPlay].reduce((sum, c) => sum + (c.definition.recruit || 0), 0);

    if (totalPrintedRecruit < 6) {
      return {
        ...state,
        logs: addLog(state.logs, `Need at least 6 printed Recruit points to set up Defensive Position (current available: ${totalPrintedRecruit}).`, 'warning'),
      };
    }

    newLogs = addLog(newLogs, `[OBJECTIVE 2 COMPLETE] Defensive Position secured on the Hills! Advancing to Objective 3: Get to the Choppa!`, 'victory');
    return {
      ...newState,
      currentObjectiveIndex: 2,
      objectivesCompleted: [true, true, false],
      operationsZone: [],
      logs: newLogs,
    };
  }

  // Objective 3: Get to the Choppa Evacuation
  if (goalCardId === 'act3_the_choppa') {
    if (state.currentObjectiveIndex !== 2) return state;
    if (state.attackPoints < 3) {
      return {
        ...state,
        logs: addLog(state.logs, `Evacuating to The Choppa requires 3 Attack.`, 'warning'),
      };
    }

    const player = state.players[state.activePlayerIndex];
    const enemyCountInCombatZone = state.combatZone.filter((c) => c.definition.cardType === 'Enemy').length;

    newLogs = addLog(newLogs, `[The Choppa] Running to the chopper under fire! Taking ${enemyCountInCombatZone} strikes!`, 'combat');

    // Draw strikes
    let strikesDrawn: CardInstance[] = [];
    let strikeDeck = [...state.enemyStrikeDeck];
    let totalDmg = 0;
    for (let k = 0; k < enemyCountInCombatZone; k++) {
      const strike = strikeDeck.shift();
      if (strike) {
        strikesDrawn.push(strike);
        totalDmg += (strike.definition.strikeDamage || 0);
      }
    }

    const updatedHealth = player.health - totalDmg;
    player.strikes = [...player.strikes, ...strikesDrawn];
    player.health = updatedHealth;

    if (updatedHealth <= 0) {
      player.isDead = true;
      newLogs = addLog(newLogs, `[KIA] ${player.name} was shot down trying to reach The Choppa!`, 'defeat');
    } else {
      player.evacuatedToChoppa = true;
      newLogs = addLog(newLogs, `[EVACUATED!] ${player.name} safely boarded The Choppa!`, 'victory');
    }

    // Check if all living players are in Choppa
    const livingPlayers = state.players.filter((p) => !p.isDead);
    const allEvacuated = livingPlayers.length > 0 && livingPlayers.every((p) => p.evacuatedToChoppa);

    if (allEvacuated) {
      newLogs = addLog(newLogs, `[MINOR VICTORY!] All surviving operatives evacuated safely in The Choppa! Mission accomplished!`, 'victory');
      return {
        ...newState,
        isVictory: true,
        victoryType: 'Minor',
        enemyStrikeDeck: strikeDeck,
        logs: newLogs,
      };
    }

    return {
      ...newState,
      attackPoints: state.attackPoints - 3,
      enemyStrikeDeck: strikeDeck,
      logs: newLogs,
    };
  }

  return state;
}

// RECRUIT CARD FROM HQ
export function recruitHQCard(state: GameState, hqIndex: number): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  const card = state.hq[hqIndex];
  if (!card) return state;

  const cost = card.definition.cost || 0;
  if (state.recruitPoints < cost) {
    return {
      ...state,
      logs: addLog(state.logs, `Insufficient Recruit points! "${card.definition.name}" costs ${cost}.`, 'warning'),
    };
  }

  let newBarracks = [...state.barracksDeck];
  let newHq = [...state.hq];
  const replacement = newBarracks.shift() || null;
  newHq[hqIndex] = replacement;

  const player = state.players[state.activePlayerIndex];
  const updatedPlayer = {
    ...player,
    discard: [...player.discard, card],
  };

  const updatedPlayers = state.players.map((p, idx) => idx === state.activePlayerIndex ? updatedPlayer : p);

  return {
    ...state,
    recruitPoints: state.recruitPoints - cost,
    hq: newHq,
    barracksDeck: newBarracks,
    players: updatedPlayers,
    logs: addLog(state.logs, `[Recruit] ${player.name} recruited "${card.definition.name}" for ${cost} Recruit.`, 'recruit'),
  };
}

// RECRUIT COMMANDER
export function recruitCommander(state: GameState): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;

  if (state.commanderDeck.length === 0) {
    return {
      ...state,
      logs: addLog(state.logs, `No Commanders remaining in command reserve.`, 'warning'),
    };
  }

  if (state.recruitPoints < 3) {
    return {
      ...state,
      logs: addLog(state.logs, `Recruiting a Commander requires 3 Recruit points.`, 'warning'),
    };
  }

  let newCommanders = [...state.commanderDeck];
  const commander = newCommanders.shift()!;

  const player = state.players[state.activePlayerIndex];
  const updatedPlayer = {
    ...player,
    discard: [...player.discard, commander],
  };

  const updatedPlayers = state.players.map((p, idx) => idx === state.activePlayerIndex ? updatedPlayer : p);

  return {
    ...state,
    recruitPoints: state.recruitPoints - 3,
    commanderDeck: newCommanders,
    players: updatedPlayers,
    logs: addLog(state.logs, `[Command] ${player.name} recruited Commander Phillips (${commander.definition.roleClass})!`, 'recruit'),
  };
}

// COORDINATE CARD (Rulebook p.12-13 & p.16: Lend a Coordinate card during another player's turn)
export function coordinateCard(state: GameState, lendingPlayerIndex: number, cardInstanceId: string): GameState {
  if (state.phase !== 'ACTION' || state.isVictory || state.isDefeat) return state;
  if (lendingPlayerIndex === state.activePlayerIndex) return state;

  const lender = state.players[lendingPlayerIndex];
  if (!lender || lender.isDead || lender.coordinatedThisTurn) return state;

  const cardIdx = lender.hand.findIndex((c) => c.instanceId === cardInstanceId);
  if (cardIdx === -1) return state;

  const card = lender.hand[cardIdx];
  if (!card.definition.keywords?.includes('Coordinate')) return state;

  // 1. Remove card from lender's hand and immediately draw 1 replacement card
  const newLenderHand = [...lender.hand];
  newLenderHand.splice(cardIdx, 1);
  let newLenderDeck = [...lender.deck];
  let newLenderDiscard = [...lender.discard];

  if (newLenderDeck.length === 0 && newLenderDiscard.length > 0) {
    newLenderDeck = shuffle(newLenderDiscard.splice(0, newLenderDiscard.length));
  }
  const replacement = newLenderDeck.pop();
  if (replacement) newLenderHand.push(replacement);

  // 2. Active player plays a virtual copy of the Coordinated card
  const virtualCard: CardInstance = {
    ...card,
    isVirtualCopy: true,
    coordinatedFromPlayerId: lender.id,
  };

  const activePlayer = state.players[state.activePlayerIndex];
  const updatedPlayers = state.players.map((p, idx) => {
    if (idx === lendingPlayerIndex) {
      return {
        ...p,
        hand: newLenderHand,
        deck: newLenderDeck,
        discard: newLenderDiscard,
        coordinatedThisTurn: true,
      };
    }
    if (idx === state.activePlayerIndex) {
      return {
        ...p,
        hand: [...p.hand, virtualCard],
      };
    }
    return p;
  });

  const stateWithLend: GameState = {
    ...state,
    players: updatedPlayers,
    coordinatedCountThisTurn: (state.coordinatedCountThisTurn || 0) + 1,
    logs: addLog(
      state.logs,
      `[Coordinate] ${lender.name} lent "${card.definition.name}" to ${activePlayer.name} and drew 1 replacement card!`,
      'recruit'
    ),
  };

  return playCard(stateWithLend, virtualCard.instanceId);
}

// END TURN (RESOLVE STRIKES & CLEANUP)
export function endTurn(state: GameState): GameState {
  if (state.isVictory || state.isDefeat) return state;

  let newState = { ...state };
  let newLogs = [...newState.logs];
  let strikeDeck = [...newState.enemyStrikeDeck];
  let activePlayer = { ...newState.players[newState.activePlayerIndex] };

  // STRIKE PHASE: Rulebook p.11 — From right to left, each enemy in the Combat Zone strikes you, one at a time.
  const combatZoneEnemies = [...newState.combatZone]
    .filter((c) => c.definition.cardType === 'Enemy')
    .reverse();
  let strikesReceived: CardInstance[] = [];
  let damageTakenThisTurn = 0;

  for (const enemy of combatZoneEnemies) {
    // Check Double Strike keyword
    const strikeCount = enemy.definition.keywords?.includes('Double Strike') ? 2 : 1;

    for (let s = 0; s < strikeCount; s++) {
      if (strikeDeck.length === 0) {
        // Reshuffle discarded strikes
        strikeDeck = shuffle([...newState.discardedStrikes]);
        newState.discardedStrikes = [];
      }

      const strike = strikeDeck.shift();
      if (strike) {
        strikesReceived.push(strike);
        const dmg = strike.definition.strikeDamage || 0;
        damageTakenThisTurn += dmg;
        newLogs = addLog(
          newLogs,
          `[Strike] ${enemy.definition.name} struck ${activePlayer.name}: ${strike.definition.name} (${dmg} damage)!`,
          'combat'
        );
      }
    }
  }

  // Apply damage
  activePlayer.strikes = [...activePlayer.strikes, ...strikesReceived];
  activePlayer.health = Math.max(0, activePlayer.maxHealth - activePlayer.strikes.reduce((sum, s) => sum + (s.definition.strikeDamage || 0), 0));

  const deathProtected = activePlayer.cannotDieUntilTurnEnd || activePlayer.cannotDieUntilEndOfNextTurn;
  if (activePlayer.health <= 0 && !deathProtected) {
    activePlayer.isDead = true;
    newLogs = addLog(newLogs, `[KIA] ${activePlayer.name} has succumbed to their injuries!`, 'defeat');
  }

  // CLEANUP PHASE
  // Return any Coordinated virtual cards to their lending player's discard pile (Rulebook p.12-13)
  const lenderReturns: Record<string, CardInstance[]> = {};
  const ownPlayedCards: CardInstance[] = [];
  for (const played of activePlayer.inPlay) {
    if (played.isVirtualCopy && played.coordinatedFromPlayerId) {
      if (!lenderReturns[played.coordinatedFromPlayerId]) {
        lenderReturns[played.coordinatedFromPlayerId] = [];
      }
      lenderReturns[played.coordinatedFromPlayerId].push({
        ...played,
        isVirtualCopy: false,
        coordinatedFromPlayerId: undefined,
      });
    } else if (!played.isVirtualCopy) {
      ownPlayedCards.push(played);
    }
  }

  // Discard played and remaining hand
  const newDiscard = [...activePlayer.discard, ...ownPlayedCards, ...activePlayer.hand.filter((c) => !c.isVirtualCopy)];
  let newPlayerDeck = [...activePlayer.deck];
  const newHand: CardInstance[] = [];

  // Draw 6 cards (Rulebook p.11: only shuffle discard when deck is completely empty AND you still need to draw)
  for (let i = 0; i < 6; i++) {
    if (newPlayerDeck.length === 0 && newDiscard.length > 0) {
      newPlayerDeck = shuffle(newDiscard.splice(0, newDiscard.length));
    }
    const drawn = newPlayerDeck.pop();
    if (drawn) newHand.push(drawn);
  }

  activePlayer.hand = newHand;
  activePlayer.deck = newPlayerDeck;
  activePlayer.discard = newDiscard;
  activePlayer.inPlay = [];

  // Advance death immunity window (Rulebook LEP-279: "You can't die until the end of your next turn.")
  if (activePlayer.cannotDieUntilEndOfNextTurn) {
    // Transitions from turn played -> next turn window
    activePlayer.cannotDieUntilTurnEnd = true;
    activePlayer.cannotDieUntilEndOfNextTurn = false;
  } else if (activePlayer.cannotDieUntilTurnEnd) {
    activePlayer.cannotDieUntilTurnEnd = false;
    // After your next turn, if you have fatal damage, you die
    if (activePlayer.health <= 0) {
      activePlayer.isDead = true;
      newLogs = addLog(newLogs, `[KIA] ${activePlayer.name} succumbed to fatal injuries after their survival window expired!`, 'defeat');
    }
  }

  const updatedPlayers = newState.players.map((p, idx) => {
    const returned = lenderReturns[p.id] || [];
    if (idx === newState.activePlayerIndex) {
      return {
        ...activePlayer,
        coordinatedThisTurn: false,
      };
    }
    return {
      ...p,
      discard: returned.length > 0 ? [...p.discard, ...returned] : p.discard,
      coordinatedThisTurn: false,
    };
  });

  // Check defeat condition (all players dead)
  const allDead = updatedPlayers.every((p) => p.isDead);
  if (allDead) {
    newLogs = addLog(newLogs, `All fireteam operatives KIA. The Jungle Hunter has wiped out the squad. DEFEAT.`, 'defeat');
    return {
      ...newState,
      players: updatedPlayers,
      enemyStrikeDeck: strikeDeck,
      isDefeat: true,
      defeatReason: 'All operatives killed by the Predator.',
      logs: newLogs,
    };
  }

  // Advance to next player
  let nextPlayerIdx = (newState.activePlayerIndex + 1) % newState.players.length;
  while (updatedPlayers[nextPlayerIdx].isDead) {
    nextPlayerIdx = (nextPlayerIdx + 1) % newState.players.length;
  }

  newLogs = addLog(newLogs, `--- Turn ${newState.turnCount + 1}: ${updatedPlayers[nextPlayerIdx].name}'s Turn ---`, 'info');

  const advancedState: GameState = {
    ...newState,
    turnCount: newState.turnCount + 1,
    activePlayerIndex: nextPlayerIdx,
    phase: 'WILDERNESS',
    recruitPoints: 0,
    attackPoints: 0,
    players: updatedPlayers,
    enemyStrikeDeck: strikeDeck,
    classesPlayedThisTurn: new Set<RoleClass>(),
    barracksCharactersPlayedThisTurn: new Set<string>(),
    logs: newLogs,
  };

  // Automatically trigger Wilderness Phase for the new turn
  return advanceWilderness(advancedState);
}

// TRIGGER OBJECTIVE EVENT HELPER
function triggerObjectiveEvent(state: GameState): GameState {
  let newLogs = [...state.logs];
  let newSpaces = state.wildernessSpaces.map((s) => ({ ...s }));

  if (state.currentObjectiveIndex === 0) {
    // Obj 1 Event: Move Guerrilla Camp 1 space right if in Wilds
    const campIdx = newSpaces.findIndex((s) => s.card?.definition.id === 'act1_guerrilla_camp');
    if (campIdx !== -1 && campIdx < 4) {
      if (!newSpaces[campIdx + 1].card) {
        newSpaces[campIdx + 1].card = newSpaces[campIdx].card;
        newSpaces[campIdx].card = null;
        newLogs = addLog(newLogs, `[Objective 1 Event] Guerrilla Camp moved 1 space to the right!`, 'warning');
      }
    }
  } else if (state.currentObjectiveIndex === 1) {
    // Obj 2 Event: Put top card of Enemy deck into Hills face down (pushes Hills card to Combat Zone)
    if (state.enemyDeck.length > 0) {
      const topEnemy = state.enemyDeck.shift()!;
      topEnemy.isFaceDown = true;
      const hillsExisting = newSpaces[4].card;
      newSpaces[4].card = topEnemy;
      if (hillsExisting) {
        hillsExisting.isFaceDown = false;
        state.combatZone.push(hillsExisting);
        newLogs = addLog(newLogs, `[Objective 2 Event] Pushed ${hillsExisting.definition.name} into Combat Zone!`, 'combat');
      }
    }
  } else if (state.currentObjectiveIndex === 2) {
    // Obj 3 Event: Draw Strike for each Mark you have
    const activePlayer = state.players[state.activePlayerIndex];
    if (activePlayer.marks > 0) {
      newLogs = addLog(newLogs, `[Objective 3 Event] El Cazador Trofeo: ${activePlayer.marks} strikes drawn for your ${activePlayer.marks} Marks!`, 'combat');
      for (let m = 0; m < activePlayer.marks; m++) {
        const strike = state.enemyStrikeDeck.shift();
        if (strike) {
          activePlayer.strikes.push(strike);
        }
      }
    }
  }

  return {
    ...state,
    wildernessSpaces: newSpaces,
    logs: newLogs,
  };
}
