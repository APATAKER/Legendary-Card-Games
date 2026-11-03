#pragma once
// The tabletop & rulebook flow model: physical cards lying in piles on the table plus
// the official Legendary Encounters: Predator turn/phase state machine. Every change goes
// through an explicit operation below and is stored in TableState so Undo/Redo and
// deterministic replay work across both manual drag-and-drop and rule actions.

#include "core/Math.h"
#include "game/Assets.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc {

using PileId = std::uint32_t;
constexpr PileId kNoPile = 0;

/// Official 4-step turn sequence from Rulebook p.7 ("On Your Turn").
enum class TurnPhase {
    EnemyPhase = 0, // 1. Draw an Enemy card face-down to Underground (+ Runner step)
    ActionPhase,    // 2. Play cards from hand, Recruit, Scan Wilds, Fight Enemies
    StrikePhase,    // 3. Each Enemy in Combat Zone strikes from Right to Left
    CleanupPhase,   // 4. Discard hand & played cards, then draw 6 new cards
    Victory,        // Major Victory (Final Enemy killed) or Minor Victory (The Choppa)
    Defeat          // All players killed or Enemy Deck depleted twice
};

/// Game Setup configuration chosen in the New Game Setup Wizard.
struct GameSetupConfig {
    int movie = 1;                // 1 = Predator (1987), 2 = Predator 2 (1990)
    bool playAsPredators = false; // false = Humans (Co-op), true = Predators (Hunter Mode)
    int playerCount = 1;          // 1..5 players
    int avatarDefId = 72;         // Player 1's Avatar card defId (72 = Lieutenant)
    std::vector<int> playerAvatars{72, 82, 70, 64, 76}; // Unique per-player Avatar selection (1..5P)
};

/// Structured record of a card or stack movement for gameplay telemetry & rule auditing.
struct MoveLogEntry {
    std::uint32_t seq = 0;
    std::string timestamp;
    int turn = 1;
    std::string phase;
    std::string actionType; // DRAG_DROP, MULTI_DROP, STACK_MERGE, PLAY_CARD, RECRUIT, etc.
    std::string cardSummary;
    std::string fromZone;
    std::string toZone;
    int cardCount = 1;
    int destStackCount = 1;
    bool faceUp = true;
    std::string stateDelta;
};

/// One physical card.
struct CardInst {
    std::uint32_t uid = 0;
    int def = 0; // CardDef id
    bool faceUp = false;
    bool attached = false; // Attached to a Wilds location (immune to regular movement, stacked at bottom)
};

/// Anything lying on the table; a single card is a pile of one.
struct Pile {
    PileId id = kNoPile;
    Vec2 pos;                    // centre, world units
    int quarterTurns = 0;        // 0 = upright, 1 = turned 90 degrees clockwise
    std::vector<CardInst> cards; // bottom -> top (back() is visible; attached cards sit at front())
    std::string zone;            // zone id it rests in, "" when loose
    std::string label;           // caption under the pile (box groups)

    const CardInst& top() const { return cards.back(); }
};

/// Per-player state for 1..5 separate player boards & unique characters.
struct PlayerState {
    int avatarDefId = 72;
    int health = 9;
    int defense = 5;
    int strikeDamage = 0;
    int marksCount = 0;
    int manualStrikeOffset = 0;
    int manualMarksOffset = 0;
    int nextHandSizePenalty = 0;
    int cannotDieTurnsRemaining = 0;
    bool alive = true;
    bool coordinatedThisTurn = false;
};

/// Everything that is saved, undone, and synchronised between players.
struct TableState {
    std::vector<Pile> piles; // vector order = z-order, back() is on top
    PileId nextPileId = 1;
    std::uint32_t nextUid = 1;

    // --- Official Rulebook Turn & Tactical State ---
    GameSetupConfig setupConfig{};
    int turnNumber = 1;
    int playerCount = 1;
    int activePlayer = 0;
    std::vector<PlayerState> players;
    TurnPhase phase = TurnPhase::EnemyPhase;

    int recruitPool = 0;
    int attackPool = 0;
    int avatarHealth = 9;     // Mirrored from players[activePlayer]
    int avatarDefense = 5;    // Mirrored from players[activePlayer]
    int strikeDamage = 0;     // Mirrored from players[activePlayer]
    int marksCount = 0;       // Mirrored from players[activePlayer]
    int manualStrikeOffset = 0;
    int manualMarksOffset = 0;
    int currentObjective = 1; // 1 = Expendable Assets, 2 = Flares/Frags, 3 = Get to the Choppa!
    int enemyDeckReshuffles = 0;

    int avoidStrikesAvailable = 0;
    int freeScansAvailable = 0;
    int combatZoneAttackPenalty = 0;
    int drawCardsOnKill = 0;
    int nextHandSizePenalty = 0;
    int cannotDieTurnsRemaining = 0;
    int coordinatedCountThisTurn = 0;
    bool defensivePositionAttachedToHills = false;

    std::vector<std::string> playedClasses;
    std::vector<std::string> playedTeams;
    std::vector<std::string> playedCharacters;
    std::vector<std::uint32_t> accountedPlayedUids; // Tracks cards in active player's played area already credited
    std::string lastActionLog = "Mission ready: Step 1 — Enemy Phase.";

    std::uint32_t nextMoveSeq = 1;
    std::vector<MoveLogEntry> moveLog;
};

/// Presentation-only animation state per pile; never undone or networked.
struct PileVisual {
    Vec2 pos;
    float angle = 0.f;   // radians
    float lift = 0.f;    // 0 resting .. 1 held in hand
    float flipT = 1.f;   // 0..1 progress of a flip animation (1 = settled)
    Vec2 topOffset;      // top card offset decaying to zero after a merge
};

struct DropTarget {
    enum class Kind { Loose, Pile, Slot, Area } kind = Kind::Loose;
    PileId pile = kNoPile;      // Kind::Pile -> stack onto this pile
    const Zone* zone = nullptr; // Slot / Area
    Vec2 pos;                   // resting centre when not stacking
    int quarterTurns = -1;      // >= 0 forces an orientation (landscape slots)
    Rect highlight;             // what to glow while hovering
};

class Table {
public:
    Table(const Catalog& catalog, const BoardLayout& layout);

    const TableState& state() const { return state_; }
    TableState& mutableState() { return state_; }
    const Catalog& catalog() const { return catalog_; }
    const BoardLayout& layout() const { return layout_; }

    void reset(TableState s);

    // --- player & zone queries -------------------------------------------------
    std::string playerZone(int playerIdx, std::string_view role) const;
    std::string activePlayerZone(std::string_view role) const;
    void setActivePlayer(int playerIdx);

    const Pile* find(PileId id) const;
    Pile* find(PileId id);
    const Pile* findZonePile(const std::string& zoneId) const;
    Pile* findZonePile(const std::string& zoneId);
    PileId pileAt(Vec2 world) const;    // topmost pile under the point (incl. attached header)
    Vec2 pileSize(const Pile& p) const; // axis-aligned extent incl. rotation
    Rect pileRect(const Pile& p) const;
    Vec2 cardSize(int def) const;       // upright extent of a card
    const Zone* zoneAt(Vec2 world) const;

    int scanCostForZone(const std::string& zoneId) const;
    int effectiveEnemyAttack(const Pile& p) const;
    bool isWildsSpaceClear(int slotIdx) const; // True if no unattached card occupies wilds_{slotIdx}
    int countDiscardedEvents() const;          // Number of Event cards in discard_hazards
    bool canCompleteObjective2() const;

    // --- physical tabletop operations (call checkpoint() once per user action) --
    void checkpoint();
    PileId splitTop(PileId src, int count); // new pile holding the top `count` cards
    PileId pullCardAt(PileId src, size_t cardIndex, Vec2 dstPos);
    PileId spawnCard(int defId, Vec2 pos, bool faceUp = true);
    void bringToFront(PileId id);
    void place(PileId id, Vec2 pos, const std::string& zone, int quarterTurns = -1);
    void mergeOnto(PileId src, PileId dst); // src cards go on top of dst (or bottom if attached)
    void flipTop(PileId id);
    void flipPile(PileId id);              // turn the whole pile over (order reverses)
    void rotate(PileId id);
    void shuffle(PileId id, std::uint32_t seed);

    DropTarget resolveDrop(PileId dragged, bool freePlace) const;
    void applyDrop(PileId dragged, const DropTarget& t);
    void applyDropWithLog(PileId dragged, const DropTarget& t, const std::string& srcZoneBefore);
    void applyMultiDrop(const std::vector<PileId>& draggedPiles, const DropTarget& primaryTarget,
                        const std::vector<std::string>& srcZonesBefore);
    PileId gatherPiles(const std::vector<PileId>& pileIds, Vec2 targetPos);

    // --- reactive zone sync & movement telemetry -------------------------------
    void reflowZone(const std::string& zoneId);
    void reflowAllPlayerAreas();
    void syncStateFromZones(std::string* deltaOut = nullptr);
    void recordMove(const std::string& actionType, const std::string& cardSummary, int cardCount,
                    const std::string& fromZone, const std::string& toZone, int destStackCount,
                    bool faceUp, const std::string& stateDelta);
    const std::filesystem::path& logFilePath() const { return logFilePath_; }

    // --- official rulebook flow & actions --------------------------------------
    std::string stepPhase(std::uint32_t seed);
    std::string resolveEnemyPhase(std::uint32_t seed);
    std::string playCard(PileId handPileId, std::uint32_t seed);
    std::string playAllHandCards(std::uint32_t seed);
    std::string coordinateCard(PileId handPileId, std::uint32_t seed);
    std::string recruitCard(PileId targetPileId, std::uint32_t seed);
    std::string scanWilds(PileId targetPileId, std::uint32_t seed);
    std::string fightEnemy(PileId targetPileId, std::uint32_t seed);
    std::string completeObjective2();
    std::string resolveStrikePhase(std::uint32_t seed);
    std::string resolveCleanupPhase(std::uint32_t seed);
    void refillHQ();
    void layoutPlayerRow(const std::string& zoneId, float startX, float y, float stepX,
                         const std::string& firstLabel);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // --- presentation ------------------------------------------------------------
    PileVisual& visual(PileId id);
    void updateVisuals(float dt, PileId held);

private:
    int indexOf(PileId id) const;
    void syncVisuals();
    Pile& ensureZonePile(const std::string& zoneId, Vec2 defaultPos, const std::string& label);
    void attachCardToWildsSlot(int slotIdx, CardInst card);
    void revealAndAttachGuerrillaCamp(CardInst camp, std::string& logOut);
    void pushCardIntoWildsSlot(int slotIdx, CardInst card, std::uint32_t seed,
                               std::string& logOut);
    void moveCardToCombatZone(CardInst card, std::uint32_t seed, std::string& logOut);
    void onCardRevealedInWilds(PileId wildsPileId, int slotIdx, std::uint32_t seed,
                               std::string& logOut);
    void resolveObjectiveEvent(std::uint32_t seed, std::string& logOut);
    void resolveLocationHazard(CardInst hazardCard, std::uint32_t seed, std::string& logOut);
    void applyPlayedCardAbility(int defId, bool classAlreadyPlayed, bool teamAlreadyPlayed,
                                int otherBarracksChars, std::uint32_t seed, std::string& logOut);
    void drawPlayerCards(int playerIdx, int count, std::uint32_t seed, std::string& logOut);
    void drawStrikeAgainstPlayer(int playerIdx, std::uint32_t seed, std::string& logOut);
    void healPlayerStrikes(int playerIdx, int count, bool leastDamageFirst, std::string& logOut);
    void eliminatePlayer(int playerIdx, std::string& logOut);
    void advanceObjectiveStack();

    const Catalog& catalog_;
    const BoardLayout& layout_;
    TableState state_;
    std::deque<TableState> undo_, redo_;
    std::unordered_map<PileId, PileVisual> visuals_;
    std::filesystem::path logFilePath_;
    std::filesystem::path textLogFilePath_;
};

/// Default preferred unique Avatar IDs (5 entries) for a given Movie (1 or 2) & Faction.
std::vector<int> defaultAvatarsForScenario(int movie, bool playAsPredators);

/// Resolve `1..5` guaranteed-unique, faction-valid Avatar IDs from `cfg`.
std::vector<int> resolveUniquePlayerAvatars(const GameSetupConfig& cfg);

/// Build game setup from a GameSetupConfig (Movie 1/2, Humans/Predators, 1..5P, unique Avatars)
/// with separate 7-zone Player Board Areas for every player.
TableState buildGameSetup(const Catalog& catalog, const BoardLayout& layout, std::uint32_t seed,
                          const GameSetupConfig& cfg);

/// Default Val Verde (Predator movie, human players) setup.
TableState buildDefaultSetup(const Catalog& catalog, const BoardLayout& layout, std::uint32_t seed,
                             int playerCount = 1);

} // namespace lc
