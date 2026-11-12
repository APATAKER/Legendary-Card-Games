#include "game/Table.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>

namespace lc {

namespace {

constexpr size_t kMaxUndo = 64;

bool hasKeyword(const CardDef& d, std::string_view kw) {
    for (const auto& k : d.keywords) {
        if (k == kw) return true;
    }
    return false;
}

bool containsStr(const std::vector<std::string>& vec, std::string_view val) {
    if (val.empty()) return false;
    return std::find(vec.begin(), vec.end(), val) != vec.end();
}

int wildsIndexFromZone(const std::string& zoneId) {
    if (zoneId == "wilds_0") return 0; // Hills
    if (zoneId == "wilds_1") return 1; // Ruins
    if (zoneId == "wilds_2") return 2; // River
    if (zoneId == "wilds_3") return 3; // Graveyard
    if (zoneId == "wilds_4") return 4; // Underground
    return -1;
}

const char* wildsName(int idx) {
    switch (idx) {
    case 0:
        return "Hills";
    case 1:
        return "Ruins";
    case 2:
        return "River";
    case 3:
        return "Graveyard";
    case 4:
        return "Underground";
    default:
        return "Wilds";
    }
}

bool isEventDef(int defId) {
    return defId == 105 || defId == 112 || defId == 122 || defId == 214 || defId == 127 ||
           defId == 136 || defId == 146;
}

bool isHazardDef(int defId) {
    return defId == 106 || defId == 113 || defId == 123 || defId == 128 || defId == 137 ||
           defId == 147;
}

bool isBarracksCharacterDef(int defId) { return defId >= 0 && defId <= 63; }

const char* phaseShortName(TurnPhase ph) {
    switch (ph) {
    case TurnPhase::EnemyPhase:
        return "EnemyPhase";
    case TurnPhase::ActionPhase:
        return "ActionPhase";
    case TurnPhase::StrikePhase:
        return "StrikePhase";
    case TurnPhase::CleanupPhase:
        return "CleanupPhase";
    case TurnPhase::Victory:
        return "Victory";
    case TurnPhase::Defeat:
        return "Defeat";
    }
    return "Unknown";
}

std::string nowTimestampStr() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            continue;
        else
            out += c;
    }
    return out;
}

bool isHandOrPlayedZone(std::string_view zoneId) {
    int p = 0;
    std::string_view role;
    if (!parsePlayerZone(zoneId, p, role)) return false;
    return role == "hand" || role == "played";
}

} // namespace

Table::Table(const Catalog& catalog, const BoardLayout& layout)
    : catalog_(catalog), layout_(layout) {
    std::error_code ec;
    std::filesystem::path logDir = std::filesystem::current_path() / "logs";
#ifdef LC_ASSET_DIR
    logDir = std::filesystem::path(LC_ASSET_DIR).parent_path() / "logs";
#endif
    std::filesystem::create_directories(logDir, ec);
    logFilePath_ = logDir / "movement_log.jsonl";
    textLogFilePath_ = logDir / "movement_log.txt";
    bugReportJsonlPath_ = logDir / "bug_reports.jsonl";
    bugReportMdPath_ = logDir / "bug_reports.md";
    loadBugReportsFromDisk();
}

std::string Table::playerZone(int playerIdx, std::string_view role) const {
    const int n = std::max(1, state_.playerCount);
    return playerZoneId(std::clamp(playerIdx, 0, n - 1), role);
}

std::string Table::activePlayerZone(std::string_view role) const {
    return playerZone(state_.activePlayer, role);
}

void Table::setActivePlayer(int playerIdx) {
    const int n = std::max(1, state_.playerCount);
    const int clamped = std::clamp(playerIdx, 0, n - 1);
    if (clamped == state_.activePlayer) return;

    state_.activePlayer = clamped;
    state_.accountedPlayedUids.clear();
    const std::string playedZid = activePlayerZone("played");
    for (const auto& p : state_.piles) {
        if (p.zone == playedZid) {
            for (const auto& c : p.cards) {
                state_.accountedPlayedUids.push_back(c.uid);
            }
        }
    }
    syncStateFromZones();
}

void Table::reset(TableState s) {
    state_ = std::move(s);
    if (state_.players.empty()) {
        state_.playerCount = std::max(1, state_.playerCount);
        state_.players.resize(static_cast<size_t>(state_.playerCount));
        for (int p = 0; p < state_.playerCount; ++p) {
            int av = (p < static_cast<int>(state_.setupConfig.playerAvatars.size()))
                         ? state_.setupConfig.playerAvatars[static_cast<size_t>(p)]
                         : state_.setupConfig.avatarDefId;
            state_.players[static_cast<size_t>(p)].avatarDefId = av;
            avatarStatsForDef(av, state_.players[static_cast<size_t>(p)].health,
                              state_.players[static_cast<size_t>(p)].defense);
        }
    }
    undo_.clear();
    redo_.clear();
    visuals_.clear();
    reflowAllPlayerAreas();
    syncStateFromZones();
    syncVisuals();
    recordMove("SETUP_GAME",
               state_.setupConfig.movie == 2 ? "Predator 2 Setup" : "Predator 1 Setup",
               static_cast<int>(state_.piles.size()), "box", "table",
               static_cast<int>(state_.piles.size()), true, state_.lastActionLog);
}

int Table::indexOf(PileId id) const {
    for (size_t i = 0; i < state_.piles.size(); ++i) {
        if (state_.piles[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

const Pile* Table::find(PileId id) const {
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : &state_.piles[static_cast<size_t>(idx)];
}

Pile* Table::find(PileId id) {
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : &state_.piles[static_cast<size_t>(idx)];
}

const Pile* Table::findZonePile(const std::string& zoneId) const {
    for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
        const Pile& p = state_.piles[static_cast<size_t>(i)];
        if (p.zone == zoneId && !p.cards.empty()) return &p;
    }
    return nullptr;
}

Pile* Table::findZonePile(const std::string& zoneId) {
    for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
        Pile& p = state_.piles[static_cast<size_t>(i)];
        if (p.zone == zoneId && !p.cards.empty()) return &p;
    }
    return nullptr;
}

Pile& Table::ensureZonePile(const std::string& zoneId, Vec2 defaultPos, const std::string& label) {
    if (Pile* existing = findZonePile(zoneId)) return *existing;
    Vec2 pos = defaultPos;
    if (const Zone* z = layout_.zone(zoneId)) pos = z->rect.center();
    Pile np;
    np.id = state_.nextPileId++;
    np.pos = pos;
    np.zone = zoneId;
    np.label = label;
    state_.piles.push_back(std::move(np));
    (void)visual(state_.piles.back().id);
    return state_.piles.back();
}

Vec2 Table::cardSize(int def) const {
    const Vec2 s = layout_.cardSize;
    if (def >= 0 && def < static_cast<int>(catalog_.cards.size()) && catalog_.card(def).landscape) {
        return {s.y, s.x};
    }
    return s;
}

Vec2 Table::pileSize(const Pile& p) const {
    const Vec2 base = p.cards.empty() ? layout_.cardSize : cardSize(p.top().def);
    return (p.quarterTurns & 1) ? Vec2{base.y, base.x} : base;
}

Rect Table::pileRect(const Pile& p) const {
    return Rect::fromCenter(p.pos, pileSize(p));
}

PileId Table::pileAt(Vec2 world) const {
    for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
        const Pile& p = state_.piles[static_cast<size_t>(i)];
        if (p.cards.empty()) continue;
        if (pileRect(p).contains(world)) return p.id;
        // Also hit-test the exposed top header of a bottom attached card in a 2-card Wilds stack
        if (p.cards.size() >= 2 && p.cards.front().attached) {
            const Rect attachedHeaderRect =
                Rect::fromCenter(p.pos + Vec2{0.f, -48.f}, pileSize(p));
            if (attachedHeaderRect.contains(world)) return p.id;
        }
    }
    return kNoPile;
}

const Zone* Table::zoneAt(Vec2 world) const {
    for (const auto& z : layout_.zones) {
        if (z.rect.contains(world)) return &z;
    }
    return nullptr;
}

int Table::scanCostForZone(const std::string& zoneId) const {
    const int idx = wildsIndexFromZone(zoneId);
    if (idx < 0) return 0;
    int baseCost = 2;
    if (idx == 2 || idx == 3) baseCost = 3; // River, Graveyard
    if (idx == 4) baseCost = 4;             // Underground

    // Ongoing: Not a Single Track (def 116) adds +2 Attack to scan spaces
    for (const auto& p : state_.piles) {
        if (p.cards.empty() || !p.top().faceUp) continue;
        if (p.top().def == 116 && (p.zone == "combat" || wildsIndexFromZone(p.zone) >= 0)) {
            baseCost += 2;
            break;
        }
    }
    return baseCost;
}

int Table::effectiveEnemyAttack(const Pile& p) const {
    if (p.cards.empty() || !p.top().faceUp) return -1;
    const int defId = p.top().def;
    const CardDef& d = catalog_.card(defId);
    if (d.attack < 0) return -1;

    int atk = d.attack;

    // Soviet Advisor (def 104): Ongoing: Each Val Verde Guerrilla (def 103) has +1 Attack
    if (defId == 103) {
        for (const auto& other : state_.piles) {
            if (other.cards.empty() || !other.top().faceUp) continue;
            if (other.top().def == 104 &&
                (other.zone == "combat" || wildsIndexFromZone(other.zone) >= 0)) {
                atk += 1;
            }
        }
    }

    // It Changed Colors (def 111): Ongoing: -2 Attack for each different Class played/held this turn
    if (defId == 111) {
        const auto& clsList =
            (state_.phase == TurnPhase::ActionPhase && !state_.roundHandClasses.empty())
                ? state_.roundHandClasses
                : state_.playedClasses;
        atk = std::max(0, atk - static_cast<int>(clsList.size()) * 2);
    }

    // Now It Wants Us (def 119): Wilds: -1 Attack for each space away from Combat Zone
    if (defId == 119) {
        const int wIdx = wildsIndexFromZone(p.zone);
        if (wIdx >= 0) {
            atk = std::max(0, atk - (wIdx + 1));
        }
    }

    // Get to the Choppa! (def 9): Enemies in the Combat Zone have -1 Attack this turn
    if (p.zone == "combat" && state_.combatZoneAttackPenalty > 0 && defId != 114) {
        atk = std::max(0, atk - state_.combatZoneAttackPenalty);
    }

    return atk;
}

bool Table::isWildsSpaceClear(int slotIdx) const {
    if (slotIdx < 0 || slotIdx > 4) return false;
    const std::string zid = "wilds_" + std::to_string(slotIdx);
    for (const auto& p : state_.piles) {
        if (p.zone != zid) continue;
        for (const auto& c : p.cards) {
            if (!c.attached) return false;
        }
    }
    return true;
}

bool Table::isWildsSlotSwappable(int slotIdx) const {
    if (slotIdx < 0 || slotIdx > 4) return false;
    const Pile* wp = findZonePile("wilds_" + std::to_string(slotIdx));
    return wp && !wp->cards.empty() && !wp->top().attached;
}

int Table::countDiscardedEvents() const {
    int count = 0;
    for (const auto& p : state_.piles) {
        if (p.zone != "discard_hazards") continue;
        for (const auto& c : p.cards) {
            if (isEventDef(c.def)) ++count;
        }
    }
    return count;
}

bool Table::canCompleteObjective2() const {
    if (state_.currentObjective != 2) return false;
    // Check if Defensive Position (def 107) is in Operations ("testing")
    bool hasDefPos = false;
    for (const auto& p : state_.piles) {
        if (p.zone == "testing" && !p.cards.empty() && p.top().def == 107) {
            hasDefPos = true;
            break;
        }
    }
    if (!hasDefPos) return false;
    // Hills ("wilds_0") must be clear of unattached cards
    if (!isWildsSpaceClear(0)) return false;
    // Need 6 Recruit Points
    return state_.recruitPool >= 6;
}

Vec2 Table::zoneCenterPos(const std::string& zoneId, Vec2 fallback) const {
    if (const Pile* zp = findZonePile(zoneId)) return zp->pos;
    if (const Zone* z = layout_.zone(zoneId)) return z->rect.center();
    return fallback;
}

void Table::queueCardFlight(CardInst card, Vec2 fromPos, Vec2 toPos, bool startFaceUp,
                            bool endFaceUp, int staggerIdx) {
    const float dx = toPos.x - fromPos.x;
    const float dy = toPos.y - fromPos.y;
    if (dx * dx + dy * dy < 4.f && startFaceUp == endFaceUp) return;

    const int idx = (staggerIdx >= 0) ? staggerIdx : (flightBatchCounter_++);
    FlyingCardAnim anim;
    anim.card = card;
    anim.startPos = fromPos;
    anim.endPos = toPos;
    anim.delay = 0.06f * static_cast<float>(std::min(idx, 12));
    anim.elapsed = 0.f;
    anim.duration = 0.32f;
    anim.startFaceUp = startFaceUp;
    anim.endFaceUp = endFaceUp;

    // Replace any existing flight for the same card uid
    for (auto& ex : flyingCards_) {
        if (ex.card.uid == card.uid) {
            ex = anim;
            return;
        }
    }
    if (flyingCards_.size() >= 64) {
        flyingCards_.erase(flyingCards_.begin());
    }
    flyingCards_.push_back(anim);
}

bool Table::isCardInFlight(std::uint32_t cardUid) const {
    for (const auto& anim : flyingCards_) {
        if (anim.card.uid == cardUid && anim.elapsed < anim.delay + anim.duration) {
            return true;
        }
    }
    return false;
}

bool Table::isCardKilledInPlayed(std::uint32_t cardUid) const {
    return std::find(state_.killedPlayedUids.begin(), state_.killedPlayedUids.end(), cardUid) !=
           state_.killedPlayedUids.end();
}

bool Table::isCardStatsSwapped(std::uint32_t cardUid) const {
    return std::find(state_.swappedStatUids.begin(), state_.swappedStatUids.end(), cardUid) !=
           state_.swappedStatUids.end();
}

void Table::effectiveCardStats(const CardInst& c, int* recruitOut, int* attackOut) const {
    const CardDef& d = catalog_.card(c.def);
    int rec = std::max(0, d.recruit);
    int atk = std::max(0, d.attack);
    if (isCardStatsSwapped(c.uid)) {
        std::swap(rec, atk);
    }

    const int nOtherChars = isBarracksCharacterDef(c.def) ? countOtherBarracksChars(d.subtitle) : 0;
    const int coordCount = std::max(0, state_.coordinatedCountThisTurn);

    // Passive Brothers in Arms, Call For Backup & [Class] stat scaling
    if (c.def == 6) {
        rec += 2 * nOtherChars; // Before Anybody Knows We Were There: 1 -> 3 -> 5 -> 7 *
    } else if (c.def == 10) {
        atk += 2 * nOtherChars; // Bad Idea: 1 -> 3 -> 5 -> 7 ///
    } else if (c.def == 71) {
        atk += 1 * coordCount;  // Fighting the Good Fight: +1 /// per Coordinate
    } else if (c.def == 73) {
        rec += 2 * coordCount;  // Command Decisions: +2 * per Coordinate
    } else if (c.def == 91 && countTurnClass("Strength") >= 2) {
        atk += 2;               // Master of the Hunt: [Strength] +2 ///
    } else if (c.def == 93 && countTurnClass("Leadership") >= 2) {
        rec += 2;               // Thrill of the Hunt: [Leadership] +2 *
    }

    if (recruitOut) *recruitOut = rec;
    if (attackOut) *attackOut = atk;
}

std::vector<CardInst> Table::collectActiveTurnCards() const {
    std::vector<CardInst> out;
    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    std::uint32_t lingeringPrevTurnImmunityUid = 0;
    if (state_.activePlayer >= 0 &&
        state_.activePlayer < static_cast<int>(state_.players.size())) {
        const PlayerState& ps = state_.players[static_cast<size_t>(state_.activePlayer)];
        if (ps.activeImmunityCardUid != 0 && ps.cannotDieTurnsRemaining == 1) {
            lingeringPrevTurnImmunityUid = ps.activeImmunityCardUid;
        }
    }

    for (const auto& p : state_.piles) {
        if (p.zone != handZid && p.zone != playedZid) continue;
        for (const auto& c : p.cards) {
            if (lingeringPrevTurnImmunityUid != 0 && c.uid == lingeringPrevTurnImmunityUid) {
                continue;
            }
            out.push_back(c);
        }
    }
    for (const auto& cc : state_.coordinatedCards) {
        out.push_back(cc);
    }
    return out;
}

int Table::countTurnClass(std::string_view classType) const {
    if (classType.empty()) return 0;
    int count = 0;
    for (const auto& c : collectActiveTurnCards()) {
        if (catalog_.card(c.def).classType == classType) ++count;
    }
    return count;
}

int Table::countTurnTeam(std::string_view teamName) const {
    if (teamName.empty()) return 0;
    int count = 0;
    for (const auto& c : collectActiveTurnCards()) {
        if (catalog_.card(c.def).team == teamName) ++count;
    }
    return count;
}

int Table::countOtherBarracksChars(std::string_view mySubtitle) const {
    std::vector<std::string> distinctOthers;
    for (const auto& c : collectActiveTurnCards()) {
        if (!isBarracksCharacterDef(c.def)) continue;
        const std::string& sub = catalog_.card(c.def).subtitle;
        if (sub.empty() || sub == mySubtitle) continue;
        if (!containsStr(distinctOthers, sub)) {
            distinctOthers.push_back(sub);
        }
    }
    return static_cast<int>(distinctOthers.size());
}

int Table::expectedStrikesThisTurn(int* rawStrikesOut, int* avoidOut) const {
    int raw = 0;
    if (state_.marksCount > 0) {
        for (int s = 0; s <= 4; ++s) {
            if (const Pile* wp = findZonePile("wilds_" + std::to_string(s));
                wp && !wp->cards.empty() && wp->top().faceUp && wp->top().def == 108) {
                raw += 1;
            }
        }
    }
    for (const auto& p : state_.piles) {
        if (p.zone == "combat" && !p.cards.empty() && p.top().faceUp && p.top().def != 114) {
            const CardDef& d = catalog_.card(p.top().def);
            raw += hasKeyword(d, "Double Strike") ? 2 : 1;
        }
    }
    const int avoid = std::max(0, state_.avoidStrikesAvailable);
    if (rawStrikesOut) *rawStrikesOut = raw;
    if (avoidOut) *avoidOut = avoid;
    return std::max(0, raw - avoid);
}

void Table::checkpoint() {
    undo_.push_back(state_);
    if (undo_.size() > kMaxUndo) undo_.pop_front();
    redo_.clear();
}

bool Table::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(state_);
    state_ = std::move(undo_.back());
    undo_.pop_back();
    syncVisuals();
    return true;
}

bool Table::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(state_);
    state_ = std::move(redo_.back());
    redo_.pop_back();
    syncVisuals();
    return true;
}

void Table::bringToFront(PileId id) {
    const int idx = indexOf(id);
    if (idx < 0 || idx == static_cast<int>(state_.piles.size()) - 1) return;
    Pile p = std::move(state_.piles[static_cast<size_t>(idx)]);
    state_.piles.erase(state_.piles.begin() + idx);
    state_.piles.push_back(std::move(p));
}

PileId Table::splitTop(PileId srcId, int count) {
    Pile* src = find(srcId);
    if (!src || count <= 0) return kNoPile;
    if (count >= static_cast<int>(src->cards.size())) {
        src->zone.clear();
        bringToFront(srcId);
        return srcId;
    }

    Pile np;
    np.id = state_.nextPileId++;
    np.pos = src->pos;
    np.quarterTurns = src->quarterTurns;
    np.cards.assign(src->cards.end() - count, src->cards.end());
    src->cards.erase(src->cards.end() - count, src->cards.end());

    PileVisual pv = visual(srcId);
    state_.piles.push_back(std::move(np));
    visuals_[state_.piles.back().id] = pv;
    return state_.piles.back().id;
}

PileId Table::pullCardAt(PileId srcId, size_t cardIndex, Vec2 dstPos) {
    Pile* src = find(srcId);
    if (!src || cardIndex >= src->cards.size()) return kNoPile;

    CardInst pulled = src->cards[cardIndex];
    pulled.faceUp = true;
    pulled.attached = false;
    src->cards.erase(src->cards.begin() + static_cast<std::ptrdiff_t>(cardIndex));
    if (src->cards.empty()) {
        const int sIdx = indexOf(srcId);
        if (sIdx >= 0) state_.piles.erase(state_.piles.begin() + sIdx);
        visuals_.erase(srcId);
    }

    Pile np;
    np.id = state_.nextPileId++;
    np.pos = dstPos;
    np.cards = {pulled};
    state_.piles.push_back(std::move(np));
    (void)visual(state_.piles.back().id);
    return state_.piles.back().id;
}

PileId Table::spawnCard(int defId, Vec2 pos, bool faceUp) {
    Pile np;
    np.id = state_.nextPileId++;
    np.pos = pos;
    np.cards = {{state_.nextUid++, defId, faceUp, false}};
    state_.piles.push_back(std::move(np));
    (void)visual(state_.piles.back().id);
    return state_.piles.back().id;
}

void Table::place(PileId id, Vec2 pos, const std::string& zone, int quarterTurns) {
    Pile* p = find(id);
    if (!p) return;
    p->pos = pos;
    p->zone = zone;
    if (quarterTurns >= 0) p->quarterTurns = quarterTurns & 1;
    if (wildsIndexFromZone(zone) < 0) {
        for (auto& c : p->cards) c.attached = false;
    }
}

void Table::mergeOnto(PileId srcId, PileId dstId) {
    if (srcId == dstId) return;
    Pile* src = find(srcId);
    Pile* dst = find(dstId);
    if (!src || !dst) return;

    const Vec2 srcVisualPos = visual(srcId).pos;
    const bool dstInWilds = (wildsIndexFromZone(dst->zone) >= 0);

    for (CardInst c : src->cards) {
        if (!dstInWilds) c.attached = false;
        if (dstInWilds && c.attached) {
            auto it = std::find_if(dst->cards.begin(), dst->cards.end(),
                                   [](const CardInst& ex) { return !ex.attached; });
            dst->cards.insert(it, c);
        } else {
            dst->cards.push_back(c);
        }
    }

    PileVisual& dv = visual(dstId);
    dv.topOffset = srcVisualPos - dv.pos;

    const int sIdx = indexOf(srcId);
    if (sIdx >= 0) state_.piles.erase(state_.piles.begin() + sIdx);
    visuals_.erase(srcId);
    bringToFront(dstId);
}

void Table::flipTop(PileId id) {
    Pile* p = find(id);
    if (!p || p->cards.empty()) return;
    p->cards.back().faceUp = !p->cards.back().faceUp;
    visual(id).flipT = 0.f;

    if (!p->cards.back().faceUp) {
        p->cards.back().attached = false;
        return;
    }

    // If a hidden card in The Wilds was flipped face-up, resolve its Reveal ability (e.g. Attach!)
    const int wIdx = wildsIndexFromZone(p->zone);
    if (wIdx >= 0 && !p->cards.back().attached) {
        std::string log = "Revealed card in " + std::string(wildsName(wIdx)) + ".";
        onCardRevealedInWilds(id, wIdx, state_.nextMoveSeq * 2654435761u + 7u, log);
        state_.lastActionLog = log;
    }
}

void Table::flipPile(PileId id) {
    Pile* p = find(id);
    if (!p || p->cards.empty()) return;
    std::reverse(p->cards.begin(), p->cards.end());
    for (auto& c : p->cards) {
        c.faceUp = !c.faceUp;
        if (!c.faceUp) c.attached = false;
    }
    visual(id).flipT = 0.f;
}

void Table::rotate(PileId id) {
    Pile* p = find(id);
    if (!p) return;
    p->quarterTurns = (p->quarterTurns + 1) & 1;
}

void Table::shuffle(PileId id, std::uint32_t seed) {
    Pile* p = find(id);
    if (!p || p->cards.size() <= 1) return;
    std::mt19937 rng(seed);
    std::shuffle(p->cards.begin(), p->cards.end(), rng);
    visual(id).flipT = 0.f;
}

DropTarget Table::resolveDrop(PileId draggedId, bool freePlace) const {
    DropTarget out;
    const Pile* dragged = find(draggedId);
    if (!dragged) return out;

    const Vec2 pos = dragged->pos;
    const Rect dRect = pileRect(*dragged);
    out.pos = pos;

    if (freePlace) {
        out.kind = DropTarget::Kind::Loose;
        return out;
    }

    // 1. Check board & player zones first so Area zones (Hand Area, Played Area, Combat Zone) fan cleanly
    //    without accidentally merging onto adjacent fanned cards, while Slot zones merge onto the slot stack.
    const Zone* bestZone = nullptr;
    float bestScore = 0.f;
    for (const auto& z : layout_.zones) {
        const Rect snapRect = z.rect.inflated(20.f);
        const float area = dRect.overlapArea(snapRect);
        if (area > bestScore) {
            bestScore = area;
            bestZone = &z;
        }
    }

    if (bestZone && bestScore >= dRect.w * dRect.h * 0.18f) {
        out.zone = bestZone;
        out.highlight = bestZone->rect;
        if (bestZone->kind == ZoneKind::Slot) {
            for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
                const Pile& other = state_.piles[static_cast<size_t>(i)];
                if (other.id == draggedId || other.cards.empty()) continue;
                if (other.zone == bestZone->id || bestZone->rect.contains(other.pos)) {
                    out.kind = DropTarget::Kind::Pile;
                    out.pile = other.id;
                    out.pos = other.pos;
                    return out;
                }
            }
            out.kind = DropTarget::Kind::Slot;
            out.pos = bestZone->rect.center();
            const bool cardIsLand =
                !dragged->cards.empty() && catalog_.card(dragged->top().def).landscape;
            out.quarterTurns = (bestZone->landscape != cardIsLand) ? 1 : 0;
            return out;
        } else {
            out.kind = DropTarget::Kind::Area;
            const Vec2 sz = pileSize(*dragged);
            const float minX = bestZone->rect.x + sz.x * 0.5f + 6.f;
            const float maxX = std::max(minX, bestZone->rect.right() - sz.x * 0.5f - 6.f);
            out.pos = {std::clamp(pos.x, minX, maxX), bestZone->rect.center().y};
            out.quarterTurns = 0;
            return out;
        }
    }

    // 2. Outside board/player zones: check overlap with a loose pile for merging/stacking.
    const float minMergeArea = dRect.w * dRect.h * 0.35f;
    for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
        const Pile& other = state_.piles[static_cast<size_t>(i)];
        if (other.id == draggedId || other.cards.empty()) continue;
        const Rect oRect = pileRect(other);
        if (dRect.overlapArea(oRect) >= minMergeArea) {
            out.kind = DropTarget::Kind::Pile;
            out.pile = other.id;
            out.pos = other.pos;
            out.highlight = oRect.inflated(6.f);
            return out;
        }
    }

    return out;
}

void Table::applyDrop(PileId draggedId, const DropTarget& t) {
    switch (t.kind) {
    case DropTarget::Kind::Pile:
        mergeOnto(draggedId, t.pile);
        break;
    case DropTarget::Kind::Slot:
    case DropTarget::Kind::Area:
        place(draggedId, t.pos, t.zone ? t.zone->id : "", t.quarterTurns);
        break;
    case DropTarget::Kind::Loose:
        place(draggedId, t.pos, "", -1);
        break;
    }
}

void Table::recordMove(const std::string& actionType, const std::string& cardSummary, int cardCount,
                       const std::string& fromZone, const std::string& toZone, int destStackCount,
                       bool faceUp, const std::string& stateDelta) {
    MoveLogEntry entry;
    entry.seq = state_.nextMoveSeq++;
    entry.timestamp = nowTimestampStr();
    entry.turn = state_.turnNumber;
    entry.phase = phaseShortName(state_.phase);
    entry.actionType = actionType;
    entry.cardSummary = cardSummary;
    entry.fromZone = fromZone.empty() ? "loose" : fromZone;
    entry.toZone = toZone.empty() ? "loose" : toZone;
    entry.cardCount = cardCount;
    entry.destStackCount = destStackCount;
    entry.faceUp = faceUp;
    entry.stateDelta = stateDelta;

    state_.moveLog.push_back(entry);
    if (state_.moveLog.size() > 500) {
        state_.moveLog.erase(state_.moveLog.begin());
    }

    if (!logFilePath_.empty()) {
        std::ofstream jf(logFilePath_, std::ios::app);
        if (jf) {
            jf << "{\"seq\":" << entry.seq << ",\"ts\":\"" << jsonEscape(entry.timestamp)
               << "\",\"turn\":" << entry.turn << ",\"phase\":\"" << jsonEscape(entry.phase)
               << "\",\"action\":\"" << jsonEscape(entry.actionType) << "\",\"cards\":\""
               << jsonEscape(entry.cardSummary) << "\",\"count\":" << entry.cardCount
               << ",\"from\":\"" << jsonEscape(entry.fromZone) << "\",\"to\":\""
               << jsonEscape(entry.toZone) << "\",\"destStack\":" << entry.destStackCount
               << ",\"faceUp\":" << (entry.faceUp ? "true" : "false") << ",\"delta\":\""
               << jsonEscape(entry.stateDelta) << "\"}\n";
        }
    }
    if (!textLogFilePath_.empty()) {
        std::ofstream tf(textLogFilePath_, std::ios::app);
        if (tf) {
            tf << "#" << std::setw(4) << std::setfill('0') << entry.seq << " [" << entry.timestamp
               << "] [T" << entry.turn << " " << entry.phase << "] " << std::setw(14)
               << std::setfill(' ') << std::left << entry.actionType << " | " << entry.cardCount
               << "x " << entry.cardSummary << " | " << entry.fromZone << " -> " << entry.toZone
               << " (stack=" << entry.destStackCount << ", "
               << (entry.faceUp ? "FaceUp" : "FaceDown") << ") | " << entry.stateDelta << "\n";
        }
    }
}

void Table::setBugReportPaths(const std::filesystem::path& jsonlPath,
                              const std::filesystem::path& mdPath) {
    bugReportJsonlPath_ = jsonlPath;
    bugReportMdPath_ = mdPath;
    loadBugReportsFromDisk();
}

int Table::openBugReportCount() const {
    int count = 0;
    for (const auto& r : bugReports_) {
        if (r.status != "RESOLVED") ++count;
    }
    return count;
}

void Table::loadBugReportsFromDisk() {
    bugReports_.clear();
    nextBugReportId_ = 1;
    if (bugReportJsonlPath_.empty() || !std::filesystem::exists(bugReportJsonlPath_)) return;

    std::ifstream in(bugReportJsonlPath_);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;

        BugReportEntry r;
        r.id = j.value("id", nextBugReportId_);
        r.reportCode = j.value("reportCode", "BUG-" + std::to_string(r.id));
        r.timestamp = j.value("timestamp", "");
        r.status = j.value("status", "OPEN");
        r.category = j.value("category", "Card Ability / Rules");
        r.severity = j.value("severity", "Normal");
        r.title = j.value("title", "");
        r.description = j.value("description", "");
        r.subjectCard = j.value("subjectCard", "");
        r.subjectZone = j.value("subjectZone", "");
        r.turn = j.value("turn", 1);
        r.phase = j.value("phase", "");
        r.scenarioSummary = j.value("scenarioSummary", "");
        r.tacticalSnapshot = j.value("tacticalSnapshot", "");
        r.boardSnapshot = j.value("boardSnapshot", "");
        if (j.contains("recentMoves") && j["recentMoves"].is_array()) {
            for (const auto& m : j["recentMoves"]) {
                if (m.is_string()) r.recentMoves.push_back(m.get<std::string>());
            }
        }
        nextBugReportId_ = std::max(nextBugReportId_, r.id + 1);
        bugReports_.push_back(std::move(r));
    }
}

std::string Table::formatBugReportMarkdown(const BugReportEntry& r) const {
    std::ostringstream md;
    md << "## [" << r.reportCode << "] " << r.title << "\n";
    md << "- **Status**: `" << r.status << "`  |  **Severity**: `" << r.severity
       << "`  |  **Category**: `" << r.category << "`\n";
    md << "- **Timestamp**: " << r.timestamp << "  |  **Scenario**: " << r.scenarioSummary
       << "  |  **Turn / Phase**: Turn " << r.turn << " (" << r.phase << ")\n";
    if (!r.subjectCard.empty() || !r.subjectZone.empty()) {
        md << "- **Subject Card / Action**: "
           << (r.subjectCard.empty() ? "N/A" : r.subjectCard)
           << "  |  **Zone**: " << (r.subjectZone.empty() ? "N/A" : r.subjectZone) << "\n";
    }
    md << "\n### User Notes / Expected vs Actual\n";
    md << (r.description.empty() ? "_(No additional user notes provided)_" : r.description)
       << "\n";

    md << "\n### Tactical State Snapshot\n```text\n" << r.tacticalSnapshot << "```\n";
    md << "\n### Board Zones Snapshot\n```text\n" << r.boardSnapshot << "```\n";

    md << "\n### Recent Move History (" << r.recentMoves.size() << " events)\n```text\n";
    if (r.recentMoves.empty()) {
        md << "(No moves recorded yet)\n";
    } else {
        for (const auto& line : r.recentMoves) {
            md << line << "\n";
        }
    }
    md << "```\n";
    return md.str();
}

void Table::saveBugReportsToDisk() const {
    if (!bugReportJsonlPath_.empty()) {
        std::error_code ec;
        if (bugReportJsonlPath_.has_parent_path()) {
            std::filesystem::create_directories(bugReportJsonlPath_.parent_path(), ec);
        }
        std::ofstream jf(bugReportJsonlPath_, std::ios::trunc);
        if (jf) {
            for (const auto& r : bugReports_) {
                nlohmann::json j;
                j["id"] = r.id;
                j["reportCode"] = r.reportCode;
                j["timestamp"] = r.timestamp;
                j["status"] = r.status;
                j["category"] = r.category;
                j["severity"] = r.severity;
                j["title"] = r.title;
                j["description"] = r.description;
                j["subjectCard"] = r.subjectCard;
                j["subjectZone"] = r.subjectZone;
                j["turn"] = r.turn;
                j["phase"] = r.phase;
                j["scenarioSummary"] = r.scenarioSummary;
                j["tacticalSnapshot"] = r.tacticalSnapshot;
                j["boardSnapshot"] = r.boardSnapshot;
                j["recentMoves"] = r.recentMoves;
                jf << j.dump() << "\n";
            }
        }
    }

    if (!bugReportMdPath_.empty()) {
        std::error_code ec;
        if (bugReportMdPath_.has_parent_path()) {
            std::filesystem::create_directories(bugReportMdPath_.parent_path(), ec);
        }
        std::ofstream mf(bugReportMdPath_, std::ios::trunc);
        if (mf) {
            mf << "# Legendary Encounters: Predator — Recorded Bug Reports\n\n";
            mf << "Total Reports: **" << bugReports_.size() << "**  |  Open: **"
               << openBugReportCount() << "**  |  Resolved: **"
               << (static_cast<int>(bugReports_.size()) - openBugReportCount()) << "**\n\n";

            if (!bugReports_.empty()) {
                mf << "| ID | Status | Severity | Category | Turn / Phase | Subject Card / Zone | Summary | Timestamp |\n";
                mf << "|---|---|---|---|---|---|---|---|\n";
                for (int i = static_cast<int>(bugReports_.size()) - 1; i >= 0; --i) {
                    const auto& r = bugReports_[static_cast<size_t>(i)];
                    std::string subj = r.subjectCard;
                    if (!r.subjectZone.empty()) {
                        subj += subj.empty() ? r.subjectZone : (" (" + r.subjectZone + ")");
                    }
                    if (subj.empty()) subj = "-";
                    mf << "| **" << r.reportCode << "** | `" << r.status << "` | " << r.severity
                       << " | " << r.category << " | T" << r.turn << " " << r.phase << " | "
                       << subj << " | " << r.title << " | " << r.timestamp << " |\n";
                }
                mf << "\n---\n\n";
                for (int i = static_cast<int>(bugReports_.size()) - 1; i >= 0; --i) {
                    mf << formatBugReportMarkdown(bugReports_[static_cast<size_t>(i)]);
                    if (i > 0) mf << "\n---\n\n";
                }
            } else {
                mf << "_No bug reports recorded yet._\n";
            }
        }
    }
}

std::string Table::buildTacticalSnapshot() const {
    std::ostringstream oss;
    const char* movieStr =
        (state_.setupConfig.movie == 2) ? "Predator 2 (LA)" : "Predator 1 (Val Verde)";
    const char* modeStr = state_.setupConfig.playAsPredators ? "Predators (Hunter)" : "Humans (Co-op)";
    const int nPlayers = std::max(1, state_.playerCount);
    const int actP = std::clamp(state_.activePlayer, 0, nPlayers - 1);

    auto avatarLabel = [&](int pIdx) -> std::string {
        if (pIdx >= 0 && pIdx < static_cast<int>(state_.players.size())) {
            const int avId = state_.players[static_cast<size_t>(pIdx)].avatarDefId;
            if (avId >= 0 && avId < static_cast<int>(catalog_.cards.size())) {
                const CardDef& d = catalog_.card(avId);
                return !d.title.empty() ? d.title : d.name;
            }
        }
        return "Operative";
    };

    oss << "Scenario: " << movieStr << " — " << modeStr << " (" << nPlayers << "P)\n";
    oss << "Turn: " << state_.turnNumber << "  |  Phase: " << phaseShortName(state_.phase)
        << "  |  Objective: " << state_.currentObjective
        << "  |  Enemy Reshuffles: " << state_.enemyDeckReshuffles << "\n";
    oss << "Active Turn: P" << (actP + 1) << " (" << avatarLabel(actP) << ")\n";
    oss << "Pools: Recruit=" << state_.recruitPool << " *  |  Attack=" << state_.attackPool
        << " ///  |  FreeScans=" << state_.freeScansAvailable
        << "  |  AvoidStrikes=" << state_.avoidStrikesAvailable
        << "  |  CombatPenalty=" << state_.combatZoneAttackPenalty
        << "  |  DrawOnKill=" << state_.drawCardsOnKill << "\n";

    auto joinVec = [](const std::vector<std::string>& v) {
        if (v.empty()) return std::string("none");
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += ", ";
            s += v[i];
        }
        return s;
    };
    oss << "Played Synergies: Classes=[" << joinVec(state_.playedClasses) << "]  |  Teams=["
        << joinVec(state_.playedTeams) << "]  |  Chars=[" << joinVec(state_.playedCharacters)
        << "]\n";
    if (!state_.coordinatedCards.empty()) {
        oss << "Coordinated Virtual Cards (" << state_.coordinatedCards.size() << "): ";
        for (size_t i = 0; i < state_.coordinatedCards.size(); ++i) {
            if (i > 0) oss << ", ";
            const CardDef& cd = catalog_.card(state_.coordinatedCards[i].def);
            oss << "#" << cd.id << " " << (cd.title.empty() ? cd.name : cd.title);
        }
        oss << "\n";
    }

    // Active Pending Modal check
    std::vector<std::string> modals;
    if (state_.pendingSacrificeChoice) modals.push_back("SacrificeChoice");
    if (state_.pendingCoverMeChoice) modals.push_back("CoverMeChoice");
    if (state_.pendingSneakPeek) modals.push_back("SneakPeekPickSlot");
    if (state_.pendingSneakPeekChoice) modals.push_back("SneakPeekDecision");
    if (state_.pendingKillChoice) modals.push_back("KillCardChoice");
    if (state_.pendingHealChoice) modals.push_back("HealStrikeChoice");
    if (state_.pendingPlayerChoiceMode != 0)
        modals.push_back("PlayerChoiceMode=" + std::to_string(state_.pendingPlayerChoiceMode));
    if (state_.pendingWildsSwapChoice) modals.push_back("WildsSwapChoice");
    if (state_.pendingStatSwapChoice) modals.push_back("StatSwapChoice");
    if (state_.pendingDiscardToDeckChoice) modals.push_back("DiscardToDeckChoice");
    if (state_.pendingGainCardChoice) modals.push_back("GainCardChoice");
    if (state_.pendingLikeAHunterModal) modals.push_back("LikeAHunterRevealModal");
    oss << "Pending Modals: " << joinVec(modals) << "\n";

    oss << "Squad Roster:\n";
    for (int p = 0; p < nPlayers && p < static_cast<int>(state_.players.size()); ++p) {
        const PlayerState& ps = state_.players[static_cast<size_t>(p)];
        const int curHp = std::max(0, ps.health - ps.strikeDamage);
        oss << "  P" << (p + 1) << " (" << avatarLabel(p) << "): HP " << curHp << "/" << ps.health
            << ", DEF " << ps.defense << ", Strikes=" << ps.strikeDamage
            << ", Marks=" << ps.marksCount << ", Alive=" << (ps.alive ? "Yes" : "No");
        if (p == actP) oss << " [ACTIVE]";
        if (ps.escapedInChoppa) oss << " [ESCAPED_CHOPPA]";
        if (ps.coordinatedThisTurn) oss << " [COORD_USED]";
        if (ps.allHandCoordinateThisTurn) oss << " [ALL_HAND_COORD]";
        if (ps.cannotDieTurnsRemaining > 0)
            oss << " [IMMUNE_TURNS=" << ps.cannotDieTurnsRemaining << "]";
        oss << "\n";
    }
    oss << "Last Action Log: " << state_.lastActionLog << "\n";
    return oss.str();
}

std::string Table::buildBoardSnapshot() const {
    std::ostringstream oss;

    auto formatCardInst = [&](const CardInst& c) -> std::string {
        const CardDef& d = catalog_.card(c.def);
        std::string s = "#" + std::to_string(d.id) + " " + (d.title.empty() ? d.name : d.title);
        if (!d.subtitle.empty()) s += " (" + d.subtitle + ")";
        if (!c.faceUp) s += " [FaceDown]";
        if (c.attached) s += " [ATTACHED]";
        if (isCardKilledInPlayed(c.uid)) s += " [KILLED]";
        if (isCardStatsSwapped(c.uid)) s += " [SWAPPED]";
        std::string abilLbl;
        if (canActivateCardAbility(c.uid, &abilLbl, nullptr)) s += " [ABILITY_READY: " + abilLbl + "]";
        return s;
    };

    // 1. Objectives & Commanders
    if (const Pile* obj = findZonePile("objectives"); obj && !obj->cards.empty()) {
        oss << "Objectives (" << obj->cards.size() << "): Top = " << formatCardInst(obj->top())
            << "\n";
    }
    if (const Pile* cmd = findZonePile("commanders"); cmd && !cmd->cards.empty()) {
        oss << "Commanders (" << cmd->cards.size() << "): Top = " << formatCardInst(cmd->top())
            << "\n";
    }

    // 2. The HQ (hq_0..hq_4)
    oss << "HQ Slots:\n";
    for (int h = 0; h < 5; ++h) {
        const std::string zid = "hq_" + std::to_string(h);
        const Pile* hp = findZonePile(zid);
        oss << "  HQ " << (h + 1) << " (" << zid << "): ";
        if (!hp || hp->cards.empty()) {
            oss << "[Empty]\n";
        } else {
            const CardDef& d = catalog_.card(hp->top().def);
            oss << formatCardInst(hp->top()) << " [Cost " << std::max(0, d.cost) << "*, "
                << std::max(0, d.recruit) << "*/" << std::max(0, d.attack) << "///]\n";
        }
    }

    // 3. The Wilds (wilds_0..wilds_4)
    oss << "The Wilds:\n";
    for (int w = 0; w < 5; ++w) {
        const std::string zid = "wilds_" + std::to_string(w);
        const Pile* wp = findZonePile(zid);
        oss << "  " << wildsName(w) << " (" << zid << ", Scan " << scanCostForZone(zid)
            << "///): ";
        if (!wp || wp->cards.empty()) {
            oss << "[Empty]\n";
        } else {
            for (size_t i = 0; i < wp->cards.size(); ++i) {
                if (i > 0) oss << "  +  ";
                oss << formatCardInst(wp->cards[i]);
            }
            if (wp->top().faceUp) {
                const int effAtk = effectiveEnemyAttack(*wp);
                if (effAtk >= 0) oss << " (EffATK=" << effAtk << ")";
            }
            oss << "\n";
        }
    }

    // 4. Combat Zone
    oss << "Combat Zone: ";
    bool anyCombat = false;
    for (const auto& p : state_.piles) {
        if (p.zone == "combat" && !p.cards.empty()) {
            for (const auto& c : p.cards) {
                if (anyCombat) oss << " | ";
                oss << formatCardInst(c);
                anyCombat = true;
            }
        }
    }
    if (!anyCombat) oss << "[Clear]";
    oss << "\n";

    // 5. Shared Decks & Discards
    auto zoneCardCount = [&](const std::string& zid) -> int {
        int total = 0;
        for (const auto& p : state_.piles) {
            if (p.zone == zid) total += static_cast<int>(p.cards.size());
        }
        return total;
    };
    oss << "Shared Piles: EnemyDeck(prey)=" << zoneCardCount("prey")
        << " | Barracks=" << zoneCardCount("barracks")
        << " | StrikesDeck=" << zoneCardCount("strikes")
        << " | MarksDeck=" << zoneCardCount("marks")
        << " | DeadChars=" << zoneCardCount("wounds")
        << " | HazardDiscard=" << zoneCardCount("discard_hazards")
        << " (Events=" << countDiscardedEvents() << ")\n";

    // 6. Per-Player Board Areas
    const int nPlayers = std::max(1, state_.playerCount);
    for (int pIdx = 0; pIdx < nPlayers; ++pIdx) {
        oss << "Player " << (pIdx + 1) << " Zones:\n";
        const std::string handZid = playerZone(pIdx, "hand");
        const std::string playedZid = playerZone(pIdx, "played");
        const std::string strikesZid = playerZone(pIdx, "strikes");
        const std::string marksZid = playerZone(pIdx, "marks");
        const std::string deckZid = playerZone(pIdx, "deck");
        const std::string discZid = playerZone(pIdx, "discard");

        auto listZoneCards = [&](const std::string& zid) -> std::string {
            std::string out;
            int count = 0;
            for (const auto& p : state_.piles) {
                if (p.zone != zid) continue;
                for (const auto& c : p.cards) {
                    if (count > 0) out += ", ";
                    out += formatCardInst(c);
                    ++count;
                }
            }
            if (count == 0) return "[Empty]";
            return "(" + std::to_string(count) + ") " + out;
        };

        oss << "  Hand (" << handZid << "): " << listZoneCards(handZid) << "\n";
        oss << "  Played (" << playedZid << "): " << listZoneCards(playedZid) << "\n";
        oss << "  Strikes (" << strikesZid << "): " << listZoneCards(strikesZid) << "\n";
        oss << "  Marks (" << marksZid << "): " << listZoneCards(marksZid) << "\n";
        oss << "  Deck (" << deckZid << "): " << zoneCardCount(deckZid)
            << " cards  |  Discard (" << discZid << "): " << zoneCardCount(discZid) << " cards";
        if (const Pile* dp = findZonePile(discZid); dp && !dp->cards.empty()) {
            oss << " (Top: " << formatCardInst(dp->top()) << ")";
        }
        oss << "\n";
    }

    return oss.str();
}

std::vector<std::string> Table::formatRecentMoves(int maxCount) const {
    std::vector<std::string> out;
    const int n = static_cast<int>(state_.moveLog.size());
    const int clamped = std::clamp(maxCount, 1, 100);
    const int startIdx = std::max(0, n - clamped);
    for (int i = startIdx; i < n; ++i) {
        const MoveLogEntry& e = state_.moveLog[static_cast<size_t>(i)];
        std::ostringstream line;
        line << "#" << std::setw(4) << std::setfill('0') << e.seq << " [" << e.timestamp << "] [T"
             << e.turn << " " << e.phase << "] " << std::setw(14) << std::setfill(' ') << std::left
             << e.actionType << " | " << e.cardCount << "x " << e.cardSummary << " [" << e.fromZone
             << " -> " << e.toZone << "] (stack=" << e.destStackCount << ", "
             << (e.faceUp ? "FaceUp" : "FaceDown") << ") | " << e.stateDelta;
        out.push_back(line.str());
    }
    return out;
}

const BugReportEntry& Table::submitBugReport(const std::string& title, const std::string& category,
                                             const std::string& severity,
                                             const std::string& description,
                                             const std::string& subjectCard,
                                             const std::string& subjectZone, int recentMoveCount) {
    BugReportEntry r;
    r.id = nextBugReportId_++;
    char codeBuf[32];
    std::snprintf(codeBuf, sizeof codeBuf, "BUG-%04d", r.id);
    r.reportCode = codeBuf;
    r.timestamp = nowTimestampStr();
    r.status = "OPEN";
    r.category = category.empty() ? "Card Ability / Rules" : category;
    r.severity = severity.empty() ? "Normal" : severity;
    r.subjectCard = subjectCard;
    r.subjectZone = subjectZone;
    r.turn = state_.turnNumber;
    r.phase = phaseShortName(state_.phase);

    const char* movieStr =
        (state_.setupConfig.movie == 2) ? "Predator 2 (LA)" : "Predator 1 (Val Verde)";
    const char* modeStr = state_.setupConfig.playAsPredators ? "Predators" : "Humans";
    r.scenarioSummary = std::string(movieStr) + " — " + modeStr + " (" +
                        std::to_string(std::max(1, state_.playerCount)) + "P)";

    if (!title.empty()) {
        r.title = title;
    } else if (!subjectCard.empty()) {
        r.title = "Issue with " + subjectCard +
                  (subjectZone.empty() ? "" : (" in " + subjectZone));
    } else {
        r.title = "Gameplay issue on Turn " + std::to_string(r.turn) + " (" + r.phase + ")";
    }

    r.description = description;
    r.tacticalSnapshot = buildTacticalSnapshot();
    r.boardSnapshot = buildBoardSnapshot();
    r.recentMoves = formatRecentMoves(recentMoveCount);

    bugReports_.push_back(r);
    saveBugReportsToDisk();

    // Cross-reference the bug report in the live movement log so both logs link via BUG-XXXX
    recordMove("BUG_REPORT",
               "[" + r.reportCode + "] " +
                   (r.subjectCard.empty() ? r.title : (r.subjectCard + " — " + r.title)),
               1, r.subjectZone.empty() ? "table" : r.subjectZone, "bug_reports.md",
               static_cast<int>(bugReports_.size()), true,
               "Recorded " + r.reportCode + " (" + r.category + ", " + r.severity + ")");

    return bugReports_.back();
}

bool Table::updateBugReportStatus(int reportId, const std::string& newStatus) {
    for (auto& r : bugReports_) {
        if (r.id == reportId) {
            r.status = newStatus;
            saveBugReportsToDisk();
            return true;
        }
    }
    return false;
}

bool Table::deleteBugReport(int reportId) {
    for (auto it = bugReports_.begin(); it != bugReports_.end(); ++it) {
        if (it->id == reportId) {
            bugReports_.erase(it);
            saveBugReportsToDisk();
            return true;
        }
    }
    return false;
}

void Table::reflowZone(const std::string& zoneId) {
    int pIdx = 0;
    std::string_view role;
    const bool parsed = parsePlayerZone(zoneId, pIdx, role);

    const Zone* z = layout_.zone(zoneId);
    const float y = z ? z->rect.center().y : ((parsed && role == "played") ? 1232.f : 1502.f);
    const float leftX = z ? (z->rect.x + 110.f) : 595.f;
    const float rightX = z ? (z->rect.right() - 110.f) : 1775.f;

    // Split any multi-card piles in Hand or Played Area into individual 1-card face-up piles
    std::vector<CardInst> splitExtras;
    for (auto& p : state_.piles) {
        if (p.zone != zoneId || p.cards.empty()) continue;
        p.cards[0].faceUp = true;
        p.cards[0].attached = false;
        for (size_t i = 1; i < p.cards.size(); ++i) {
            CardInst c = p.cards[i];
            c.faceUp = true;
            c.attached = false;
            splitExtras.push_back(c);
        }
        if (p.cards.size() > 1) {
            p.cards.resize(1);
        }
    }
    for (const auto& c : splitExtras) {
        Pile np;
        np.id = state_.nextPileId++;
        np.pos = {leftX, y};
        np.zone = zoneId;
        np.cards = {c};
        state_.piles.push_back(std::move(np));
        (void)visual(state_.piles.back().id);
    }

    // Remove empty piles
    for (auto it = state_.piles.begin(); it != state_.piles.end();) {
        if (it->cards.empty()) {
            visuals_.erase(it->id);
            it = state_.piles.erase(it);
        } else {
            ++it;
        }
    }

    std::uint32_t immunityUid = 0;
    if (parsed && role == "played" && pIdx >= 0 &&
        pIdx < static_cast<int>(state_.players.size())) {
        immunityUid = state_.players[static_cast<size_t>(pIdx)].activeImmunityCardUid;
    }

    std::vector<Pile*> zPiles;
    for (auto& p : state_.piles) {
        if (p.zone != zoneId || p.cards.empty()) continue;
        if (immunityUid != 0 && p.top().uid == immunityUid) {
            const Zone* avZ = layout_.zone(playerZoneId(pIdx, "avatar"));
            const Vec2 avPos = avZ ? avZ->rect.center() : Vec2{140.f, y};
            p.pos = avPos + Vec2{46.f, -18.f};
            p.quarterTurns = 0;
            p.cards.back().faceUp = true;
            p.label = "IMMUNE";
            continue;
        }
        zPiles.push_back(&p);
    }
    std::sort(zPiles.begin(), zPiles.end(),
              [](const Pile* a, const Pile* b) { return a->pos.x < b->pos.x; });

    const int n = static_cast<int>(zPiles.size());
    if (n == 0) return;

    const float maxSpan = std::max(100.f, rightX - leftX);
    const float stepX = (n <= 1) ? 0.f : std::min(205.f, maxSpan / static_cast<float>(n - 1));
    const float totalW = stepX * static_cast<float>(n - 1);
    const float startX = (leftX + rightX - totalW) * 0.5f;

    for (int i = 0; i < n; ++i) {
        zPiles[static_cast<size_t>(i)]->pos = {startX + static_cast<float>(i) * stepX, y};
        zPiles[static_cast<size_t>(i)]->quarterTurns = 0;
        zPiles[static_cast<size_t>(i)]->cards.back().faceUp = true;
        zPiles[static_cast<size_t>(i)]->label.clear();
    }
}

void Table::reflowAllPlayerAreas() {
    const int n = std::max(1, state_.playerCount);
    for (int p = 0; p < n; ++p) {
        reflowZone(playerZoneId(p, "hand"));
        reflowZone(playerZoneId(p, "played"));
    }
}

void Table::syncStateFromZones(std::string* deltaOut) {
    const int prevRecruit = state_.recruitPool;
    const int prevAttack = state_.attackPool;
    const int prevStrikes = state_.strikeDamage;
    const int prevMarks = state_.marksCount;

    const int nPlayers = std::max(1, state_.playerCount);
    if (static_cast<int>(state_.players.size()) < nPlayers) {
        state_.players.resize(static_cast<size_t>(nPlayers));
    }
    state_.activePlayer = std::clamp(state_.activePlayer, 0, nPlayers - 1);

    // Migrate any legacy `player_*` zone ids onto `p0_*` cleanly
    for (auto& p : state_.piles) {
        if (p.zone.rfind("player_", 0) == 0) {
            p.zone = playerZoneId(0, p.zone.substr(7));
        }
    }

    // 1. Consolidate multi-pile stack slots in EVERY player's board area & compute per-player Strikes/Marks
    for (int pIdx = 0; pIdx < nPlayers; ++pIdx) {
        PlayerState& ps = state_.players[static_cast<size_t>(pIdx)];
        for (const char* role : {"avatar", "strikes", "marks", "deck", "discard"}) {
            const std::string slotId = playerZoneId(pIdx, role);
            Pile* primary = nullptr;
            for (size_t i = 0; i < state_.piles.size();) {
                Pile& p = state_.piles[i];
                if (p.zone != slotId || p.cards.empty()) {
                    ++i;
                    continue;
                }
                if (!primary) {
                    primary = &p;
                    if (const Zone* z = layout_.zone(slotId)) primary->pos = z->rect.center();
                    primary->quarterTurns = 0;
                    ++i;
                } else {
                    primary->cards.insert(primary->cards.end(), p.cards.begin(), p.cards.end());
                    visuals_.erase(p.id);
                    state_.piles.erase(state_.piles.begin() + static_cast<std::ptrdiff_t>(i));
                    primary = findZonePile(slotId);
                }
            }
            if (primary) {
                const std::string_view rView(role);
                const bool wantFaceUp =
                    (rView == "avatar") ? ps.alive : (rView != "deck");
                for (auto& c : primary->cards) {
                    c.faceUp = wantFaceUp;
                    c.attached = false;
                }
                if (rView == "avatar" && !primary->cards.empty()) {
                    ps.avatarDefId = primary->top().def;
                }
            }
        }

        // Compute Strike damage for player `pIdx`
        int zoneStrikeDmg = 0;
        if (const Pile* sp = findZonePile(playerZoneId(pIdx, "strikes"))) {
            for (const auto& c : sp->cards) {
                const CardDef& d = catalog_.card(c.def);
                if (c.def == 184) continue; // Close Call = 0 dmg
                const int dmg = (d.attack >= 0) ? d.attack : 1;
                zoneStrikeDmg += dmg;
            }
        }
        ps.strikeDamage = std::max(0, zoneStrikeDmg + ps.manualStrikeOffset);

        // Compute Marks count for player `pIdx`
        int zoneMarks = 0;
        if (const Pile* mp = findZonePile(playerZoneId(pIdx, "marks"))) {
            zoneMarks = static_cast<int>(mp->cards.size());
        }
        ps.marksCount = std::max(0, zoneMarks + ps.manualMarksOffset);
    }

    // Mirror active player's stats onto TableState convenience fields
    {
        const PlayerState& ap = state_.players[static_cast<size_t>(state_.activePlayer)];
        state_.avatarHealth = ap.health;
        state_.avatarDefense = ap.defense;
        state_.strikeDamage = ap.strikeDamage;
        state_.marksCount = ap.marksCount;
        state_.manualStrikeOffset = ap.manualStrikeOffset;
        state_.manualMarksOffset = ap.manualMarksOffset;
        state_.nextHandSizePenalty = ap.nextHandSizePenalty;
        state_.cannotDieTurnsRemaining = ap.cannotDieTurnsRemaining;
    }

    // 2. Sync Active Player's Turn Cards (Hand + Played Area + Coordinated virtual copies)
    const std::string activePlayedZid = activePlayerZone("played");
    const std::vector<CardInst> turnCards = collectActiveTurnCards();

    state_.playedClasses = state_.coordinatedClasses;
    state_.playedTeams = state_.coordinatedTeams;
    state_.playedCharacters = state_.coordinatedCharacters;
    for (const auto& p : state_.piles) {
        if (p.zone != activePlayedZid) continue;
        for (const auto& c : p.cards) {
            const CardDef& d = catalog_.card(c.def);
            if (!d.classType.empty() && !containsStr(state_.playedClasses, d.classType)) {
                state_.playedClasses.push_back(d.classType);
            }
            if (!d.team.empty() && !containsStr(state_.playedTeams, d.team)) {
                state_.playedTeams.push_back(d.team);
            }
            if (isBarracksCharacterDef(c.def) && !d.subtitle.empty() &&
                !containsStr(state_.playedCharacters, d.subtitle)) {
                state_.playedCharacters.push_back(d.subtitle);
            }
        }
    }

    state_.roundHandClasses.clear();
    state_.roundHandTeams.clear();
    for (const auto& c : turnCards) {
        const CardDef& d = catalog_.card(c.def);
        if (!d.classType.empty() && !containsStr(state_.roundHandClasses, d.classType)) {
            state_.roundHandClasses.push_back(d.classType);
        }
        if (!d.team.empty() && !containsStr(state_.roundHandTeams, d.team)) {
            state_.roundHandTeams.push_back(d.team);
        }
        if (state_.phase == TurnPhase::ActionPhase) {
            if (!d.classType.empty() && !containsStr(state_.playedClasses, d.classType)) {
                state_.playedClasses.push_back(d.classType);
            }
            if (!d.team.empty() && !containsStr(state_.playedTeams, d.team)) {
                state_.playedTeams.push_back(d.team);
            }
            if (isBarracksCharacterDef(c.def) && !d.subtitle.empty() &&
                !containsStr(state_.playedCharacters, d.subtitle)) {
                state_.playedCharacters.push_back(d.subtitle);
            }
        }
    }

    // In ActionPhase, all cards in Hand + Played Area + Coordinated virtual copies automatically
    // contribute their Recruit, Attack, and passive turn modifiers!
    if (state_.phase == TurnPhase::ActionPhase) {
        for (const auto& c : turnCards) {
            const CardDef& d = catalog_.card(c.def);
            int effRec = 0;
            int effAtk = 0;
            effectiveCardStats(c, &effRec, &effAtk);

            const int nOtherChars =
                isBarracksCharacterDef(c.def) ? countOtherBarracksChars(d.subtitle) : 0;
            const int coordCount = std::max(0, state_.coordinatedCountThisTurn);

            // Passive turn modifiers
            int effAvoid = 0;
            if (c.def == 4 && countTurnClass("Survival") >= 2) {
                effAvoid = 1;              // Real Nasty Habit: [Survival] Avoid 1 Strike
            } else if (c.def == 65) {
                effAvoid = coordCount;     // In the Shadows: Call For Backup Avoid 1 Strike per Coordinate
            }

            int effFreeScan = 0;
            if (c.def == 2) {
                effFreeScan = nOtherChars; // Dug In Like An Alabama Tick: Brothers in Arms Free Scan (0..3)
            } else if (c.def == 67) {
                effFreeScan = coordCount;  // On the Case: Call For Backup Free Scan per Coordinate
            } else if (c.def == 85 && countTurnClass("Intel") >= 2) {
                effFreeScan = 1;           // Stalking the Prey: [Intel] Free Scan
            }

            const int effCombatPen = (c.def == 9) ? 1 : 0; // Get to the Choppa!: -1 Attack to Combat Zone Enemies
            const int effDrawKill = (c.def == 15) ? 1 : 0; // Contained Anomaly: Draw 1 card per Enemy killed

            const int prevR = state_.accountedCardRecruit[c.uid];
            const int prevA = state_.accountedCardAttack[c.uid];
            const int prevAv = state_.accountedCardAvoid[c.uid];
            const int prevFs = state_.accountedCardFreeScan[c.uid];
            const int prevCp = state_.accountedCardCombatPenalty[c.uid];
            const int prevDk = state_.accountedCardDrawOnKill[c.uid];

            state_.recruitPool = std::max(0, state_.recruitPool + (effRec - prevR));
            state_.attackPool = std::max(0, state_.attackPool + (effAtk - prevA));
            state_.avoidStrikesAvailable =
                std::max(0, state_.avoidStrikesAvailable + (effAvoid - prevAv));
            state_.freeScansAvailable =
                std::max(0, state_.freeScansAvailable + (effFreeScan - prevFs));
            state_.combatZoneAttackPenalty =
                std::max(0, state_.combatZoneAttackPenalty + (effCombatPen - prevCp));
            state_.drawCardsOnKill =
                std::max(0, state_.drawCardsOnKill + (effDrawKill - prevDk));

            state_.accountedCardRecruit[c.uid] = effRec;
            state_.accountedCardAttack[c.uid] = effAtk;
            state_.accountedCardAvoid[c.uid] = effAvoid;
            state_.accountedCardFreeScan[c.uid] = effFreeScan;
            state_.accountedCardCombatPenalty[c.uid] = effCombatPen;
            state_.accountedCardDrawOnKill[c.uid] = effDrawKill;

            if (std::find(state_.accountedPlayedUids.begin(), state_.accountedPlayedUids.end(),
                          c.uid) == state_.accountedPlayedUids.end()) {
                state_.accountedPlayedUids.push_back(c.uid);
            }
        }

        // Un-credit any card removed from Hand/Played (e.g. discarded from hand) unless locked
        for (auto it = state_.accountedPlayedUids.begin();
             it != state_.accountedPlayedUids.end();) {
            const std::uint32_t uid = *it;
            const bool inTurn = std::any_of(
                turnCards.begin(), turnCards.end(),
                [uid](const CardInst& c) { return c.uid == uid; });
            const bool isLocked =
                std::find(state_.lockedAccountedUids.begin(), state_.lockedAccountedUids.end(),
                          uid) != state_.lockedAccountedUids.end();
            if (!inTurn && !isLocked) {
                state_.recruitPool =
                    std::max(0, state_.recruitPool - state_.accountedCardRecruit[uid]);
                state_.attackPool =
                    std::max(0, state_.attackPool - state_.accountedCardAttack[uid]);
                state_.avoidStrikesAvailable =
                    std::max(0, state_.avoidStrikesAvailable - state_.accountedCardAvoid[uid]);
                state_.freeScansAvailable =
                    std::max(0, state_.freeScansAvailable - state_.accountedCardFreeScan[uid]);
                state_.combatZoneAttackPenalty = std::max(
                    0, state_.combatZoneAttackPenalty - state_.accountedCardCombatPenalty[uid]);
                state_.drawCardsOnKill =
                    std::max(0, state_.drawCardsOnKill - state_.accountedCardDrawOnKill[uid]);

                state_.accountedCardRecruit.erase(uid);
                state_.accountedCardAttack.erase(uid);
                state_.accountedCardAvoid.erase(uid);
                state_.accountedCardFreeScan.erase(uid);
                state_.accountedCardCombatPenalty.erase(uid);
                state_.accountedCardDrawOnKill.erase(uid);
                it = state_.accountedPlayedUids.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (deltaOut) {
        std::ostringstream oss;
        if (state_.recruitPool != prevRecruit)
            oss << "Recruit " << prevRecruit << "->" << state_.recruitPool << " ";
        if (state_.attackPool != prevAttack)
            oss << "Attack " << prevAttack << "->" << state_.attackPool << " ";
        if (state_.strikeDamage != prevStrikes)
            oss << "Strikes " << prevStrikes << "->" << state_.strikeDamage << " ";
        if (state_.marksCount != prevMarks)
            oss << "Marks " << prevMarks << "->" << state_.marksCount << " ";
        *deltaOut = oss.str();
        if (deltaOut->empty()) *deltaOut = "No stat change";
    }
}

void Table::applyDropWithLog(PileId draggedId, const DropTarget& t,
                             const std::string& srcZoneBefore) {
    const Pile* dragged = find(draggedId);
    if (!dragged || dragged->cards.empty()) return;

    const int cardCount = static_cast<int>(dragged->cards.size());
    const CardDef& topDef = catalog_.card(dragged->top().def);
    std::string cardSummary = topDef.title.empty() ? topDef.name : topDef.title;
    if (cardCount > 1) {
        cardSummary += " (+" + std::to_string(cardCount - 1) + " more)";
    }

    std::string dstZone;
    if (t.kind == DropTarget::Kind::Pile) {
        if (const Pile* targetPile = find(t.pile)) dstZone = targetPile->zone;
    } else if (t.zone) {
        dstZone = t.zone->id;
    }

    applyDrop(draggedId, t);

    if (isHandOrPlayedZone(srcZoneBefore)) reflowZone(srcZoneBefore);
    if (!dstZone.empty() && dstZone != srcZoneBefore && isHandOrPlayedZone(dstZone)) {
        reflowZone(dstZone);
    }

    std::string delta;
    syncStateFromZones(&delta);
    refillHQ();

    // If a Strike card was manually dropped onto a player's Strikes zone and their HP dropped to <= 0, eliminate them
    int dstP = -1;
    std::string_view dstRole;
    if (parsePlayerZone(dstZone, dstP, dstRole) && dstRole == "strikes" &&
        dstP >= 0 && dstP < static_cast<int>(state_.players.size())) {
        const PlayerState& ps = state_.players[static_cast<size_t>(dstP)];
        if (ps.alive && ps.strikeDamage >= ps.health && ps.cannotDieTurnsRemaining <= 0) {
            eliminatePlayer(dstP, delta);
            syncStateFromZones();
            state_.lastActionLog = delta;
        }
    }

    int destStackCount = cardCount;
    bool faceUp = true;
    if (!dstZone.empty()) {
        if (const Pile* zp = findZonePile(dstZone)) {
            destStackCount = static_cast<int>(zp->cards.size());
            faceUp = zp->top().faceUp;
        }
    } else if (const Pile* pAfter = find(t.kind == DropTarget::Kind::Pile ? t.pile : draggedId)) {
        destStackCount = static_cast<int>(pAfter->cards.size());
        if (!pAfter->cards.empty()) faceUp = pAfter->top().faceUp;
    }

    const std::string actionType =
        (t.kind == DropTarget::Kind::Pile) ? "STACK_MERGE" : "DRAG_DROP";
    recordMove(actionType, cardSummary, cardCount, srcZoneBefore, dstZone, destStackCount, faceUp,
               delta);
}

void Table::applyMultiDrop(const std::vector<PileId>& draggedPiles,
                           const DropTarget& primaryTarget,
                           const std::vector<std::string>& srcZonesBefore) {
    if (draggedPiles.empty()) return;
    if (draggedPiles.size() == 1) {
        applyDropWithLog(draggedPiles[0], primaryTarget,
                         srcZonesBefore.empty() ? "" : srcZonesBefore[0]);
        return;
    }

    int totalCards = 0;
    std::string firstCardName;
    for (PileId pid : draggedPiles) {
        if (const Pile* p = find(pid); p && !p->cards.empty()) {
            totalCards += static_cast<int>(p->cards.size());
            if (firstCardName.empty()) {
                const CardDef& d = catalog_.card(p->top().def);
                firstCardName = d.title.empty() ? d.name : d.title;
            }
        }
    }
    const std::string summary =
        firstCardName + " & " + std::to_string(draggedPiles.size() - 1) + " other pile(s)";
    const std::string fromZone = srcZonesBefore.empty() ? "multi" : srcZonesBefore[0];
    std::string dstZone;

    if (primaryTarget.kind == DropTarget::Kind::Pile) {
        PileId dstPileId = primaryTarget.pile;
        if (const Pile* dp = find(dstPileId)) dstZone = dp->zone;
        for (PileId pid : draggedPiles) {
            if (pid != dstPileId && find(pid)) mergeOnto(pid, dstPileId);
        }
    } else if (primaryTarget.kind == DropTarget::Kind::Slot && primaryTarget.zone) {
        dstZone = primaryTarget.zone->id;
        PileId baseId = draggedPiles[0];
        place(baseId, primaryTarget.pos, dstZone, primaryTarget.quarterTurns);
        for (size_t i = 1; i < draggedPiles.size(); ++i) {
            if (find(draggedPiles[i])) mergeOnto(draggedPiles[i], baseId);
        }
    } else if (primaryTarget.kind == DropTarget::Kind::Area && primaryTarget.zone) {
        dstZone = primaryTarget.zone->id;
        for (PileId pid : draggedPiles) {
            if (Pile* p = find(pid)) {
                place(pid, {p->pos.x, primaryTarget.zone->rect.center().y}, dstZone, 0);
            }
        }
    } else {
        for (PileId pid : draggedPiles) {
            if (Pile* p = find(pid)) place(pid, p->pos, "", -1);
        }
    }

    reflowAllPlayerAreas();

    std::string delta;
    syncStateFromZones(&delta);
    refillHQ();

    // If a Strike card was manually dropped onto a player's Strikes zone and their HP dropped to <= 0, eliminate them
    int dstP = -1;
    std::string_view dstRole;
    if (parsePlayerZone(dstZone, dstP, dstRole) && dstRole == "strikes" &&
        dstP >= 0 && dstP < static_cast<int>(state_.players.size())) {
        const PlayerState& ps = state_.players[static_cast<size_t>(dstP)];
        if (ps.alive && ps.strikeDamage >= ps.health && ps.cannotDieTurnsRemaining <= 0) {
            eliminatePlayer(dstP, delta);
            syncStateFromZones();
            state_.lastActionLog = delta;
        }
    }

    int destStackCount = totalCards;
    bool faceUp = true;
    if (!dstZone.empty()) {
        if (const Pile* zp = findZonePile(dstZone)) {
            destStackCount = static_cast<int>(zp->cards.size());
            faceUp = zp->top().faceUp;
        }
    }

    recordMove("MULTI_DROP", summary, totalCards, fromZone, dstZone, destStackCount, faceUp, delta);
}

PileId Table::gatherPiles(const std::vector<PileId>& pileIds, Vec2 targetPos) {
    if (pileIds.empty()) return kNoPile;
    PileId baseId = kNoPile;
    int totalCards = 0;
    std::string fromZone;

    for (PileId pid : pileIds) {
        Pile* p = find(pid);
        if (!p || p->cards.empty()) continue;
        totalCards += static_cast<int>(p->cards.size());
        if (baseId == kNoPile) {
            baseId = pid;
            fromZone = p->zone;
            p->pos = targetPos;
        } else {
            mergeOnto(pid, baseId);
        }
    }

    if (baseId != kNoPile) {
        reflowAllPlayerAreas();
        std::string delta;
        syncStateFromZones(&delta);
        if (const Pile* bp = find(baseId); bp && !bp->cards.empty()) {
            const CardDef& d = catalog_.card(bp->top().def);
            recordMove("STACK_SELECTED", d.name + " Stack", totalCards, fromZone, bp->zone,
                       static_cast<int>(bp->cards.size()), bp->top().faceUp, delta);
        }
    }
    return baseId;
}

void Table::refillHQ() {
    Pile* barracks = findZonePile("barracks");
    if (!barracks || barracks->cards.empty()) return;

    for (int i = 0; i < 5; ++i) {
        const std::string zid = "hq_" + std::to_string(i);
        if (findZonePile(zid) == nullptr) {
            Pile* curBarracks = findZonePile("barracks");
            if (!curBarracks || curBarracks->cards.empty()) break;
            const Vec2 barracksPos = curBarracks->pos;
            CardInst c = curBarracks->cards.back();
            curBarracks->cards.pop_back();
            c.faceUp = true;
            c.attached = false;

            Vec2 pos{600.f + static_cast<float>(i) * 190.f, 880.f};
            if (const Zone* z = layout_.zone(zid)) pos = z->rect.center();
            Pile np;
            np.id = state_.nextPileId++;
            np.pos = pos;
            np.zone = zid;
            np.cards = {c};
            state_.piles.push_back(std::move(np));
            PileVisual& pv = visual(state_.piles.back().id);
            pv.pos = pos;
            pv.flipT = 0.f;
            queueCardFlight(c, barracksPos, pos, false, true);
        }
    }
}

void Table::layoutPlayerRow(const std::string& zoneId, float /*startX*/, float /*y*/, float /*stepX*/,
                            const std::string& /*firstLabel*/) {
    reflowZone(zoneId);
}

void Table::advanceObjectiveStack() {
    if (Pile* obj = findZonePile("objectives")) {
        if (obj->cards.size() > 1) {
            // Top card is back(); move completed objective to bottom (front)
            CardInst completed = obj->cards.back();
            obj->cards.pop_back();
            obj->cards.insert(obj->cards.begin(), completed);
            visual(obj->id).flipT = 0.f;
        }
    }
    state_.currentObjective = std::min(3, state_.currentObjective + 1);
}

void Table::attachCardToWildsSlot(int slotIdx, CardInst card) {
    slotIdx = std::clamp(slotIdx, 0, 4);
    card.faceUp = true;
    card.attached = true;

    const std::string zid = "wilds_" + std::to_string(slotIdx);
    if (Pile* dst = findZonePile(zid)) {
        // Attached cards always sit at the bottom of the location stack underneath regular cards
        auto it = std::find_if(dst->cards.begin(), dst->cards.end(),
                               [](const CardInst& existing) { return !existing.attached; });
        dst->cards.insert(it, card);
        visual(dst->id).flipT = 0.f;
        return;
    }

    Vec2 pos{461.f + static_cast<float>(slotIdx) * 210.f + 105.f, 234.f};
    if (const Zone* z = layout_.zone(zid)) pos = z->rect.center();

    Pile np;
    np.id = state_.nextPileId++;
    np.pos = pos;
    np.zone = zid;
    np.cards = {card};
    state_.piles.push_back(std::move(np));
    visual(state_.piles.back().id).flipT = 0.f;
}

void Table::revealAndAttachGuerrillaCamp(CardInst camp, std::string& logOut) {
    // Guerrilla Camp Reveal: "Attach to the River. Then move it one space to the right for each discarded Event."
    const int discardedEvents = countDiscardedEvents();
    const int targetSlot = std::clamp(2 + discardedEvents, 2, 4); // 2=River, 3=Graveyard, 4=Underground
    attachCardToWildsSlot(targetSlot, camp);

    if (discardedEvents <= 0) {
        logOut += " [Reveal — Attach: Guerrilla Camp attached to River (0 discarded Events)!]";
    } else {
        logOut += " [Reveal — Attach: Guerrilla Camp attached to River -> moved +" +
                  std::to_string(targetSlot - 2) + " right for " +
                  std::to_string(discardedEvents) + " discarded Event(s) -> attached to " +
                  std::string(wildsName(targetSlot)) + "!]";
    }
}

void Table::revealThreeHundredCityBlocks(CardInst card115, std::uint32_t seed,
                                         std::string& logOut) {
    // Three Hundred City Blocks (id 115):
    // "Reveal: Attach this to the Final Enemy. If you can't, add an Enemy card to the Wilds, then shuffle this into the Enemy deck."
    Pile* finalEnemyPile = nullptr;
    for (auto& p : state_.piles) {
        if ((p.zone == "combat" || wildsIndexFromZone(p.zone) >= 0) && !p.cards.empty() &&
            p.top().faceUp && p.top().def == 121) {
            finalEnemyPile = &p;
            break;
        }
    }

    if (finalEnemyPile) {
        card115.faceUp = true;
        card115.attached = true;
        finalEnemyPile->cards.insert(finalEnemyPile->cards.begin(), card115);
        state_.threeHundredCityBlocksAttachedToFinalEnemy = true;
        visual(finalEnemyPile->id).flipT = 0.f;
        logOut +=
            " [Reveal — Three Hundred City Blocks: Attached to Final Enemy (One Ugly Mother...)!]";
        return;
    }

    // Final Enemy not face-up on board -> add an Enemy card to the Wilds, then shuffle 115 into the Enemy deck
    Pile* prey = findZonePile("prey");
    if (prey && !prey->cards.empty()) {
        const Vec2 preyPos = prey->pos;
        CardInst drawnEnemy = prey->cards.back();
        prey->cards.pop_back();
        drawnEnemy.faceUp = false;
        drawnEnemy.attached = false;
        logOut += " [Reveal — Three Hundred City Blocks: Final Enemy not in play -> added 1 Enemy to The Wilds!]";
        queueCardFlight(drawnEnemy, preyPos, zoneCenterPos("wilds_4", {1400.f, 234.f}), false,
                        false);
        pushCardIntoWildsSlot(4, drawnEnemy, seed + 19u, logOut);
    }

    card115.faceUp = false;
    card115.attached = false;
    Pile& preyDeck = ensureZonePile("prey", {1620.f, 220.f}, "");
    preyDeck.cards.push_back(card115);
    shuffle(preyDeck.id, seed + 83u);
    queueCardFlight(card115, zoneCenterPos("wilds_0", {566.f, 234.f}), preyDeck.pos, true, false);
    logOut += " [Shuffled Three Hundred City Blocks back into the Enemy Deck!]";
}

void Table::moveCardToCombatZone(CardInst card, std::uint32_t seed, std::string& logOut) {
    const bool wasFaceUp = card.faceUp;
    card.faceUp = true;
    const CardDef& d = catalog_.card(card.def);
    const Vec2 hillsPos = zoneCenterPos("wilds_0", {566.f, 234.f});

    // If an Event or Hazard is pushed into the Combat Zone, it resolves its effect immediately
    if (isEventDef(card.def)) {
        card.attached = false;
        Pile& disc = ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
        disc.cards.push_back(card);
        queueCardFlight(card, hillsPos, disc.pos, wasFaceUp, true);
        logOut += " [Event revealed in Combat Zone -> " + d.name + "]";
        resolveObjectiveEvent(seed, logOut);
        return;
    }
    if (isHazardDef(card.def)) {
        card.attached = false;
        logOut += " [Hazard revealed in Combat Zone!]";
        resolveLocationHazard(card, seed, logOut);
        return;
    }
    if (card.def == 102) {
        if (!card.attached) {
            // Face-down Guerrilla Camp pushed off Hills reveals and immediately attaches to River (+ discarded Events)!
            logOut += " [Guerrilla Camp revealed entering Combat Zone!]";
            revealAndAttachGuerrillaCamp(card, logOut);
        } else {
            // Already-attached face-up Guerrilla Camp moved left into Combat Zone -> Discarded Events & Objective 1 Complete!
            card.attached = false;
            Pile& disc =
                ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
            disc.cards.push_back(card);
            queueCardFlight(card, hillsPos, disc.pos, true, true);
            if (state_.currentObjective == 1) {
                advanceObjectiveStack();
                logOut += " [Guerrilla Camp entered Combat Zone -> Discarded Events & OBJECTIVE 1 COMPLETE!]";
            } else {
                logOut += " [Guerrilla Camp entered Combat Zone -> Discarded Events!]";
            }
        }
        return;
    }
    if (card.def == 107) {
        // Defensive Position moves to Operations
        card.attached = false;
        Pile& ops = ensureZonePile("testing", {1300.f, 548.f}, "Operations");
        ops.cards.push_back(card);
        queueCardFlight(card, hillsPos, ops.pos, wasFaceUp, true);
        logOut += " [Defensive Position moved to Operations!]";
        return;
    }
    if (card.def == 115) {
        card.attached = false;
        revealThreeHundredCityBlocks(card, seed, logOut);
        return;
    }

    card.attached = false;

    // Collect existing Combat Zone piles and slide them to the right (Rulebook p.7)
    const Zone* cz = layout_.zone("combat");
    const float baseY = cz ? cz->rect.center().y : 548.f;
    const float startX = cz ? (cz->rect.x + 95.f) : 620.f;

    std::vector<Pile*> czPiles;
    for (auto& p : state_.piles) {
        if (p.zone == "combat" && !p.cards.empty()) czPiles.push_back(&p);
    }
    std::sort(czPiles.begin(), czPiles.end(),
              [](const Pile* a, const Pile* b) { return a->pos.x < b->pos.x; });

    const float stepX = czPiles.size() >= 3 ? 115.f : 155.f;
    for (size_t i = 0; i < czPiles.size(); ++i) {
        czPiles[i]->pos = {startX + static_cast<float>(i + 1) * stepX, baseY};
    }

    Pile np;
    np.id = state_.nextPileId++;
    np.pos = {startX, baseY};
    np.zone = "combat";
    if (card.def == 121 && state_.threeHundredCityBlocksAttachedToFinalEnemy) {
        CardInst att115{state_.nextUid++, 115, true, true};
        np.cards = {att115, card};
    } else {
        np.cards = {card};
    }
    state_.piles.push_back(std::move(np));
    visual(state_.piles.back().id).flipT = 0.f;
    queueCardFlight(card, hillsPos, {startX, baseY}, wasFaceUp, true);

    logOut += " [" + d.name + " entered the Combat Zone!]";
}

void Table::pushCardIntoWildsSlot(int slotIdx, CardInst card, std::uint32_t seed,
                                  std::string& logOut) {
    if (slotIdx < 0) {
        moveCardToCombatZone(card, seed, logOut);
        return;
    }

    card.attached = false;
    const std::string zid = "wilds_" + std::to_string(slotIdx);
    const Vec2 dstPos = zoneCenterPos(
        zid, {461.f + static_cast<float>(slotIdx) * 210.f + 105.f, 234.f});
    const Vec2 srcPos =
        (slotIdx < 4) ? zoneCenterPos("wilds_" + std::to_string(slotIdx + 1), dstPos + Vec2{210.f, 0.f})
                      : zoneCenterPos("prey", {1620.f, 220.f});

    // Attached cards are immune to regular Wilds movement: only push an unattached top card!
    if (Pile* existing = findZonePile(zid);
        existing && !existing->cards.empty() && !existing->top().attached) {
        CardInst pushed = existing->cards.back();
        existing->cards.pop_back();
        if (existing->cards.empty()) {
            const int eIdx = indexOf(existing->id);
            if (eIdx >= 0) {
                visuals_.erase(existing->id);
                state_.piles.erase(state_.piles.begin() + eIdx);
            }
        }
        pushCardIntoWildsSlot(slotIdx - 1, pushed, seed, logOut);
    }

    // Re-fetch destination pile after recursion: if an attached card rests here, stack on top of it
    PileId landedPileId = kNoPile;
    if (Pile* target = findZonePile(zid)) {
        target->cards.push_back(card);
        landedPileId = target->id;
        (void)visual(landedPileId);
    } else {
        Pile np;
        np.id = state_.nextPileId++;
        np.pos = dstPos;
        np.zone = zid;
        np.cards = {card};
        state_.piles.push_back(std::move(np));
        landedPileId = state_.piles.back().id;
        (void)visual(landedPileId);
    }
    queueCardFlight(card, srcPos, dstPos, card.faceUp, card.faceUp);

    // Objective 2 attached Defensive Position on Hills automatically scans hidden cards entering Hills
    bool hasDefPosOnHills = state_.defensivePositionAttachedToHills;
    if (!hasDefPosOnHills && slotIdx == 0) {
        if (const Pile* hp = find(landedPileId)) {
            for (const auto& c : hp->cards) {
                if (c.def == 107 && c.attached && c.faceUp) {
                    hasDefPosOnHills = true;
                    break;
                }
            }
        }
    }
    if (slotIdx == 0 && hasDefPosOnHills && !card.faceUp) {
        if (Pile* hp = find(landedPileId); hp && !hp->cards.empty() && !hp->top().attached) {
            hp->cards.back().faceUp = true;
            visual(landedPileId).flipT = 0.f;
            logOut += " [Defensive Position auto-scanned Hills!]";
            onCardRevealedInWilds(landedPileId, 0, seed, logOut);
        }
    }
}

void Table::resolveObjectiveEvent(std::uint32_t seed, std::string& logOut) {
    if (state_.currentObjective == 1) {
        // Objective 1 Event: "Better Equipped Every Day" — Move face-up Guerrilla Camp 1 space right
        bool foundCamp = false;
        for (int s = 4; s >= 0; --s) {
            const std::string zid = "wilds_" + std::to_string(s);
            Pile* wp = findZonePile(zid);
            if (!wp) continue;
            for (size_t ci = 0; ci < wp->cards.size(); ++ci) {
                if (wp->cards[ci].def == 102 && wp->cards[ci].faceUp) {
                    foundCamp = true;
                    if (s < 4) {
                        const Vec2 fromPos = wp->pos;
                        CardInst camp = wp->cards[ci];
                        wp->cards.erase(wp->cards.begin() + static_cast<std::ptrdiff_t>(ci));
                        if (wp->cards.empty()) {
                            const int idx = indexOf(wp->id);
                            if (idx >= 0) {
                                visuals_.erase(wp->id);
                                state_.piles.erase(state_.piles.begin() + idx);
                            }
                        }
                        attachCardToWildsSlot(s + 1, camp);
                        queueCardFlight(camp, fromPos,
                                        zoneCenterPos("wilds_" + std::to_string(s + 1)), true,
                                        true);
                        logOut += " [Event: Guerrilla Camp moved right from " +
                                  std::string(wildsName(s)) + " to " +
                                  std::string(wildsName(s + 1)) + "!]";
                    } else {
                        wp->cards[ci].attached = true;
                        logOut += " [Event: Guerrilla Camp is already at Underground (rightmost space).]";
                    }
                    break;
                }
            }
            if (foundCamp) break;
        }
        if (!foundCamp) {
            logOut += " [Event: Guerrilla Camp not yet revealed in Wilds.]";
        }
    } else if (state_.currentObjective == 2) {
        // Objective 2 Event: "Right Out From Under Our Noses" — Top Enemy card into Hills face-down
        if (Pile* prey = findZonePile("prey"); prey && !prey->cards.empty()) {
            const Vec2 preyPos = prey->pos;
            CardInst c = prey->cards.back();
            prey->cards.pop_back();
            c.faceUp = false;
            c.attached = false;
            logOut += " [Event: Enemy ambushed into Hills face-down!]";
            pushCardIntoWildsSlot(0, c, seed, logOut);
            queueCardFlight(c, preyPos, zoneCenterPos("wilds_0"), false, false);
        }
    } else {
        // Objective 3 Event: "El Cazador Trofeo de los Hombres" — Draw a Strike for each Mark
        syncStateFromZones();
        const int marks = state_.marksCount;
        if (marks > 0) {
            logOut += " [Event: Drawing " + std::to_string(marks) + " Strike(s) for Marks!]";
            for (int i = 0; i < marks; ++i) {
                drawStrikeAgainstPlayer(state_.activePlayer,
                                        seed + static_cast<std::uint32_t>(i), logOut);
                if (state_.phase == TurnPhase::Defeat) break;
            }
        } else {
            logOut += " [Event: El Cazador Trofeo (0 Marks -> 0 Strikes).] ";
        }
    }
}

void Table::resolveLocationHazard(CardInst hazardCard, std::uint32_t seed, std::string& logOut) {
    hazardCard.faceUp = true;
    hazardCard.attached = false;
    const std::string marksZid = activePlayerZone("marks");
    Pile& marks = ensureZonePile(marksZid, {140.f, layout_.boardSize.y + 440.f}, "Marks");
    marks.cards.push_back(hazardCard);
    queueCardFlight(hazardCard, zoneCenterPos("wilds_0", {566.f, 234.f}), marks.pos, true, true);
    syncStateFromZones();

    logOut += " [HAZARD! Player " + std::to_string(state_.activePlayer + 1) + " Marked (" +
              std::to_string(state_.marksCount) + " total)!]";

    // Val Verdean Jungle Hazard 1-3: For each Mark you have, push leftmost unattached Wilds card to Combat Zone
    const int pushCount = state_.marksCount;
    for (int m = 0; m < pushCount; ++m) {
        bool pushedAny = false;
        for (int s = 0; s <= 4; ++s) {
            const std::string zid = "wilds_" + std::to_string(s);
            if (Pile* wp = findZonePile(zid);
                wp && !wp->cards.empty() && !wp->top().attached) {
                CardInst c = wp->cards.back();
                wp->cards.pop_back();
                if (wp->cards.empty()) {
                    const int idx = indexOf(wp->id);
                    if (idx >= 0) {
                        visuals_.erase(wp->id);
                        state_.piles.erase(state_.piles.begin() + idx);
                    }
                }
                moveCardToCombatZone(c, seed + static_cast<std::uint32_t>(m), logOut);
                pushedAny = true;
                break;
            }
        }
        if (!pushedAny) break;
    }
}

void Table::onCardRevealedInWilds(PileId wildsPileId, int slotIdx, std::uint32_t seed,
                                  std::string& logOut) {
    Pile* wp = find(wildsPileId);
    if (!wp || wp->cards.empty()) return;

    const Vec2 wildsPos = wp->pos;
    const CardInst revealed = wp->top();
    const CardDef& d = catalog_.card(revealed.def);

    // Pop ONLY the revealed top card so any attached bottom card (e.g. Guerrilla Camp) stays anchored!
    auto popWildsTopCard = [&]() {
        if (Pile* cur = find(wildsPileId)) {
            if (!cur->cards.empty()) cur->cards.pop_back();
            if (cur->cards.empty()) {
                const int idx = indexOf(wildsPileId);
                if (idx >= 0) {
                    visuals_.erase(wildsPileId);
                    state_.piles.erase(state_.piles.begin() + idx);
                }
            }
        }
    };

    if (isEventDef(revealed.def)) {
        popWildsTopCard();
        CardInst ev = revealed;
        ev.attached = false;
        Pile& disc = ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
        disc.cards.push_back(ev);
        queueCardFlight(ev, wildsPos, disc.pos, true, true);
        logOut += " Revealed EVENT!";
        resolveObjectiveEvent(seed, logOut);
        return;
    }

    if (isHazardDef(revealed.def)) {
        popWildsTopCard();
        resolveLocationHazard(revealed, seed, logOut);
        return;
    }

    // Objective 1 Goal: Guerrilla Camp (def 102) -> Attach to River + 1 space right per discarded Event
    if (revealed.def == 102) {
        popWildsTopCard();
        revealAndAttachGuerrillaCamp(revealed, logOut);
        return;
    }

    // Objective 2 Goal: Defensive Position (def 107) -> Move to Operations ("testing")
    if (revealed.def == 107) {
        popWildsTopCard();
        CardInst dp = revealed;
        dp.attached = false;
        Pile& ops = ensureZonePile("testing", {1300.f, 548.f}, "Operations");
        ops.cards.push_back(dp);
        queueCardFlight(dp, wildsPos, ops.pos, true, true);
        logOut += " Revealed Defensive Position -> moved to Operations!";
        return;
    }

    // Objective 3 Minor Goal: The Choppa! (def 114) -> Move to Combat Zone
    if (revealed.def == 114) {
        popWildsTopCard();
        moveCardToCombatZone(revealed, seed, logOut);
        return;
    }

    // Act 3 Special: Three Hundred City Blocks (def 115)
    if (revealed.def == 115) {
        popWildsTopCard();
        revealThreeHundredCityBlocks(revealed, seed, logOut);
        return;
    }

    // Young Blood: Over Here (def 207) -> "Reveal - Wilds: Draw a Strike then turn this Enemy face down."
    if (revealed.def == 207 && slotIdx >= 0) {
        logOut += " Revealed 'Over Here' (Young Blood): Draw a Strike and turn it back face-down!";
        drawStrikeAgainstPlayer(state_.activePlayer, seed, logOut);
        if (Pile* stillThere = find(wildsPileId); stillThere && !stillThere->cards.empty()) {
            stillThere->cards.back().faceUp = false;
            visual(wildsPileId).flipT = 0.f;
        }
        return;
    }

    // Like a Hunter (def 109): Reveal: Each Marked player reveals top 3 of deck and discards cost 1+
    if (revealed.def == 109) {
        syncStateFromZones();
        state_.likeAHunterRevealed.clear();
        for (int pIdx = 0; pIdx < state_.playerCount; ++pIdx) {
            if (pIdx >= static_cast<int>(state_.players.size())) break;
            if (!state_.players[static_cast<size_t>(pIdx)].alive ||
                state_.players[static_cast<size_t>(pIdx)].escapedInChoppa ||
                state_.players[static_cast<size_t>(pIdx)].marksCount <= 0) {
                continue;
            }
            const std::string deckZid = playerZone(pIdx, "deck");
            const std::string discZid = playerZone(pIdx, "discard");
            const Vec2 deckPos = zoneCenterPos(deckZid, {2002.f, 1367.f});
            const Vec2 discPos = zoneCenterPos(discZid, {2207.f, 1367.f});

            int discarded = 0;
            std::vector<CardInst> kept;
            std::mt19937 rng(seed + static_cast<std::uint32_t>(pIdx * 37));

            for (int i = 0; i < 3; ++i) {
                Pile* pDeck = findZonePile(deckZid);
                if (!pDeck || pDeck->cards.empty()) {
                    Pile* pDisc = findZonePile(discZid);
                    if (!pDisc || pDisc->cards.empty()) break;
                    std::vector<CardInst> recycled = std::move(pDisc->cards);
                    pDisc->cards.clear();
                    for (auto& c : recycled) {
                        c.faceUp = false;
                        c.attached = false;
                    }
                    std::shuffle(recycled.begin(), recycled.end(), rng);
                    Pile& newDeck = ensureZonePile(deckZid, deckPos, "");
                    newDeck.cards = std::move(recycled);
                    pDeck = &newDeck;
                }
                if (!pDeck || pDeck->cards.empty()) break;

                CardInst top = pDeck->cards.back();
                pDeck->cards.pop_back();
                const int cCost = std::max(0, catalog_.card(top.def).cost);
                const bool shouldDiscard = (cCost >= 1);

                state_.likeAHunterRevealed.push_back(
                    RevealedDeckCardEntry{pIdx, top, cCost, shouldDiscard});

                if (shouldDiscard) {
                    top.faceUp = true;
                    Pile& pDisc = ensureZonePile(discZid, discPos, "");
                    pDisc.cards.push_back(top);
                    queueCardFlight(top, deckPos, discPos, false, true);
                    ++discarded;
                } else {
                    kept.push_back(top);
                }
            }
            if (!kept.empty()) {
                Pile& curDeck = ensureZonePile(deckZid, deckPos, "");
                for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
                    curDeck.cards.push_back(*it);
                }
            }
            logOut += " [Like a Hunter: P" + std::to_string(pIdx + 1) +
                      " revealed top 3 cards -> discarded " + std::to_string(discarded) +
                      " cost 1+ card(s)!]";
        }
        if (!state_.likeAHunterRevealed.empty()) {
            state_.pendingLikeAHunterModal = true;
        }
    }

    logOut += " Revealed " + d.name + " in " + wildsName(slotIdx) + ".";
}

std::string Table::resolveEnemyPhase(std::uint32_t seed) {
    if (state_.phase == TurnPhase::Victory || state_.phase == TurnPhase::Defeat) {
        return state_.lastActionLog;
    }

    std::string log;

    // Issue A6 — Festering Wound (def 193): "At the start of your next turn, discard this and draw two Strikes."
    const std::string apStrikeZid = activePlayerZone("strikes");
    if (Pile* sw = findZonePile(apStrikeZid); sw && !sw->cards.empty()) {
        const Vec2 swPos = sw->pos;
        int festeringCount = 0;
        for (auto it = sw->cards.begin(); it != sw->cards.end();) {
            if (it->def == 193) {
                CardInst fw = *it;
                fw.faceUp = true;
                fw.attached = false;
                it = sw->cards.erase(it);
                Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
                discStrikes.cards.push_back(fw);
                queueCardFlight(fw, swPos, discStrikes.pos, true, true);
                ++festeringCount;
            } else {
                ++it;
            }
        }
        if (sw->cards.empty()) {
            const int idx = indexOf(sw->id);
            if (idx >= 0) {
                visuals_.erase(sw->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }
        if (festeringCount > 0) {
            syncStateFromZones();
            for (int f = 0; f < festeringCount; ++f) {
                log += "[Festering Wound: P" + std::to_string(state_.activePlayer + 1) +
                       " discarded Festering Wound -> draws 2 Strikes!] ";
                drawStrikeAgainstPlayer(state_.activePlayer,
                                        seed + static_cast<std::uint32_t>(f * 29 + 3), log);
                if (state_.phase == TurnPhase::Defeat ||
                    !state_.players[static_cast<size_t>(state_.activePlayer)].alive) {
                    break;
                }
                drawStrikeAgainstPlayer(state_.activePlayer,
                                        seed + static_cast<std::uint32_t>(f * 29 + 11), log);
                if (state_.phase == TurnPhase::Defeat ||
                    !state_.players[static_cast<size_t>(state_.activePlayer)].alive) {
                    break;
                }
            }
            if (state_.phase == TurnPhase::Defeat) {
                state_.lastActionLog = log;
                return log;
            }
        }
    }

    // Official Rulebook p.6: In a 5-player game, Round 1 (first 5 turns) skips the Enemy Phase
    if (state_.playerCount == 5 && state_.turnNumber <= 5) {
        state_.phase = TurnPhase::ActionPhase;
        syncStateFromZones();
        log += "Turn " + std::to_string(state_.turnNumber) + " (P" +
               std::to_string(state_.activePlayer + 1) +
               " — 5P Prep Round): Skipped Enemy Phase -> Action Phase.";
        state_.lastActionLog = log;
        return log;
    }

    Pile* prey = findZonePile("prey");
    if (!prey || prey->cards.empty()) {
        // Rulebook p.15: First time Enemy deck runs out, shuffle Dead Enemies to make a new Enemy deck.
        // Second time it runs out, the Predator kills everyone.
        Pile* deadPrey = findZonePile("dead_prey");
        if (state_.enemyDeckReshuffles == 0 && deadPrey && !deadPrey->cards.empty()) {
            std::mt19937 rng(seed);
            std::vector<CardInst> recycled = std::move(deadPrey->cards);
            deadPrey->cards.clear();
            for (auto& c : recycled) {
                c.faceUp = false;
                c.attached = false;
            }
            std::shuffle(recycled.begin(), recycled.end(), rng);

            Pile& newPrey = ensureZonePile("prey", {1620.f, 220.f}, "");
            newPrey.cards = std::move(recycled);
            state_.enemyDeckReshuffles = 1;
            prey = &newPrey;
            log += "[Enemy Deck Depleted: Reshuffled Dead Enemies into a new Enemy Deck (Rulebook p.15)!] ";
        } else {
            state_.phase = TurnPhase::Defeat;
            state_.lastActionLog =
                "DEFEAT: The Enemy Deck ran out a second time — the Predator kills everyone!";
            return state_.lastActionLog;
        }
    }

    // 1. Draw top card of Enemy deck face-down into Underground (wilds_4)
    CardInst drawn = prey->cards.back();
    prey->cards.pop_back();
    drawn.faceUp = false;
    drawn.attached = false;
    if (prey->cards.empty()) {
        const int pIdx = indexOf(prey->id);
        if (pIdx >= 0) {
            visuals_.erase(prey->id);
            state_.piles.erase(state_.piles.begin() + pIdx);
        }
    }

    log += "P" + std::to_string(state_.activePlayer + 1) +
           " Enemy Phase: Added Enemy card face-down to Underground.";
    pushCardIntoWildsSlot(4, drawn, seed, log);

    // 2. End of Enemy Phase — Runner Step (Rulebook p.14):
    // Any face-up unattached Enemy with 'Runner' in the Wilds moves 1 space left, pushing as normal.
    for (int s = 0; s <= 4; ++s) {
        const std::string zid = "wilds_" + std::to_string(s);
        if (Pile* wp = findZonePile(zid);
            wp && !wp->cards.empty() && !wp->top().attached && wp->top().faceUp) {
            const CardDef& d = catalog_.card(wp->top().def);
            if (hasKeyword(d, "Runner")) {
                CardInst runner = wp->cards.back();
                wp->cards.pop_back();
                if (wp->cards.empty()) {
                    const int idx = indexOf(wp->id);
                    if (idx >= 0) {
                        visuals_.erase(wp->id);
                        state_.piles.erase(state_.piles.begin() + idx);
                    }
                }
                log += " [Runner: " + d.name + " advanced!]";
                pushCardIntoWildsSlot(s - 1, runner, seed + 17u, log);
            }
        }
    }

    state_.phase = TurnPhase::ActionPhase;
    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("ENEMY_PHASE", catalog_.card(drawn.def).name, 1, "prey", "wilds_4", 1, false, log);
    return log;
}

void Table::drawPlayerCards(int playerIdx, int count, std::uint32_t seed, std::string& logOut) {
    const int pIdx = std::clamp(playerIdx, 0, std::max(0, state_.playerCount - 1));
    const std::string deckZid = playerZone(pIdx, "deck");
    const std::string discZid = playerZone(pIdx, "discard");
    const std::string handZid = playerZone(pIdx, "hand");

    const Zone* dz = layout_.zone(deckZid);
    const Zone* hz = layout_.zone(handZid);
    const Vec2 deckPos = dz ? dz->rect.center() : Vec2{2002.f, 1367.f};
    const Vec2 handPos = hz ? hz->rect.center() : Vec2{1185.f, 1502.f};

    std::mt19937 rng(seed);
    std::vector<std::uint32_t> drawnUids;

    for (int i = 0; i < count; ++i) {
        Pile* deck = findZonePile(deckZid);
        if (!deck || deck->cards.empty()) {
            // Rulebook p.11: Shuffle discard pile into a new deck ONLY when deck is empty AND you need to draw
            Pile* discard = findZonePile(discZid);
            if (!discard || discard->cards.empty()) break;

            std::vector<CardInst> recycled = std::move(discard->cards);
            discard->cards.clear();
            const int dIdx = indexOf(discard->id);
            if (dIdx >= 0) {
                visuals_.erase(discard->id);
                state_.piles.erase(state_.piles.begin() + dIdx);
            }
            for (auto& c : recycled) {
                c.faceUp = false;
                c.attached = false;
            }
            std::shuffle(recycled.begin(), recycled.end(), rng);

            Pile& newDeck = ensureZonePile(deckZid, deckPos, "");
            newDeck.cards = std::move(recycled);
            deck = &newDeck;
            logOut += " [P" + std::to_string(pIdx + 1) +
                      " shuffled Discard into a new Player Deck!]";
        }

        if (!deck || deck->cards.empty()) break;
        CardInst c = deck->cards.back();
        deck->cards.pop_back();
        c.faceUp = true;
        c.attached = false;

        if (deck->cards.empty()) {
            const int idx = indexOf(deck->id);
            if (idx >= 0) {
                visuals_.erase(deck->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }

        Pile hp;
        hp.id = state_.nextPileId++;
        hp.pos = handPos;
        hp.zone = handZid;
        hp.cards = {c};
        state_.piles.push_back(std::move(hp));
        drawnUids.push_back(c.uid);
    }

    reflowZone(handZid);
    for (std::uint32_t uid : drawnUids) {
        for (const auto& p : state_.piles) {
            if (p.zone == handZid && !p.cards.empty() && p.top().uid == uid) {
                visual(p.id).pos = p.pos;
                queueCardFlight(p.top(), deckPos, p.pos, false, true);
                break;
            }
        }
    }
    syncStateFromZones();
}

void Table::healPlayerStrikes(int playerIdx, int count, bool leastDamageFirst,
                              std::string& logOut) {
    // Choose target player: if requested player has 0 wounds in multiplayer, allow healing most wounded teammate
    int targetP = std::clamp(playerIdx, 0, std::max(0, state_.playerCount - 1));
    Pile* wounds = findZonePile(playerZone(targetP, "strikes"));
    if (!wounds || wounds->cards.empty()) {
        int bestDmg = -1;
        for (int q = 0; q < state_.playerCount; ++q) {
            if (q < static_cast<int>(state_.players.size()) &&
                state_.players[static_cast<size_t>(q)].alive) {
                if (Pile* qw = findZonePile(playerZone(q, "strikes"));
                    qw && !qw->cards.empty() &&
                    state_.players[static_cast<size_t>(q)].strikeDamage > bestDmg) {
                    bestDmg = state_.players[static_cast<size_t>(q)].strikeDamage;
                    targetP = q;
                    wounds = qw;
                }
            }
        }
    }
    if (!wounds || wounds->cards.empty()) return;

    const std::string strikeZid = playerZone(targetP, "strikes");
    for (int n = 0; n < count; ++n) {
        wounds = findZonePile(strikeZid);
        if (!wounds || wounds->cards.empty()) break;

        // Issue A2 — Open Wound (def 194): "Your Strikes other than Open Wounds can't be healed."
        bool hasOpenWound = false;
        for (const auto& c : wounds->cards) {
            if (c.def == 194) {
                hasOpenWound = true;
                break;
            }
        }

        int bestIdx = -1;
        int bestDmg = leastDamageFirst ? 999 : -1;
        for (int i = 0; i < static_cast<int>(wounds->cards.size()); ++i) {
            const int defId = wounds->cards[static_cast<size_t>(i)].def;
            if (hasOpenWound && defId != 194) continue; // Must heal Open Wound first!
            const int dmg = std::max(0, catalog_.card(defId).attack);
            if ((leastDamageFirst && dmg < bestDmg) || (!leastDamageFirst && dmg > bestDmg)) {
                bestDmg = dmg;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;

        const Vec2 fromPos = wounds->pos;
        CardInst healed = wounds->cards[static_cast<size_t>(bestIdx)];
        wounds->cards.erase(wounds->cards.begin() + bestIdx);
        if (wounds->cards.empty()) {
            const int idx = indexOf(wounds->id);
            if (idx >= 0) {
                visuals_.erase(wounds->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }

        Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
        discStrikes.cards.push_back(healed);
        queueCardFlight(healed, fromPos, discStrikes.pos, true, true);
        logOut += " [Healed P" + std::to_string(targetP + 1) +
                  " Strike: " + catalog_.card(healed.def).name + "!]";
        syncStateFromZones();

        // Issue A3 — Permanent Damage (def 190, 3 dmg): "When this Strike is healed from you, draw a Strike."
        if (healed.def == 190) {
            logOut += " [Permanent Damage healed -> P" + std::to_string(targetP + 1) +
                      " draws a replacement Strike!]";
            const std::uint32_t strikeSeed = static_cast<std::uint32_t>(
                state_.turnNumber * 131 + state_.nextMoveSeq * 17 + n + targetP + 1);
            drawStrikeAgainstPlayer(targetP, strikeSeed, logOut);
            if (state_.phase == TurnPhase::Defeat ||
                !state_.players[static_cast<size_t>(targetP)].alive) {
                break;
            }
        }
    }

    syncStateFromZones();
}

void Table::autoPlayHandCardsForRecruit(int recruitNeeded, std::string& logOut) {
    if (recruitNeeded <= 0) return;
    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    int unplayedHandRecruit = 0;
    struct HandCand {
        PileId pileId;
        CardInst card;
        int effRec;
        bool isStarter;
    };
    std::vector<HandCand> cands;
    for (const auto& p : state_.piles) {
        if (p.zone != handZid || p.cards.empty()) continue;
        const CardInst c = p.top();
        int effRec = 0;
        if (auto it = state_.accountedCardRecruit.find(c.uid); it != state_.accountedCardRecruit.end()) {
            effRec = it->second;
        } else {
            int r = 0, a = 0;
            effectiveCardStats(c, &r, &a);
            effRec = r;
        }
        if (effRec > 0) {
            unplayedHandRecruit += effRec;
            cands.push_back({p.id, c, effRec, c.def == 225});
        }
    }

    const int alreadyPlayedUnspent = std::max(0, state_.recruitPool - unplayedHandRecruit);
    int needFromHand = recruitNeeded - alreadyPlayedUnspent;
    if (needFromHand <= 0 || cands.empty()) return;

    // Prioritize Starter cards (Experience id 225) first, then lowest Recruit value
    std::sort(cands.begin(), cands.end(), [](const HandCand& a, const HandCand& b) {
        if (a.isStarter != b.isStarter) return a.isStarter > b.isStarter;
        if (a.effRec != b.effRec) return a.effRec < b.effRec;
        return a.card.uid < b.card.uid;
    });

    bool movedAny = false;
    for (const auto& hc : cands) {
        if (needFromHand <= 0) break;
        if (Pile* hp = find(hc.pileId)) {
            const Vec2 fromPos = hp->pos;
            hp->zone = playedZid;
            needFromHand -= hc.effRec;
            movedAny = true;
            reflowZone(playedZid);
            if (const Pile* afterP = find(hc.pileId)) {
                queueCardFlight(hc.card, fromPos, afterP->pos, true, true);
            }
            const CardDef& cd = catalog_.card(hc.card.def);
            logOut += " [Auto-played " + (cd.title.empty() ? cd.name : cd.title) + " (" +
                      std::to_string(hc.effRec) + " *)]";
        }
    }
    if (movedAny) {
        reflowZone(handZid);
        reflowZone(playedZid);
        syncStateFromZones();
    }
}

void Table::autoPlayHandCardsForAttack(int attackNeeded, std::string& logOut) {
    if (attackNeeded <= 0) return;
    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    int unplayedHandAttack = 0;
    struct HandCand {
        PileId pileId;
        CardInst card;
        int effAtk;
        bool isStarter;
    };
    std::vector<HandCand> cands;
    for (const auto& p : state_.piles) {
        if (p.zone != handZid || p.cards.empty()) continue;
        const CardInst c = p.top();
        int effAtk = 0;
        if (auto it = state_.accountedCardAttack.find(c.uid); it != state_.accountedCardAttack.end()) {
            effAtk = it->second;
        } else {
            int r = 0, a = 0;
            effectiveCardStats(c, &r, &a);
            effAtk = a;
        }
        if (effAtk > 0) {
            unplayedHandAttack += effAtk;
            cands.push_back({p.id, c, effAtk, c.def == 224});
        }
    }

    const int alreadyPlayedUnspent = std::max(0, state_.attackPool - unplayedHandAttack);
    int needFromHand = attackNeeded - alreadyPlayedUnspent;
    if (needFromHand <= 0 || cands.empty()) return;

    // Prioritize Starter cards (Brute Strength id 224) first, then lowest Attack value
    std::sort(cands.begin(), cands.end(), [](const HandCand& a, const HandCand& b) {
        if (a.isStarter != b.isStarter) return a.isStarter > b.isStarter;
        if (a.effAtk != b.effAtk) return a.effAtk < b.effAtk;
        return a.card.uid < b.card.uid;
    });

    bool movedAny = false;
    for (const auto& hc : cands) {
        if (needFromHand <= 0) break;
        if (Pile* hp = find(hc.pileId)) {
            const Vec2 fromPos = hp->pos;
            hp->zone = playedZid;
            needFromHand -= hc.effAtk;
            movedAny = true;
            reflowZone(playedZid);
            if (const Pile* afterP = find(hc.pileId)) {
                queueCardFlight(hc.card, fromPos, afterP->pos, true, true);
            }
            const CardDef& cd = catalog_.card(hc.card.def);
            logOut += " [Auto-played " + (cd.title.empty() ? cd.name : cd.title) + " (" +
                      std::to_string(hc.effAtk) + " ///)]";
        }
    }
    if (movedAny) {
        reflowZone(handZid);
        reflowZone(playedZid);
        syncStateFromZones();
    }
}

void Table::autoPlayHandCardMatchingDef(int defId, std::string& logOut) {
    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");
    for (auto& p : state_.piles) {
        if (p.zone == handZid && !p.cards.empty() && p.top().def == defId) {
            const Vec2 fromPos = p.pos;
            const CardInst c = p.top();
            const PileId pid = p.id;
            p.zone = playedZid;
            reflowZone(handZid);
            reflowZone(playedZid);
            if (const Pile* afterP = find(pid)) {
                queueCardFlight(c, fromPos, afterP->pos, true, true);
            }
            const CardDef& cd = catalog_.card(defId);
            logOut += " [Auto-played " + (cd.title.empty() ? cd.name : cd.title) + "]";
            syncStateFromZones();
            return;
        }
    }
}

int Table::maxAbilityActivationsForCard(const CardInst& c) const {
    const CardDef& d = catalog_.card(c.def);
    switch (c.def) {
    case 0:  // Sexual Tyrannosaurus (Blain): [Survival]: Heal Strike with least damage
    case 1:  // Ol' Painless Is Waiting (Blain): [Leadership]: Any player draws 2 & discards 2
    case 5:  // Pushing Pencils (Dillon): [Tech]: Sneak Peek hidden card in The Wilds & optional free scan
    case 13: // Same Kind of Jungle (Mac): [Strength]: Switch two cards in The Wilds
        return (countTurnClass(d.classType) >= 2) ? 1 : 0;
    case 8: // Stick Around (Dutch): [Dutch's Team]: Kill a card in your hand or played area
        return (countTurnTeam(d.team) >= 2) ? 1 : 0;
    case 14: // I See You (Mac): Brothers in Arms: Heal a Strike (0..3 times)
        return countOtherBarracksChars(d.subtitle);
    case 3:  // Ain't Got Time to Bleed (Blain Rare): Flip top 2 Strikes for +Attack
    case 7:  // I Can Get Even (Dillon Rare): Choose player -> hand gains Coordinate
    case 11: // If It Bleeds, We Can Kill It (Dutch Rare): Can't die until end of next turn
    case 12: // Gonna Cut Your Name Into Him! (Mac): Draw a card
    case 85: // City Hunter Role (Close Quarters): Activate: Draw a card
    case 87: // Elder Hunter Role (Crafty Veteran): Activate: Heal a Strike from yourself
    case 89: // Gunslinger Hunter Role (Quick Draw): Activate: +1 Free Scan
    case 91: // Jungle Hunter Role (Stalking the Jungle): Activate: Draw a card
    case 93: // Primeval Hunter Role (First of His Breed): Activate: Draw a card
        return 1;
    case 67: // Knocking on Doors (Detective Role): Call For Backup: Draw a card
    case 69: // Running the Streets (Gangster Role): Call For Backup: Kill one of your cards
    case 75: // The Highest Tech (OWLF Agent Role): Call For Backup: Swap printed Recruit/Attack on a card
    case 77: // Position and Situation (Radioman Role): Call For Backup: Put card from discard on top of deck
    case 79: // Sources and Contacts (Reporter Role): Call For Backup: Heal a Strike from any player
    case 81: // Partners in Crime (SWAT Officer Role): Call For Backup: Gain Commander or HQ card <= 3
    case 83: // It Ain't No Man (Tracker Role): Call For Backup: Scan any space
        return state_.coordinatedCountThisTurn;
    default:
        return 0;
    }
}

int Table::remainingAbilityActivations(const CardInst& c, std::string* labelOut,
                                       std::string* reasonOut) const {
    const int maxUses = maxAbilityActivationsForCard(c);
    int used = 0;
    if (auto it = state_.abilityUsesThisTurn.find(c.uid); it != state_.abilityUsesThisTurn.end()) {
        used = it->second;
    }
    const int rem = std::max(0, maxUses - used);
    const CardDef& d = catalog_.card(c.def);

    if (labelOut) {
        switch (c.def) {
        case 0:
            *labelOut = "[Survival] Heal Lowest-Damage Strike";
            break;
        case 1:
            *labelOut = "[Leadership] Player Draws 2 & Discards 2";
            break;
        case 3:
            *labelOut = "Flip Top 2 Strikes for +Attack";
            break;
        case 5:
            *labelOut = "[Tech] Sneak Peek Hidden Wilds Card";
            break;
        case 7:
            *labelOut = "Grant Player's Hand Coordinate This Turn";
            break;
        case 8:
            *labelOut = "[Dutch's Team] Choose a Card to Kill";
            break;
        case 11:
            *labelOut = "Activate Death Immunity (Until End of Next Turn)";
            break;
        case 12:
            *labelOut = "Draw 1 Card";
            break;
        case 13:
            *labelOut = "[Strength] Switch Two Cards in The Wilds";
            break;
        case 14:
            *labelOut = "Brothers in Arms: Heal a Strike (" + std::to_string(rem) + " left)";
            break;
        case 67:
            *labelOut = "Call For Backup: Draw 1 Card";
            break;
        case 69:
            *labelOut = "Call For Backup: Choose a Card to Kill";
            break;
        case 75:
            *labelOut = "Call For Backup: Swap Card Recruit & Attack";
            break;
        case 77:
            *labelOut = "Call For Backup: Place Discard Card on Deck Top";
            break;
        case 79:
            *labelOut = "Call For Backup: Heal a Strike From Any Player";
            break;
        case 81:
            *labelOut = "Call For Backup: Gain Commander or HQ Card (Cost <= 3)";
            break;
        case 83:
            *labelOut = "Call For Backup: +1 Free Scan";
            break;
        case 85:
        case 91:
        case 93:
            *labelOut = "Activate: Draw 1 Card";
            break;
        case 87:
            *labelOut = "Activate: Heal a Strike From Yourself";
            break;
        case 89:
            *labelOut = "Activate: +1 Free Scan";
            break;
        default:
            *labelOut = "Activate Ability";
            break;
        }
    }

    if (rem <= 0) {
        if (reasonOut) {
            if (used > 0 && maxUses > 0) {
                *reasonOut = "Ability already used this turn.";
            } else if (c.def == 0 || c.def == 1 || c.def == 5 || c.def == 13) {
                *reasonOut = "Requires at least 2 [" + d.classType + "] cards in your hand/turn.";
            } else if (c.def == 8) {
                *reasonOut = "Requires at least 2 [" + d.team + "] cards in your hand/turn.";
            } else if (c.def == 14) {
                *reasonOut = "Requires at least 1 other Barracks character in your hand/turn.";
            } else if (c.def >= 65 && c.def <= 83) {
                *reasonOut = "Requires Call For Backup (a card Coordinated this turn).";
            } else {
                *reasonOut = "No activatable ability.";
            }
        }
        return 0;
    }

    // Check board/target preconditions so abilities don't misfire with 0 valid targets
    if (c.def == 0 || c.def == 14 || c.def == 79) {
        bool anyWounds = false;
        for (int q = 0; q < state_.playerCount; ++q) {
            if (q < static_cast<int>(state_.players.size()) &&
                state_.players[static_cast<size_t>(q)].alive) {
                if (const Pile* w = findZonePile(playerZone(q, "strikes"));
                    w && !w->cards.empty()) {
                    anyWounds = true;
                    break;
                }
            }
        }
        if (!anyWounds) {
            if (reasonOut) *reasonOut = "No players currently have any Strikes to heal.";
            return 0;
        }
    } else if (c.def == 87) {
        const Pile* w = findZonePile(activePlayerZone("strikes"));
        if (!w || w->cards.empty()) {
            if (reasonOut) *reasonOut = "You have no Strikes to heal.";
            return 0;
        }
    } else if (c.def == 5) {
        bool anyHiddenWilds = false;
        for (int s = 0; s <= 4; ++s) {
            if (const Pile* wp = findZonePile("wilds_" + std::to_string(s));
                wp && !wp->cards.empty() && !wp->top().faceUp) {
                anyHiddenWilds = true;
                break;
            }
        }
        if (!anyHiddenWilds) {
            if (reasonOut) *reasonOut = "There are no hidden (face-down) cards in The Wilds.";
            return 0;
        }
    } else if (c.def == 13) {
        int swappableCount = 0;
        for (int s = 0; s <= 4; ++s) {
            if (isWildsSlotSwappable(s)) ++swappableCount;
        }
        if (swappableCount < 2) {
            if (reasonOut) *reasonOut = "Requires at least 2 swappable cards in The Wilds.";
            return 0;
        }
    } else if (c.def == 77) {
        const Pile* disc = findZonePile(activePlayerZone("discard"));
        if (!disc || disc->cards.empty()) {
            if (reasonOut) *reasonOut = "Your Discard pile is empty.";
            return 0;
        }
    }

    return rem;
}

bool Table::canActivateCardAbility(std::uint32_t cardUid, std::string* labelOut,
                                   std::string* reasonOut) const {
    if (state_.phase != TurnPhase::ActionPhase) {
        if (reasonOut) *reasonOut = "Abilities can only be activated during the Action Phase.";
        return false;
    }
    if (hasPendingModal()) {
        if (reasonOut) *reasonOut = "Finish the current modal prompt first.";
        return false;
    }

    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");
    for (const auto& p : state_.piles) {
        if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty() &&
            p.top().uid == cardUid) {
            return remainingAbilityActivations(p.top(), labelOut, reasonOut) > 0;
        }
    }
    for (const auto& cc : state_.coordinatedCards) {
        if (cc.uid == cardUid) {
            return remainingAbilityActivations(cc, labelOut, reasonOut) > 0;
        }
    }
    if (reasonOut) *reasonOut = "Card is not in the active player's Hand or Played Area.";
    return false;
}

std::string Table::activateCardAbility(std::uint32_t cardUid, std::uint32_t seed) {
    if (state_.phase == TurnPhase::EnemyPhase) {
        resolveEnemyPhase(seed);
    }
    std::string label, reason;
    if (!canActivateCardAbility(cardUid, &label, &reason)) {
        return reason.empty() ? "Cannot activate ability right now." : reason;
    }

    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    std::optional<CardInst> targetCard;
    PileId handPileToMove = kNoPile;
    for (const auto& p : state_.piles) {
        if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty() &&
            p.top().uid == cardUid) {
            targetCard = p.top();
            if (p.zone == handZid) handPileToMove = p.id;
            break;
        }
    }
    if (!targetCard) {
        for (const auto& cc : state_.coordinatedCards) {
            if (cc.uid == cardUid) {
                targetCard = cc;
                break;
            }
        }
    }
    if (!targetCard) return "Card not found.";

    // If the card is still in Hand, move it to Played Area (or Avatar slot for id 11) with flight animation!
    if (handPileToMove != kNoPile) {
        if (Pile* hp = find(handPileToMove)) {
            const Vec2 fromPos = hp->pos;
            hp->zone = playedZid;
            reflowZone(handZid);
            reflowZone(playedZid);
            if (const Pile* afterP = find(handPileToMove)) {
                queueCardFlight(*targetCard, fromPos, afterP->pos, true, true);
            }
        }
    }

    const int ap = state_.activePlayer;
    const CardDef& d = catalog_.card(targetCard->def);
    const std::string cardTitle = d.title.empty() ? d.name : d.title;
    std::string log = "P" + std::to_string(ap + 1) + " activated " + cardTitle + ": ";

    switch (targetCard->def) {
    case 0: // Sexual Tyrannosaurus (Blain): [Survival]: Heal Strike with least damage
        state_.pendingHealChoice = true;
        state_.pendingHealLeastOnly = true;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a lowest-damage Strike to heal.";
        break;

    case 1: // Ol' Painless Is Waiting (Blain): [Leadership]: Any player draws 2 & discards 2
        state_.pendingPlayerChoiceMode = 1;
        state_.pendingAbilitySourceUid = targetCard->uid;
        if (state_.playerCount == 1) {
            state_.abilityUsesThisTurn[targetCard->uid] += 1;
            state_.pendingPlayerChoiceMode = 0;
            return resolvePlayerPickerChoice(ap, seed);
        }
        log += "Choose which player draws 2 cards and discards 2 cards.";
        break;

    case 3: { // Ain't Got Time to Bleed (Blain Rare): Flip top 2 Strikes into discard & gain Attack
        state_.abilityUsesThisTurn[targetCard->uid] += 1;
        int bonusAtk = 0;
        for (int i = 0; i < 2; ++i) {
            Pile* strikes = findZonePile("strikes");
            if (!strikes || strikes->cards.empty()) {
                Pile* discStrikes = findZonePile("discard_strikes");
                if (discStrikes && !discStrikes->cards.empty()) {
                    std::mt19937 rng(seed + static_cast<std::uint32_t>(i * 37));
                    std::vector<CardInst> recycled = std::move(discStrikes->cards);
                    discStrikes->cards.clear();
                    for (auto& rc : recycled) {
                        rc.faceUp = false;
                        rc.attached = false;
                    }
                    std::shuffle(recycled.begin(), recycled.end(), rng);
                    Pile& newStrikes = ensureZonePile("strikes", {1620.f, 550.f}, "");
                    newStrikes.cards = std::move(recycled);
                    strikes = &newStrikes;
                }
            }
            if (!strikes || strikes->cards.empty()) break;
            const Vec2 fromPos = strikes->pos;
            CardInst s = strikes->cards.back();
            strikes->cards.pop_back();
            s.faceUp = true;
            bonusAtk += std::max(0, catalog_.card(s.def).attack);
            Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
            discStrikes.cards.push_back(s);
            queueCardFlight(s, fromPos, discStrikes.pos, false, true, i);
        }
        state_.attackPool += bonusAtk;
        log += "Flipped 2 Strikes into Discarded Strikes for +" + std::to_string(bonusAtk) +
               " Attack ///!";
        break;
    }

    case 5: // Pushing Pencils (Dillon): [Tech]: Sneak Peek hidden card in The Wilds
        state_.pendingSneakPeekChoice = true;
        state_.sneakPeekRevealedCardSelected = false;
        state_.sneakPeekWildsSlot = -1;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a hidden card in The Wilds to Sneak Peek.";
        break;

    case 7: // I Can Get Even (Dillon Rare): Choose player -> hand gains Coordinate
        state_.pendingPlayerChoiceMode = 3;
        state_.pendingAbilitySourceUid = targetCard->uid;
        if (state_.playerCount == 1) {
            state_.abilityUsesThisTurn[targetCard->uid] += 1;
            state_.pendingPlayerChoiceMode = 0;
            return resolvePlayerPickerChoice(ap, seed);
        }
        log += "Choose a player whose hand cards gain Coordinate this turn.";
        break;

    case 8:  // Stick Around (Dutch): [Dutch's Team]: Kill a card in your hand or played area
    case 69: // Running the Streets (Gangster Role): Call For Backup: Kill one of your cards
        state_.pendingKillChoice = true;
        state_.pendingKillCount = 1;
        state_.pendingKillMinCost = 0;
        state_.pendingKillMandatory = false;
        state_.pendingKillStayUntilCleanup = true;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a card in your Hand or Played Area to kill (stays in play with KILLED badge until Cleanup).";
        break;

    case 11: // If It Bleeds, We Can Kill It (Dutch Rare): Can't die until end of next turn
        state_.abilityUsesThisTurn[targetCard->uid] += 1;
        if (ap < static_cast<int>(state_.players.size())) {
            state_.players[static_cast<size_t>(ap)].cannotDieTurnsRemaining = 2;
            state_.players[static_cast<size_t>(ap)].activeImmunityCardUid = targetCard->uid;
        }
        state_.cannotDieTurnsRemaining = 2;
        reflowZone(playedZid);
        log += "Stationed next to your Avatar! You cannot die until the end of your next turn.";
        break;

    case 12: // Gonna Cut Your Name Into Him! (Mac): Draw a card
    case 67: // Knocking on Doors (Detective Role): Call For Backup: Draw a card
    case 85: // City Hunter Role
    case 91: // Jungle Hunter Role
    case 93: // Primeval Hunter Role
        state_.abilityUsesThisTurn[targetCard->uid] += 1;
        drawPlayerCards(ap, 1, seed + 23u, log);
        log += "Drew 1 card!";
        break;

    case 13: // Same Kind of Jungle (Mac): [Strength]: Switch two cards in The Wilds
        state_.pendingWildsSwapChoice = true;
        state_.wildsSwapFirstSlot = -1;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Select two spaces in The Wilds to switch their cards.";
        break;

    case 14: // I See You (Mac): Brothers in Arms: Heal a Strike from any player
    case 79: // Sources and Contacts (Reporter Role): Call For Backup: Heal a Strike from any player
        state_.pendingHealChoice = true;
        state_.pendingHealLeastOnly = false;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a Strike on any player to heal.";
        break;

    case 75: // The Highest Tech (OWLF Agent Role): Call For Backup: Swap printed Recruit/Attack on a card
        state_.pendingStatSwapChoice = true;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a card in your Hand or Played Area to swap its printed Recruit * and Attack /// this turn.";
        break;

    case 77: // Position and Situation (Radioman Role): Call For Backup: Put card from discard on top of deck
        state_.pendingDiscardToDeckChoice = true;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a card from your Discard pile to place face-down on top of your Deck.";
        break;

    case 81: // Partners in Crime (SWAT Officer Role): Call For Backup: Gain Commander or HQ card <= 3
        state_.pendingGainCardChoice = true;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose a Commander or an HQ card with cost 3 or less to gain to your Discard pile.";
        break;

    case 83: // It Ain't No Man (Tracker Role): Call For Backup: Scan any space
    case 89: // Gunslinger Hunter Role (Quick Draw): Activate: +1 Free Scan
        state_.abilityUsesThisTurn[targetCard->uid] += 1;
        state_.freeScansAvailable += 1;
        log += "+1 Free Scan ready!";
        break;

    case 87: // Elder Hunter Role (Crafty Veteran): Activate: Heal a Strike from yourself
        state_.pendingHealChoice = true;
        state_.pendingHealLeastOnly = false;
        state_.pendingAbilitySourceUid = targetCard->uid;
        log += "Choose one of your Strikes to heal.";
        break;

    default:
        break;
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("ACTIVATE_ABILITY", cardTitle, 1, playedZid, playedZid, 1, true, log);
    return log;
}

bool Table::hasPendingModal() const {
    return state_.pendingSacrificeChoice || state_.pendingCoverMeChoice ||
           state_.pendingSneakPeekChoice || state_.pendingKillChoice ||
           state_.pendingHealChoice || state_.pendingPlayerChoiceMode != 0 ||
           state_.pendingWildsSwapChoice || state_.pendingStatSwapChoice ||
           state_.pendingDiscardToDeckChoice || state_.pendingGainCardChoice ||
           state_.pendingLikeAHunterModal;
}

std::string Table::cancelOptionalModal() {
    if (state_.pendingKillChoice && state_.pendingKillMandatory) {
        return "This kill effect is mandatory and cannot be canceled.";
    }
    if (state_.pendingPlayerChoiceMode == 2) {
        return "Must finish choosing cards to discard.";
    }
    if (state_.pendingSacrificeChoice || state_.pendingCoverMeChoice) {
        return "Must resolve the incoming Strike prompt.";
    }

    state_.pendingSneakPeekChoice = false;
    state_.sneakPeekRevealedCardSelected = false;
    state_.sneakPeekWildsSlot = -1;
    state_.pendingKillChoice = false;
    state_.pendingHealChoice = false;
    state_.pendingPlayerChoiceMode = 0;
    state_.pendingWildsSwapChoice = false;
    state_.wildsSwapFirstSlot = -1;
    state_.pendingStatSwapChoice = false;
    state_.pendingDiscardToDeckChoice = false;
    state_.pendingGainCardChoice = false;
    state_.pendingLikeAHunterModal = false;
    state_.pendingAbilitySourceUid = 0;
    state_.lastActionLog = "Canceled ability prompt (ability remains available this turn).";
    return state_.lastActionLog;
}

std::string Table::selectSneakPeekWildsSlot(int slotIdx) {
    if (!state_.pendingSneakPeekChoice) return "No Sneak Peek active.";
    if (slotIdx < 0 || slotIdx > 4) return "Invalid Wilds slot.";
    const Pile* wp = findZonePile("wilds_" + std::to_string(slotIdx));
    if (!wp || wp->cards.empty() || wp->top().faceUp) {
        return std::string(wildsName(slotIdx)) + " does not have a hidden face-down card.";
    }
    state_.sneakPeekWildsSlot = slotIdx;
    state_.sneakPeekRevealedCardSelected = true;
    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    const CardDef& d = catalog_.card(wp->top().def);
    state_.lastActionLog = "Sneak Peek (" + std::string(wildsName(slotIdx)) +
                           "): Privately viewing '" + d.name +
                           "'. Choose whether to scan it face-up for free or keep it hidden!";
    return state_.lastActionLog;
}

std::string Table::resolveSneakPeekDecision(bool freeScanFaceUp, std::uint32_t seed) {
    if (!state_.pendingSneakPeekChoice || !state_.sneakPeekRevealedCardSelected) {
        return "Select a hidden Wilds space to Sneak Peek first.";
    }
    const int slotIdx = state_.sneakPeekWildsSlot;
    state_.pendingSneakPeekChoice = false;
    state_.sneakPeekRevealedCardSelected = false;
    state_.sneakPeekWildsSlot = -1;

    Pile* wp = findZonePile("wilds_" + std::to_string(slotIdx));
    if (!wp || wp->cards.empty()) return "Wilds slot is empty.";

    const CardDef& d = catalog_.card(wp->top().def);
    std::string log;
    if (!freeScanFaceUp) {
        log = "Pushing Pencils: Kept hidden card in " + std::string(wildsName(slotIdx)) +
              " face-down (no Reveal effect triggered).";
    } else {
        wp->cards.back().faceUp = true;
        visual(wp->id).flipT = 0.f;
        log = "Pushing Pencils: Free-scanned " + std::string(wildsName(slotIdx)) + " -> revealed " +
              d.name + "!";
        onCardRevealedInWilds(wp->id, slotIdx, seed, log);
    }
    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("SNEAK_PEEK", d.name, 1, "wilds_" + std::to_string(slotIdx),
               "wilds_" + std::to_string(slotIdx), 1, freeScanFaceUp, log);
    return log;
}

std::string Table::resolveKillCardChoice(std::uint32_t targetCardUid) {
    if (!state_.pendingKillChoice) return "No pending Kill Card choice.";

    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    Pile* targetPile = nullptr;
    for (auto& p : state_.piles) {
        if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty() &&
            p.top().uid == targetCardUid) {
            targetPile = &p;
            break;
        }
    }
    if (!targetPile) return "Select a card in your Hand or Played Area.";
    if (targetCardUid == state_.pendingAbilitySourceUid && !state_.pendingKillMandatory) {
        return "Choose a different card to kill.";
    }
    if (isCardKilledInPlayed(targetCardUid)) {
        return "That card is already marked as KILLED.";
    }

    const CardInst c = targetPile->top();
    const CardDef& d = catalog_.card(c.def);
    if (d.cost < state_.pendingKillMinCost) {
        return "You must choose a card with cost " + std::to_string(state_.pendingKillMinCost) +
               " or greater.";
    }

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }

    std::string log;
    if (state_.pendingKillStayUntilCleanup) {
        // Player ability kill (Stick Around / Running the Streets):
        // Card stays in Played Area with KILLED badge until Cleanup, keeping its stats/class/team for the turn!
        const Vec2 fromPos = targetPile->pos;
        const PileId pid = targetPile->id;
        targetPile->zone = playedZid;
        state_.killedPlayedUids.push_back(c.uid);
        reflowZone(handZid);
        reflowZone(playedZid);
        if (const Pile* afterP = find(pid)) {
            if (std::hypot(afterP->pos.x - fromPos.x, afterP->pos.y - fromPos.y) > 4.f) {
                queueCardFlight(c, fromPos, afterP->pos, true, true);
            }
        }
        log = "Marked '" + (d.title.empty() ? d.name : d.title) +
              "' as KILLED in Played Area (retains stats/class/team this turn -> moves to Dead Characters at Cleanup).";
    } else {
        // Enemy death mandatory kill (Nothing On Earth / A Fitting Demise):
        // Lock already-contributed turn stats so killing doesn't retroactively corrupt spent pools, then move to dead_characters.
        if (std::find(state_.lockedAccountedUids.begin(), state_.lockedAccountedUids.end(),
                      c.uid) == state_.lockedAccountedUids.end()) {
            state_.lockedAccountedUids.push_back(c.uid);
        }
        const Vec2 fromPos = targetPile->pos;
        targetPile->cards.pop_back();
        if (targetPile->cards.empty()) {
            const int idx = indexOf(targetPile->id);
            if (idx >= 0) {
                visuals_.erase(targetPile->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }
        Pile& deadChars = ensureZonePile("dead_characters", {1846.f, 871.f}, "");
        deadChars.cards.push_back(c);
        queueCardFlight(c, fromPos, deadChars.pos, true, true);
        reflowZone(handZid);
        reflowZone(playedZid);
        log = "Killed '" + (d.title.empty() ? d.name : d.title) + "' -> moved to Dead Characters.";
    }

    state_.pendingKillCount -= 1;
    if (state_.pendingKillCount <= 0) {
        state_.pendingKillChoice = false;
    } else {
        // Check if any eligible card remains for multi-kill (Nothing On Earth)
        bool anyEligibleLeft = false;
        for (const auto& p : state_.piles) {
            if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty() &&
                !isCardKilledInPlayed(p.top().uid) &&
                catalog_.card(p.top().def).cost >= state_.pendingKillMinCost) {
                anyEligibleLeft = true;
                break;
            }
        }
        if (!anyEligibleLeft) {
            state_.pendingKillChoice = false;
            state_.pendingKillCount = 0;
        }
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("KILL_CARD", d.title.empty() ? d.name : d.title, 1, playedZid, "dead_characters", 1,
               true, log);
    return log;
}

std::string Table::resolveHealStrikeChoice(int targetPlayerIdx, std::uint32_t strikeCardUid,
                                           std::uint32_t seed) {
    if (!state_.pendingHealChoice) return "No pending Heal Strike choice.";
    if (targetPlayerIdx < 0 || targetPlayerIdx >= state_.playerCount) return "Invalid player.";

    Pile* wounds = findZonePile(playerZone(targetPlayerIdx, "strikes"));
    if (!wounds || wounds->cards.empty()) return "That player has no Strikes to heal.";

    // Issue A2 — Open Wound (def 194): "Your Strikes other than Open Wounds can't be healed."
    bool hasOpenWound = false;
    for (const auto& c : wounds->cards) {
        if (c.def == 194) {
            hasOpenWound = true;
            break;
        }
    }

    int minAllowedDmg = 999;
    if (state_.pendingHealLeastOnly) {
        for (int q = 0; q < state_.playerCount; ++q) {
            const Pile* qw = findZonePile(playerZone(q, "strikes"));
            if (!qw || qw->cards.empty()) continue;
            bool qOpenWound = false;
            for (const auto& qc : qw->cards) {
                if (qc.def == 194) {
                    qOpenWound = true;
                    break;
                }
            }
            for (const auto& qc : qw->cards) {
                if (qOpenWound && qc.def != 194) continue;
                const int dmg = std::max(0, catalog_.card(qc.def).attack);
                minAllowedDmg = std::min(minAllowedDmg, dmg);
            }
        }
    }

    int foundIdx = -1;
    for (int i = 0; i < static_cast<int>(wounds->cards.size()); ++i) {
        if (wounds->cards[static_cast<size_t>(i)].uid == strikeCardUid) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx < 0) return "Strike card not found on that player.";

    const CardInst chosen = wounds->cards[static_cast<size_t>(foundIdx)];
    if (hasOpenWound && chosen.def != 194) {
        return "Open Wound rule: You must heal Open Wound before any other Strike on P" +
               std::to_string(targetPlayerIdx + 1) + "!";
    }
    const int chosenDmg = std::max(0, catalog_.card(chosen.def).attack);
    if (state_.pendingHealLeastOnly && chosenDmg > minAllowedDmg) {
        return "Sexual Tyrannosaurus must heal a Strike with the least damage (" +
               std::to_string(minAllowedDmg) + " DMG)!";
    }

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    state_.pendingHealChoice = false;
    state_.pendingHealLeastOnly = false;

    const Vec2 fromPos = wounds->pos;
    wounds->cards.erase(wounds->cards.begin() + foundIdx);
    if (wounds->cards.empty()) {
        const int idx = indexOf(wounds->id);
        if (idx >= 0) {
            visuals_.erase(wounds->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }
    }

    Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
    discStrikes.cards.push_back(chosen);
    queueCardFlight(chosen, fromPos, discStrikes.pos, true, true);
    syncStateFromZones();

    std::string log = "Healed P" + std::to_string(targetPlayerIdx + 1) + "'s Strike: " +
                      catalog_.card(chosen.def).name + " (" + std::to_string(chosenDmg) + " DMG)!";

    // Issue A3 — Permanent Damage (def 190): "When this Strike is healed from you, draw a Strike."
    if (chosen.def == 190) {
        log += " [Permanent Damage healed -> P" + std::to_string(targetPlayerIdx + 1) +
               " draws a replacement Strike!]";
        drawStrikeAgainstPlayer(targetPlayerIdx, seed + 89u, log);
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("HEAL_STRIKE", catalog_.card(chosen.def).name, 1,
               playerZone(targetPlayerIdx, "strikes"), "discard_strikes",
               static_cast<int>(discStrikes.cards.size()), true, log);
    return log;
}

std::string Table::resolvePlayerPickerChoice(int chosenPlayerIdx, std::uint32_t seed) {
    if (chosenPlayerIdx < 0 || chosenPlayerIdx >= state_.playerCount ||
        !state_.players[static_cast<size_t>(chosenPlayerIdx)].alive) {
        return "Choose a living player.";
    }

    const int mode = state_.pendingPlayerChoiceMode;
    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }

    std::string log;
    if (mode == 1 || mode == 0) {
        // Ol' Painless Is Waiting (id 1): Chosen player draws 2 cards, then discards 2 cards
        drawPlayerCards(chosenPlayerIdx, 2, seed + 47u, log);
        int handCount = 0;
        const std::string hZid = playerZone(chosenPlayerIdx, "hand");
        for (const auto& p : state_.piles) {
            if (p.zone == hZid && !p.cards.empty()) ++handCount;
        }
        state_.pendingTargetPlayer = chosenPlayerIdx;
        state_.pendingDiscardRemaining = std::min(2, handCount);
        state_.pendingPlayerChoiceMode = (state_.pendingDiscardRemaining > 0) ? 2 : 0;
        log = "Ol' Painless Is Waiting: P" + std::to_string(chosenPlayerIdx + 1) +
              " drew 2 cards — now choose " + std::to_string(state_.pendingDiscardRemaining) +
              " card(s) from P" + std::to_string(chosenPlayerIdx + 1) + "'s hand to discard.";
    } else if (mode == 3) {
        // I Can Get Even (id 7): All cards in chosen player's hand gain Coordinate this turn
        state_.players[static_cast<size_t>(chosenPlayerIdx)].allHandCoordinateThisTurn = true;
        state_.pendingPlayerChoiceMode = 0;
        log = "I Can Get Even: All cards in P" + std::to_string(chosenPlayerIdx + 1) +
              "'s hand gain Coordinate until end of turn!";
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    return log;
}

std::string Table::resolveHandDiscardChoice(std::uint32_t handCardUid) {
    if (state_.pendingPlayerChoiceMode != 2 || state_.pendingDiscardRemaining <= 0) {
        return "No pending hand discard choice.";
    }

    const int tp = state_.pendingTargetPlayer;
    const std::string handZid = playerZone(tp, "hand");
    const std::string discZid = playerZone(tp, "discard");

    Pile* foundPile = nullptr;
    for (auto& p : state_.piles) {
        if (p.zone == handZid && !p.cards.empty() && p.top().uid == handCardUid) {
            foundPile = &p;
            break;
        }
    }
    if (!foundPile) {
        return "Choose a card in P" + std::to_string(tp + 1) + "'s Hand to discard.";
    }

    const Vec2 fromPos = foundPile->pos;
    CardInst disc = foundPile->top();
    foundPile->cards.pop_back();
    if (foundPile->cards.empty()) {
        const int idx = indexOf(foundPile->id);
        if (idx >= 0) {
            visuals_.erase(foundPile->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }
    }
    disc.faceUp = true;
    disc.attached = false;
    Pile& discPile = ensureZonePile(discZid, {2207.f, 1367.f}, "");
    discPile.cards.push_back(disc);
    queueCardFlight(disc, fromPos, discPile.pos, true, true);
    reflowZone(handZid);

    state_.pendingDiscardRemaining -= 1;
    if (state_.pendingDiscardRemaining <= 0) {
        state_.pendingPlayerChoiceMode = 0;
    }

    syncStateFromZones();
    const CardDef& d = catalog_.card(disc.def);
    state_.lastActionLog = "P" + std::to_string(tp + 1) + " discarded '" +
                           (d.title.empty() ? d.name : d.title) + "'" +
                           (state_.pendingDiscardRemaining > 0
                                ? (" (" + std::to_string(state_.pendingDiscardRemaining) +
                                   " more to discard).")
                                : " (Ol' Painless complete!).");
    return state_.lastActionLog;
}

std::string Table::resolveWildsSwapSlotClick(int slotIdx) {
    if (!state_.pendingWildsSwapChoice) return "No Wilds swap active.";
    if (!isWildsSlotSwappable(slotIdx)) {
        return std::string(wildsName(slotIdx)) +
               " does not have a swappable Enemy or hidden card (Wilds only; attached Goal cards cannot be switched).";
    }

    if (state_.wildsSwapFirstSlot < 0) {
        state_.wildsSwapFirstSlot = slotIdx;
        state_.lastActionLog = "Same Kind of Jungle: Selected " + std::string(wildsName(slotIdx)) +
                               " — now click a different Wilds space to switch with!";
        return state_.lastActionLog;
    }

    if (state_.wildsSwapFirstSlot == slotIdx) {
        state_.wildsSwapFirstSlot = -1;
        state_.lastActionLog = "Deselected Wilds space. Choose the first Wilds space to switch.";
        return state_.lastActionLog;
    }

    const int slotA = state_.wildsSwapFirstSlot;
    const int slotB = slotIdx;
    Pile* pA = findZonePile("wilds_" + std::to_string(slotA));
    Pile* pB = findZonePile("wilds_" + std::to_string(slotB));
    if (!pA || pA->cards.empty() || !pB || pB->cards.empty()) {
        state_.wildsSwapFirstSlot = -1;
        return "Both Wilds spaces must contain a card.";
    }

    const Vec2 posA = pA->pos;
    const Vec2 posB = pB->pos;
    CardInst cardA = pA->cards.back();
    CardInst cardB = pB->cards.back();
    pA->cards.back() = cardB;
    pB->cards.back() = cardA;

    queueCardFlight(cardA, posA, posB, cardA.faceUp, cardA.faceUp, 0);
    queueCardFlight(cardB, posB, posA, cardB.faceUp, cardB.faceUp, 1);

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    state_.pendingWildsSwapChoice = false;
    state_.wildsSwapFirstSlot = -1;

    syncStateFromZones();
    state_.lastActionLog = "Same Kind of Jungle: Switched cards between " +
                           std::string(wildsName(slotA)) + " and " +
                           std::string(wildsName(slotB)) + "!";
    recordMove("SWITCH_WILDS", "Same Kind of Jungle", 2, "wilds_" + std::to_string(slotA),
               "wilds_" + std::to_string(slotB), 1, true, state_.lastActionLog);
    return state_.lastActionLog;
}

std::string Table::resolveStatSwapChoice(std::uint32_t cardUid) {
    if (!state_.pendingStatSwapChoice) return "No stat swap choice pending.";
    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    std::optional<CardInst> target;
    for (const auto& p : state_.piles) {
        if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty() &&
            p.top().uid == cardUid) {
            target = p.top();
            break;
        }
    }
    if (!target) return "Select a card in your Hand or Played Area.";

    auto it = std::find(state_.swappedStatUids.begin(), state_.swappedStatUids.end(), cardUid);
    if (it == state_.swappedStatUids.end()) {
        state_.swappedStatUids.push_back(cardUid);
    } else {
        state_.swappedStatUids.erase(it);
    }

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    state_.pendingStatSwapChoice = false;
    syncStateFromZones();

    const CardDef& d = catalog_.card(target->def);
    int r = 0, a = 0;
    effectiveCardStats(*target, &r, &a);
    state_.lastActionLog = "The Highest Tech: Swapped printed Recruit & Attack on '" +
                           (d.title.empty() ? d.name : d.title) + "' -> now " + std::to_string(r) +
                           " * / " + std::to_string(a) + " /// this turn!";
    return state_.lastActionLog;
}

std::string Table::resolveDiscardToDeckChoice(std::uint32_t cardUid) {
    if (!state_.pendingDiscardToDeckChoice) return "No Discard-to-Deck choice pending.";
    const std::string discZid = activePlayerZone("discard");
    const std::string deckZid = activePlayerZone("deck");

    Pile* disc = findZonePile(discZid);
    if (!disc || disc->cards.empty()) {
        state_.pendingDiscardToDeckChoice = false;
        return "Your Discard pile is empty.";
    }

    int foundIdx = -1;
    for (int i = 0; i < static_cast<int>(disc->cards.size()); ++i) {
        if (disc->cards[static_cast<size_t>(i)].uid == cardUid) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx < 0) return "Card not found in your Discard pile.";

    const Vec2 fromPos = disc->pos;
    CardInst chosen = disc->cards[static_cast<size_t>(foundIdx)];
    disc->cards.erase(disc->cards.begin() + foundIdx);
    chosen.faceUp = false;
    chosen.attached = false;

    Pile& deck = ensureZonePile(deckZid, {2002.f, 1367.f}, "");
    deck.cards.push_back(chosen);
    queueCardFlight(chosen, fromPos, deck.pos, true, false);

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    state_.pendingDiscardToDeckChoice = false;
    syncStateFromZones();

    const CardDef& d = catalog_.card(chosen.def);
    state_.lastActionLog = "Position and Situation: Placed '" +
                           (d.title.empty() ? d.name : d.title) +
                           "' from Discard face-down on top of your Deck!";
    return state_.lastActionLog;
}

std::string Table::resolveGainCardChoice(const std::string& sourceZoneId) {
    if (!state_.pendingGainCardChoice) return "No Gain Card choice pending.";
    const bool isCmd = (sourceZoneId == "commanders");
    const bool isHq = (sourceZoneId.rfind("hq_", 0) == 0);
    if (!isCmd && !isHq) return "Choose the Commander stack or an HQ card with cost <= 3.";

    Pile* src = findZonePile(sourceZoneId);
    if (!src || src->cards.empty()) return "That space is empty.";

    const CardDef& d = catalog_.card(src->top().def);
    const int cost = isCmd ? 3 : std::max(0, d.cost);
    if (cost > 3) return d.name + " costs " + std::to_string(cost) + " * (max cost 3 *).";

    const Vec2 fromPos = src->pos;
    CardInst gained = src->cards.back();
    src->cards.pop_back();
    gained.faceUp = true;
    gained.attached = false;
    if (src->cards.empty()) {
        const int idx = indexOf(src->id);
        if (idx >= 0) {
            visuals_.erase(src->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }
    }

    const std::string discZid = activePlayerZone("discard");
    Pile& disc = ensureZonePile(discZid, {2207.f, 1367.f}, "");
    disc.cards.push_back(gained);
    queueCardFlight(gained, fromPos, disc.pos, true, true);

    if (isHq) {
        refillHQ();
    }

    if (state_.pendingAbilitySourceUid != 0) {
        state_.abilityUsesThisTurn[state_.pendingAbilitySourceUid] += 1;
        state_.pendingAbilitySourceUid = 0;
    }
    state_.pendingGainCardChoice = false;
    syncStateFromZones();

    state_.lastActionLog = "Partners in Crime: Gained '" + (d.title.empty() ? d.name : d.title) +
                           "' directly to your Discard pile!";
    return state_.lastActionLog;
}

void Table::toggleCoverMeCard(std::uint32_t cardUid) {
    if (!state_.pendingCoverMeChoice) return;
    auto it =
        std::find(state_.coverMeSelectedUids.begin(), state_.coverMeSelectedUids.end(), cardUid);
    if (it == state_.coverMeSelectedUids.end()) {
        state_.coverMeSelectedUids.push_back(cardUid);
    } else {
        state_.coverMeSelectedUids.erase(it);
    }
}

int Table::selectedCoverMeAttackTotal() const {
    int total = 0;
    for (std::uint32_t uid : state_.coverMeSelectedUids) {
        for (int p = 0; p < state_.playerCount; ++p) {
            if (p == state_.pendingCoverMeTargetPlayer) continue;
            const std::string hZid = playerZone(p, "hand");
            for (const auto& pile : state_.piles) {
                if (pile.zone == hZid && !pile.cards.empty() && pile.top().uid == uid) {
                    total += std::max(0, catalog_.card(pile.top().def).attack);
                }
            }
        }
    }
    return total;
}

std::string Table::resolveCoverMeChoice(bool coverAndCancelStrike, std::uint32_t /*seed*/) {
    if (!state_.pendingCoverMeChoice) return "No Cover Me! Strike choice pending.";

    const int targetP = state_.pendingCoverMeTargetPlayer;
    CardInst s = state_.pendingCoverMeCard;
    s.faceUp = true;
    s.attached = false;

    if (coverAndCancelStrike) {
        const int selAtk = selectedCoverMeAttackTotal();
        if (selAtk < 4) {
            return "Select teammate Attack cards totaling at least 4 /// Attack (currently " +
                   std::to_string(selAtk) + " ///).";
        }

        for (std::uint32_t uid : state_.coverMeSelectedUids) {
            for (int p = 0; p < state_.playerCount; ++p) {
                if (p == targetP) continue;
                const std::string hZid = playerZone(p, "hand");
                const std::string dZid = playerZone(p, "discard");
                for (size_t i = 0; i < state_.piles.size(); ++i) {
                    Pile& hp = state_.piles[i];
                    if (hp.zone == hZid && !hp.cards.empty() && hp.top().uid == uid) {
                        const Vec2 fromPos = hp.pos;
                        CardInst disc = hp.top();
                        hp.cards.pop_back();
                        if (hp.cards.empty()) {
                            visuals_.erase(hp.id);
                            state_.piles.erase(state_.piles.begin() + static_cast<std::ptrdiff_t>(i));
                        }
                        disc.faceUp = true;
                        disc.attached = false;
                        Pile& discPile = ensureZonePile(dZid, {2207.f, 1367.f}, "");
                        discPile.cards.push_back(disc);
                        queueCardFlight(disc, fromPos, discPile.pos, true, true);
                        reflowZone(hZid);
                        break;
                    }
                }
            }
        }

        state_.pendingCoverMeChoice = false;
        state_.coverMeSelectedUids.clear();
        Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
        discStrikes.cards.push_back(s);
        queueCardFlight(s, zoneCenterPos("strikes", {1620.f, 550.f}), discStrikes.pos, true, true);
        syncStateFromZones();

        state_.lastActionLog = "COVER ME! Canceled (4 DMG) — teammates discarded " +
                               std::to_string(selAtk) + " /// Attack from hand!";
        recordMove("COVER_ME_CANCELED", "Cover Me!", 1, "strikes", "discard_strikes",
                   static_cast<int>(discStrikes.cards.size()), true, state_.lastActionLog);
        return state_.lastActionLog;
    }

    // Teammates declined to cover -> target player takes the 4-damage Cover Me! Strike
    state_.pendingCoverMeChoice = false;
    state_.coverMeSelectedUids.clear();

    const std::string strikeZid = playerZone(targetP, "strikes");
    Pile& wounds = ensureZonePile(strikeZid, {335.f, layout_.boardSize.y + 185.f}, "");
    wounds.cards.push_back(s);
    queueCardFlight(s, zoneCenterPos("strikes", {1620.f, 550.f}), wounds.pos, true, true);
    syncStateFromZones();

    const PlayerState& ps = state_.players[static_cast<size_t>(targetP)];
    std::string log = "P" + std::to_string(targetP + 1) + " took COVER ME! (4 dmg -> " +
                      std::to_string(ps.strikeDamage) + "/" + std::to_string(ps.health) + " HP)!";
    if (ps.strikeDamage >= ps.health) {
        if (ps.cannotDieTurnsRemaining > 0) {
            log += " [Dutch Rare Immunity: Survived fatal damage!]";
        } else {
            eliminatePlayer(targetP, log);
            syncStateFromZones();
        }
    }
    state_.lastActionLog = log;
    recordMove("COVER_ME_TAKEN", "Cover Me!", 1, "strikes", strikeZid, ps.strikeDamage, true, log);
    return log;
}

void Table::applyPlayedCardAbility(int /*defId*/, bool /*classAlreadyPlayed*/,
                                   bool /*teamAlreadyPlayed*/, int /*otherBarracksChars*/,
                                   std::uint32_t /*seed*/, std::string& /*logOut*/) {
    // Passive stats & bonuses are now reactively computed in syncStateFromZones(),
    // and active/modal card abilities are triggered via activateCardAbility().
}

std::string Table::playCard(PileId handPileId, std::uint32_t seed) {
    if (state_.phase == TurnPhase::EnemyPhase) {
        resolveEnemyPhase(seed);
    }
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Cards can only be played during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt before playing another card.";
    }

    const std::string handZid = activePlayerZone("hand");
    const std::string playedZid = activePlayerZone("played");

    Pile* hp = find(handPileId);
    if (!hp || hp->cards.empty()) {
        return "Select a card in the active player's Hand to play.";
    }

    // If the player clicked a Coordinate card in a TEAMMATE's hand, automatically Coordinate it!
    int ownerP = -1;
    std::string_view ownerRole;
    if (parsePlayerZone(hp->zone, ownerP, ownerRole) && ownerRole == "hand" &&
        ownerP != state_.activePlayer) {
        const bool hasCoord =
            hasKeyword(catalog_.card(hp->top().def), "Coordinate") ||
            (ownerP < static_cast<int>(state_.players.size()) &&
             state_.players[static_cast<size_t>(ownerP)].allHandCoordinateThisTurn);
        if (hasCoord) {
            return coordinateCard(handPileId, seed);
        }
        return "That card belongs to Player " + std::to_string(ownerP + 1) +
               " (currently Player " + std::to_string(state_.activePlayer + 1) +
               "'s turn — teammates can lend Coordinate cards!).";
    }

    // If the card is already in Played Area and has an optional ability ready, clicking it activates the ability!
    if (hp->zone == playedZid) {
        if (canActivateCardAbility(hp->top().uid)) {
            return activateCardAbility(hp->top().uid, seed);
        }
        return catalog_.card(hp->top().def).name + " is already in your Played Area.";
    }

    if (hp->zone != handZid) {
        return "Select a card in Player " + std::to_string(state_.activePlayer + 1) +
               "'s Hand to play.";
    }

    const CardInst card = hp->top();
    const CardDef& d = catalog_.card(card.def);
    const Vec2 fromPos = hp->pos;

    hp->zone = playedZid;
    reflowZone(handZid);
    reflowZone(playedZid);
    if (const Pile* afterP = find(handPileId)) {
        queueCardFlight(card, fromPos, afterP->pos, true, true);
    }

    syncStateFromZones();

    int effRec = 0, effAtk = 0;
    effectiveCardStats(card, &effRec, &effAtk);
    std::string log = "P" + std::to_string(state_.activePlayer + 1) + " played " +
                      (d.title.empty() ? d.name : d.title) + " (" + std::to_string(effRec) +
                      " *, " + std::to_string(effAtk) + " ///).";

    // Auto-trigger purely beneficial non-modal abilities on explicit play (3, 11, 12, 67, 83, 85, 89, 91, 93)
    if (card.def == 3 || card.def == 11 || card.def == 12 || card.def == 67 || card.def == 83 ||
        card.def == 85 || card.def == 89 || card.def == 91 || card.def == 93) {
        if (canActivateCardAbility(card.uid)) {
            log += " " + activateCardAbility(card.uid, seed);
        }
    } else {
        std::string abilLabel;
        if (canActivateCardAbility(card.uid, &abilLabel, nullptr)) {
            log += " [Optional Ability Ready: " + abilLabel + "]";
        }
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("PLAY_CARD", d.title.empty() ? d.name : d.title, 1, handZid, playedZid,
               static_cast<int>(state_.accountedPlayedUids.size()), true, log);
    return log;
}

std::string Table::playAllHandCards(std::uint32_t seed) {
    if (state_.phase == TurnPhase::EnemyPhase) {
        resolveEnemyPhase(seed);
    }
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Can only play cards during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt first.";
    }

    const std::string handZid = activePlayerZone("hand");
    std::vector<PileId> handIds;
    for (const auto& p : state_.piles) {
        if (p.zone == handZid && !p.cards.empty()) handIds.push_back(p.id);
    }
    if (handIds.empty()) {
        return "No cards left in Player " + std::to_string(state_.activePlayer + 1) + "'s Hand.";
    }

    for (size_t i = 0; i < handIds.size(); ++i) {
        playCard(handIds[i], seed + static_cast<std::uint32_t>(i * 31u));
    }
    state_.lastActionLog = "P" + std::to_string(state_.activePlayer + 1) +
                           " played all hand cards -> Recruit: " +
                           std::to_string(state_.recruitPool) + " *  |  Attack: " +
                           std::to_string(state_.attackPool) + " ///";
    return state_.lastActionLog;
}

std::string Table::coordinateCard(PileId handPileId, std::uint32_t seed) {
    if (state_.phase == TurnPhase::EnemyPhase) {
        resolveEnemyPhase(seed);
    }
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Coordinate can only be used during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt first.";
    }

    Pile* hp = find(handPileId);
    if (!hp || hp->cards.empty()) {
        return "Select a Coordinate card in a player's Hand.";
    }

    int ownerP = -1;
    std::string_view ownerRole;
    if (!parsePlayerZone(hp->zone, ownerP, ownerRole) || ownerRole != "hand") {
        return "Select a Coordinate card in a player's Hand.";
    }
    if (ownerP < static_cast<int>(state_.players.size()) &&
        (!state_.players[static_cast<size_t>(ownerP)].alive ||
         state_.players[static_cast<size_t>(ownerP)].escapedInChoppa)) {
        return "Eliminated or evacuated players cannot Coordinate.";
    }
    if (state_.playerCount > 1 && ownerP == state_.activePlayer) {
        return "Rulebook p.12: In multiplayer, Coordinate lends a card from ANOTHER player's hand to the active player!";
    }
    if (ownerP < static_cast<int>(state_.players.size()) &&
        state_.players[static_cast<size_t>(ownerP)].coordinatedThisTurn) {
        return "Player " + std::to_string(ownerP + 1) +
               " has already Coordinated a card this turn (max 1 per player per turn).";
    }

    const CardInst card = hp->top();
    const CardDef& d = catalog_.card(card.def);
    const bool tempCoord =
        (ownerP < static_cast<int>(state_.players.size()) &&
         state_.players[static_cast<size_t>(ownerP)].allHandCoordinateThisTurn);
    if (!hasKeyword(d, "Coordinate") && !tempCoord) {
        return d.name + " does not have the Coordinate keyword.";
    }

    // Solo (1-Player) Call For Backup rule: requires a Call For Backup card in hand/played this turn
    if (state_.playerCount == 1 && ownerP == state_.activePlayer) {
        bool hasCallForBackupInTurn = false;
        for (const auto& tc : collectActiveTurnCards()) {
            if (tc.uid != card.uid && hasKeyword(catalog_.card(tc.def), "Call For Backup")) {
                hasCallForBackupInTurn = true;
                break;
            }
        }
        if (!hasCallForBackupInTurn) {
            return "Solo 1P Coordinate: You can only Coordinate with yourself when you have a 'Call For Backup' card in your hand/turn!";
        }
    }

    bool worldOfHurtActive = false;
    for (const auto& p : state_.piles) {
        if (!p.cards.empty() && p.top().faceUp && p.top().def == 120 &&
            (p.zone == "combat" || wildsIndexFromZone(p.zone) >= 0)) {
            worldOfHurtActive = true;
            break;
        }
    }

    state_.coordinatedCountThisTurn += 1;
    if (ownerP < static_cast<int>(state_.players.size())) {
        state_.players[static_cast<size_t>(ownerP)].coordinatedThisTurn = true;
    }

    const std::string ownerHandZid = playerZone(ownerP, "hand");
    const std::string ownerPlayedZid = playerZone(ownerP, "played");
    const std::string ownerDiscZid = playerZone(ownerP, "discard");

    std::string log;
    if (ownerP == state_.activePlayer) {
        // Solo 1P Rule: Discard the Coordinate card from hand and draw a replacement card to unlock Call For Backup!
        const Vec2 fromPos = hp->pos;
        CardInst disc = hp->top();
        hp->cards.pop_back();
        if (hp->cards.empty()) {
            const int idx = indexOf(hp->id);
            if (idx >= 0) {
                visuals_.erase(hp->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }
        // Remove the discarded Coordinate card's unplayed hand stats before drawing replacement
        disc.faceUp = true;
        disc.attached = false;
        Pile& discPile = ensureZonePile(ownerDiscZid, {2207.f, 1367.f}, "");
        discPile.cards.push_back(disc);
        queueCardFlight(disc, fromPos, discPile.pos, true, true);
        reflowZone(ownerHandZid);

        if (!worldOfHurtActive) {
            drawPlayerCards(ownerP, 1, seed + 99u, log);
            log = "[SOLO 1P COORDINATE] Discarded " + (d.title.empty() ? d.name : d.title) +
                  " and drew 1 replacement card -> Call For Backup unlocked!";
        } else {
            log = "[SOLO 1P COORDINATE] Discarded " + (d.title.empty() ? d.name : d.title) +
                  " (A World of Hurt blocked replacement draw) -> Call For Backup unlocked!";
        }
        syncStateFromZones();
        state_.lastActionLog = log;
        recordMove("COORDINATE_1P", d.title.empty() ? d.name : d.title, 1, ownerHandZid,
                   ownerDiscZid, static_cast<int>(discPile.cards.size()), true, log);
        return log;
    }

    // Multiplayer (2-5P) Rulebook p.12:
    // Teammate sets card aside in their Played Area, draws 1 replacement,
    // and Active Player gains a virtual copy in state_.coordinatedCards (providing stats, class, team, character & ability)!
    const Vec2 fromPos = hp->pos;
    hp->zone = ownerPlayedZid;
    reflowZone(ownerHandZid);
    reflowZone(ownerPlayedZid);
    if (const Pile* afterP = find(handPileId)) {
        queueCardFlight(card, fromPos, afterP->pos, true, true);
    }

    CardInst virtualCopy;
    virtualCopy.uid = state_.nextUid++;
    virtualCopy.def = card.def;
    virtualCopy.faceUp = true;
    virtualCopy.attached = false;
    state_.coordinatedCards.push_back(virtualCopy);

    if (!d.classType.empty() && !containsStr(state_.coordinatedClasses, d.classType)) {
        state_.coordinatedClasses.push_back(d.classType);
    }
    if (!d.team.empty() && !containsStr(state_.coordinatedTeams, d.team)) {
        state_.coordinatedTeams.push_back(d.team);
    }
    if (isBarracksCharacterDef(card.def) && !d.subtitle.empty() &&
        !containsStr(state_.coordinatedCharacters, d.subtitle)) {
        state_.coordinatedCharacters.push_back(d.subtitle);
    }

    if (!worldOfHurtActive) {
        drawPlayerCards(ownerP, 1, seed + 99u, log);
        log = "[COORDINATE P" + std::to_string(ownerP + 1) + " -> P" +
              std::to_string(state_.activePlayer + 1) + "] Lent " +
              (d.title.empty() ? d.name : d.title) + " (P" + std::to_string(ownerP + 1) +
              " drew 1 replacement)!";
    } else {
        log = "[COORDINATE P" + std::to_string(ownerP + 1) + " -> P" +
              std::to_string(state_.activePlayer + 1) + "] Lent " +
              (d.title.empty() ? d.name : d.title) +
              " (A World of Hurt blocked replacement draw)!";
    }

    syncStateFromZones();

    // Auto-trigger purely beneficial instant ability on the coordinated copy if applicable
    if (virtualCopy.def == 3 || virtualCopy.def == 11 || virtualCopy.def == 12 ||
        virtualCopy.def == 67 || virtualCopy.def == 83 || virtualCopy.def == 85 ||
        virtualCopy.def == 89 || virtualCopy.def == 91 || virtualCopy.def == 93) {
        if (canActivateCardAbility(virtualCopy.uid)) {
            log += " " + activateCardAbility(virtualCopy.uid, seed + 53u);
        }
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("COORDINATE", d.title.empty() ? d.name : d.title, 1, ownerHandZid, ownerPlayedZid,
               1, true, log);
    return log;
}

std::string Table::recruitCard(PileId targetPileId, std::uint32_t /*seed*/) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Recruiting is only allowed during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt first.";
    }
    Pile* p = find(targetPileId);
    if (!p || p->cards.empty()) return "No card to recruit.";

    const bool isCommander = (p->zone == "commanders");
    const bool isHQ = (p->zone.rfind("hq_", 0) == 0);
    if (!isCommander && !isHQ) {
        return "You can only recruit from the 5 HQ slots or the Commander stack.";
    }

    const CardDef& d = catalog_.card(p->top().def);
    const int cost = isCommander ? 3 : std::max(0, d.cost);

    if (state_.recruitPool < cost) {
        state_.lastActionLog = "Need " + std::to_string(cost) + " Recruit * to recruit " +
                               (isCommander ? "Commander" : d.name) + " (have " +
                               std::to_string(state_.recruitPool) + " *).";
        return state_.lastActionLog;
    }

    std::string autoPlayLog;
    autoPlayHandCardsForRecruit(cost, autoPlayLog);

    const std::string srcZone = p->zone;
    const Vec2 fromPos = p->pos;
    state_.recruitPool -= cost;
    CardInst recruited = p->cards.back();
    p->cards.pop_back();
    recruited.faceUp = true;
    recruited.attached = false;

    if (p->cards.empty()) {
        const int idx = indexOf(p->id);
        if (idx >= 0) {
            visuals_.erase(p->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }
    }

    const std::string discZid = activePlayerZone("discard");
    Pile& discard = ensureZonePile(discZid, {2207.f, 1367.f}, "");
    discard.cards.push_back(recruited);
    queueCardFlight(recruited, fromPos, discard.pos, true, true);
    const int discCount = static_cast<int>(discard.cards.size());

    // Rulebook p.9: Immediately refill any empty HQ space with the top card of the Barracks
    if (isHQ) {
        refillHQ();
    }

    state_.lastActionLog = "P" + std::to_string(state_.activePlayer + 1) + " recruited " +
                           catalog_.card(recruited.def).name + " for " + std::to_string(cost) +
                           " * -> placed in P" + std::to_string(state_.activePlayer + 1) +
                           " Discard (HQ refilled)." + autoPlayLog;
    recordMove("RECRUIT_CARD", catalog_.card(recruited.def).name, 1, srcZone, discZid, discCount,
               true, state_.lastActionLog);
    return state_.lastActionLog;
}

std::string Table::scanWilds(PileId targetPileId, std::uint32_t seed) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Scanning is only allowed during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt first.";
    }
    Pile* p = find(targetPileId);
    if (!p || p->cards.empty()) return "No card in that space.";

    const int wIdx = wildsIndexFromZone(p->zone);
    if (wIdx < 0) return "Only hidden cards in The Wilds can be scanned.";
    if (p->top().faceUp) return "That space is already face-up.";

    const int cost = scanCostForZone(p->zone);
    bool usedFreeScan = false;
    std::string autoPlayLog;
    if (state_.freeScansAvailable > 0) {
        autoPlayHandCardMatchingDef(2, autoPlayLog); // Dug In Like An Alabama Tick
        state_.freeScansAvailable -= 1;
        usedFreeScan = true;
    } else if (state_.attackPool >= cost) {
        autoPlayHandCardsForAttack(cost, autoPlayLog);
        state_.attackPool -= cost;
    } else {
        state_.lastActionLog = "Need " + std::to_string(cost) + " Attack /// to scan " +
                               wildsName(wIdx) + " (have " + std::to_string(state_.attackPool) +
                               " ///).";
        return state_.lastActionLog;
    }

    const int scannedDef = p->top().def;
    const std::string scannedZone = p->zone;
    p->cards.back().faceUp = true;
    visual(p->id).flipT = 0.f;

    std::string log = usedFreeScan
                          ? ("Scanned " + std::string(wildsName(wIdx)) + " (Free Scan)!" + autoPlayLog)
                          : ("Scanned " + std::string(wildsName(wIdx)) + " for " +
                             std::to_string(cost) + " Attack ///!" + autoPlayLog);
    onCardRevealedInWilds(p->id, wIdx, seed, log);
    state_.lastActionLog = log;
    recordMove("SCAN_WILDS", catalog_.card(scannedDef).name, 1, scannedZone, scannedZone, 1, true,
               log);
    return log;
}

std::string Table::completeObjective2() {
    if (!canCompleteObjective2()) {
        state_.lastActionLog =
            "To complete Objective 2: Reveal Defensive Position (to Operations), clear Hills, and spend 6 * Recruit.";
        return state_.lastActionLog;
    }

    std::string autoPlayLog;
    autoPlayHandCardsForRecruit(6, autoPlayLog);
    state_.recruitPool -= 6;
    state_.defensivePositionAttachedToHills = true;

    // Move Defensive Position (def 107) from Operations ("testing") and attach it to Hills ("wilds_0")!
    std::optional<CardInst> defPosCard;
    Vec2 fromPos{1112.f, 185.f};
    for (size_t i = 0; i < state_.piles.size(); ++i) {
        Pile& ops = state_.piles[i];
        if (ops.zone == "testing" && !ops.cards.empty() && ops.top().def == 107) {
            fromPos = ops.pos;
            defPosCard = ops.cards.back();
            ops.cards.pop_back();
            if (ops.cards.empty()) {
                visuals_.erase(ops.id);
                state_.piles.erase(state_.piles.begin() + static_cast<std::ptrdiff_t>(i));
            }
            break;
        }
    }
    if (defPosCard) {
        attachCardToWildsSlot(0, *defPosCard);
        queueCardFlight(*defPosCard, fromPos, zoneCenterPos("wilds_0", {411.f, 550.f}), true, true);
    }

    advanceObjectiveStack();
    state_.lastActionLog =
        "OBJECTIVE 2 COMPLETE! Defensive Position attached to Hills (auto-scans Hills). Now on Objective 3: Get to the Choppa!" +
        autoPlayLog;
    recordMove("COMPLETE_OBJ_2", "Defensive Position", 1, "testing", "wilds_0", 1, true,
               state_.lastActionLog);
    return state_.lastActionLog;
}

std::string Table::fightEnemy(PileId targetPileId, std::uint32_t seed) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Fighting enemies is only allowed during the Action Phase.";
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt first.";
    }
    Pile* p = find(targetPileId);
    if (!p || p->cards.empty() || !p->top().faceUp) {
        return "Target must be a face-up Enemy or Goal in the Wilds or Combat Zone.";
    }

    const int wIdx = wildsIndexFromZone(p->zone);
    const bool inCombat = (p->zone == "combat");
    if (wIdx < 0 && !inCombat) {
        return "You can only fight enemies in the Wilds or Combat Zone.";
    }

    const CardInst target = p->top();
    const CardDef& d = catalog_.card(target.def);

    // Objective 1 Goal: Guerrilla Camp (def 102) can only be wiped out for 1 Attack when its space is clear!
    if (target.def == 102) {
        if (wIdx < 0 || !isWildsSpaceClear(wIdx) || p->cards.size() > 1) {
            state_.lastActionLog =
                "Clear any other card in " + std::string(wildsName(wIdx)) +
                " first before spending 1 /// Attack to wipe out the Guerrilla Camp!";
            return state_.lastActionLog;
        }
    }

    // Attached Defensive Position on Hills cannot be fought as an enemy
    if (target.def == 107 && target.attached) {
        state_.lastActionLog =
            "Defensive Position is attached to Hills (auto-scans incoming hidden cards).";
        return state_.lastActionLog;
    }

    // Objective 3 Minor Goal: The Choppa! (def 114, cost 3 * Recruit in Combat Zone)
    if (target.def == 114) {
        if (!inCombat) {
            state_.lastActionLog =
                "The Choppa! can only be boarded once it enters the Combat Zone!";
            return state_.lastActionLog;
        }
        const int choppaCost = std::max(0, d.cost); // 3 *
        if (state_.recruitPool < choppaCost) {
            state_.lastActionLog = "Need " + std::to_string(choppaCost) +
                                   " Recruit * to evacuate aboard The Choppa! (have " +
                                   std::to_string(state_.recruitPool) + " *).";
            return state_.lastActionLog;
        }

        std::string autoPlayLog;
        autoPlayHandCardsForRecruit(choppaCost, autoPlayLog);
        state_.recruitPool -= choppaCost;

        const int ap = state_.activePlayer;
        std::string log = "P" + std::to_string(ap + 1) + " paid " + std::to_string(choppaCost) +
                          " * Recruit to board The Choppa!!" + autoPlayLog;

        int enemiesInCombat = 0;
        for (const auto& cp : state_.piles) {
            if (cp.zone == "combat" && !cp.cards.empty() && cp.top().def != 114) {
                ++enemiesInCombat;
            }
        }
        for (int i = 0; i < enemiesInCombat; ++i) {
            drawStrikeAgainstPlayer(ap, seed + static_cast<std::uint32_t>(i), log);
            if (state_.phase == TurnPhase::Defeat ||
                !state_.players[static_cast<size_t>(ap)].alive) {
                break;
            }
        }

        if (state_.phase != TurnPhase::Defeat &&
            ap < static_cast<int>(state_.players.size()) &&
            state_.players[static_cast<size_t>(ap)].alive) {
            state_.players[static_cast<size_t>(ap)].escapedInChoppa = true;

            // Place the player's Avatar beneath The Choppa!
            if (Pile* av = findZonePile(playerZone(ap, "avatar")); av && !av->cards.empty()) {
                const Vec2 avPos = av->pos;
                CardInst avCard = av->cards.back();
                av->cards.pop_back();
                if (av->cards.empty()) {
                    const int avIdx = indexOf(av->id);
                    if (avIdx >= 0) {
                        visuals_.erase(av->id);
                        state_.piles.erase(state_.piles.begin() + avIdx);
                    }
                }
                avCard.faceUp = true;
                avCard.attached = true;
                if (Pile* choppaPile = find(targetPileId)) {
                    choppaPile->cards.insert(choppaPile->cards.begin(), avCard);
                    queueCardFlight(avCard, avPos, choppaPile->pos, true, true);
                }
            }

            bool allLivingEscaped = true;
            int escapedCount = 0;
            for (const auto& ps : state_.players) {
                if (ps.alive) {
                    if (ps.escapedInChoppa) {
                        ++escapedCount;
                    } else {
                        allLivingEscaped = false;
                    }
                }
            }

            if (allLivingEscaped) {
                state_.phase = TurnPhase::Victory;
                log += " [MINOR VICTORY! All surviving operatives (" +
                       std::to_string(std::max(1, escapedCount)) +
                       ") evacuated aboard The Choppa! (Score: " +
                       std::to_string(std::max(1, escapedCount)) + " pts)]";
            } else {
                log += " [P" + std::to_string(ap + 1) +
                       " is safely aboard The Choppa! Remaining operatives must also board The Choppa! to complete Objective 3!]";
            }
        }
        syncStateFromZones();
        state_.lastActionLog = log;
        recordMove("BOARD_CHOPPA", d.name, 1, "combat", "combat", 1, true, log);
        return log;
    }

    // Rulebook p.12: Final Enemy (One Ugly Mother def 121) can only be fought after Objective 2 is complete
    if (target.def == 121 && state_.currentObjective < 3) {
        state_.lastActionLog =
            "Rulebook p.12: You cannot fight the Final Enemy (One Ugly Mother) until Objective 2 is completed!";
        return state_.lastActionLog;
    }

    const int reqAtk = effectiveEnemyAttack(*p);
    if (reqAtk < 0) {
        return d.name + " cannot be fought directly.";
    }

    // Special case: Something in Those Trees (def 118) is fought with Recruit Points outside River (wilds_2)
    const bool useRecruitToFight = (target.def == 118 && p->zone != "wilds_2");
    std::string autoPlayLog;
    if (useRecruitToFight) {
        if (state_.recruitPool < reqAtk) {
            state_.lastActionLog = d.name + " outside the River requires " +
                                   std::to_string(reqAtk) + " Recruit * to fight!";
            return state_.lastActionLog;
        }
        autoPlayHandCardsForRecruit(reqAtk, autoPlayLog);
        state_.recruitPool -= reqAtk;
    } else {
        if (state_.attackPool < reqAtk) {
            state_.lastActionLog = "Need " + std::to_string(reqAtk) + " Attack /// to defeat " +
                                   d.name + " (have " + std::to_string(state_.attackPool) + " ///).";
            return state_.lastActionLog;
        }
        autoPlayHandCardsForAttack(reqAtk, autoPlayLog);
        state_.attackPool -= reqAtk;
    }

    p = find(targetPileId);
    if (!p || p->cards.empty()) return "Target pile no longer exists.";

    std::string log = "Defeated " + d.name + " (" + std::to_string(reqAtk) +
                      (useRecruitToFight ? " *" : " ///") + " spent)!" + autoPlayLog;

    // Objective 1 Goal: Guerrilla Camp (def 102) -> Discard to Discarded Events ("discard_hazards")!
    if (target.def == 102) {
        const std::string fromWilds = p->zone;
        const Vec2 fromPos = p->pos;
        CardInst wiped = target;
        wiped.attached = false;
        wiped.faceUp = true;

        p->cards.pop_back();
        if (p->cards.empty()) {
            const int idx = indexOf(p->id);
            if (idx >= 0) {
                visuals_.erase(p->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }
        Pile& disc = ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
        disc.cards.push_back(wiped);
        queueCardFlight(wiped, fromPos, disc.pos, true, true);
        if (state_.currentObjective == 1) {
            advanceObjectiveStack();
            log = "OBJECTIVE 1 COMPLETE! Wiped out the Guerrilla Camp (1 /// Attack on clear " +
                  std::string(wildsName(wIdx)) +
                  ") -> Discarded Events! Now on Objective 2: Flares, Frags, and Claymores." +
                  autoPlayLog;
        } else {
            log = "Wiped out the Guerrilla Camp (1 /// Attack) -> moved to Discarded Events!" +
                  autoPlayLog;
        }
        state_.lastActionLog = log;
        recordMove("WIPE_OUT_CAMP", d.name, 1, fromWilds, "discard_hazards",
                   static_cast<int>(disc.cards.size()), true, log);
        return log;
    }

    // Final Enemy: One Ugly Mother (def 121)
    if (target.def == 121) {
        syncStateFromZones();
        if (state_.marksCount > 0) {
            // "Ongoing: If you would kill this Enemy while Marked, destroy a Mark instead."
            const std::string marksZid = activePlayerZone("marks");
            if (Pile* marks = findZonePile(marksZid); marks && !marks->cards.empty()) {
                const Vec2 mFrom = marks->pos;
                CardInst m = marks->cards.back();
                marks->cards.pop_back();
                if (marks->cards.empty()) {
                    const int mIdx = indexOf(marks->id);
                    if (mIdx >= 0) {
                        visuals_.erase(marks->id);
                        state_.piles.erase(state_.piles.begin() + mIdx);
                    }
                }
                Pile& disc =
                    ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
                disc.cards.push_back(m);
                queueCardFlight(m, mFrom, disc.pos, true, true);
            } else if (state_.activePlayer < static_cast<int>(state_.players.size())) {
                state_.players[static_cast<size_t>(state_.activePlayer)].manualMarksOffset -= 1;
            }
            syncStateFromZones();
            log = "Fought One Ugly Mother while Marked -> destroyed 1 Mark (" +
                  std::to_string(state_.marksCount) +
                  " remaining)! Attack again with 0 Marks to kill him!" + autoPlayLog;
            state_.lastActionLog = log;
            return log;
        }

        // Check if Three Hundred City Blocks (def 115) is attached to One Ugly Mother!
        bool hasThreeHundredAttached = state_.threeHundredCityBlocksAttachedToFinalEnemy;
        for (const auto& c : p->cards) {
            if (c.def == 115) {
                hasThreeHundredAttached = true;
                break;
            }
        }

        const Vec2 fromPos = p->pos;
        std::vector<CardInst> attached115;
        for (const auto& c : p->cards) {
            if (c.def == 115) attached115.push_back(c);
        }
        const int idx = indexOf(p->id);
        if (idx >= 0) {
            visuals_.erase(p->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }

        Pile& deadPrey = ensureZonePile("dead_prey", {1950.f, 220.f}, "");
        CardInst deadBoss = target;
        deadBoss.attached = false;
        deadPrey.cards.push_back(deadBoss);
        queueCardFlight(deadBoss, fromPos, deadPrey.pos, true, true);

        if (!attached115.empty()) {
            Pile& discHaz =
                ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
            for (auto c115 : attached115) {
                c115.attached = false;
                c115.faceUp = true;
                discHaz.cards.push_back(c115);
            }
        }
        state_.threeHundredCityBlocksAttachedToFinalEnemy = false;

        if (hasThreeHundredAttached) {
            // "Ongoing: When you kill the Final Enemy, you must pay 4 * or die."
            if (state_.recruitPool >= 4) {
                std::string extraRecLog;
                autoPlayHandCardsForRecruit(4, extraRecLog);
                state_.recruitPool -= 4;
                log += " [Three Hundred City Blocks: Paid 4 * Recruit to escape the blast radius!]" +
                       extraRecLog;
            } else {
                log += " [Three Hundred City Blocks: Could not pay 4 * Recruit -> caught in the blast!]";
                eliminatePlayer(state_.activePlayer, log);
            }
        }

        int livingPlayers = 0;
        for (const auto& ps : state_.players) {
            if (ps.alive) ++livingPlayers;
        }
        if (livingPlayers > 0 && state_.phase != TurnPhase::Defeat) {
            state_.phase = TurnPhase::Victory;
            log += " MAJOR VICTORY! You killed One Ugly Mother and completed Objective 3! (Score: " +
                   std::to_string(5 + std::max(1, livingPlayers)) + " pts)";
        }
        state_.lastActionLog = log;
        return state_.lastActionLog;
    }

    // Remove killed enemy from board (preserving any attached card underneath!) and move to Dead Enemies
    const std::string killedFromZone = inCombat ? "combat" : ("wilds_" + std::to_string(wIdx));
    const Vec2 fromPos = p->pos;
    p->cards.pop_back();
    if (p->cards.empty()) {
        const int idx = indexOf(p->id);
        if (idx >= 0) {
            visuals_.erase(p->id);
            state_.piles.erase(state_.piles.begin() + idx);
        }
    }
    CardInst deadEnemy = target;
    deadEnemy.attached = false;
    Pile& deadPrey = ensureZonePile("dead_prey", {1950.f, 220.f}, "");
    deadPrey.cards.push_back(deadEnemy);
    queueCardFlight(deadEnemy, fromPos, deadPrey.pos, true, true);
    const int deadPreyCount = static_cast<int>(deadPrey.cards.size());

    // Mac 1 Rare (Gonna Have Me Some Fun): Draw 2 cards each time you kill an Enemy this turn
    if (state_.drawCardsOnKill > 0) {
        drawPlayerCards(state_.activePlayer, state_.drawCardsOnKill, seed + 55u, log);
        log += " [Gonna Have Me Some Fun: Drew " + std::to_string(state_.drawCardsOnKill) +
               " cards!]";
    }

    // Helper lambda to open mandatory cost>=1 kill modal for Nothing On Earth (117) and A Fitting Demise (210)
    auto triggerMandatoryCost1Kill = [&](int killReq, const std::string& reasonLabel) {
        if (killReq <= 0) return;
        const std::string handZid = activePlayerZone("hand");
        const std::string playedZid = activePlayerZone("played");
        int eligibleCount = 0;
        for (const auto& cp : state_.piles) {
            if ((cp.zone == handZid || cp.zone == playedZid) && !cp.cards.empty() &&
                !isCardKilledInPlayed(cp.top().uid) &&
                catalog_.card(cp.top().def).cost >= 1) {
                ++eligibleCount;
            }
        }
        if (eligibleCount <= 0) {
            log += " [" + reasonLabel + ": You have no cards costing 1 or more in Hand/Played Area.]";
            return;
        }
        state_.pendingKillChoice = true;
        state_.pendingKillCount = std::min(killReq, eligibleCount);
        state_.pendingKillMinCost = 1;
        state_.pendingKillMandatory = true;
        state_.pendingKillStayUntilCleanup = false;
        state_.pendingAbilitySourceUid = 0;
        log += " [" + reasonLabel + ": Choose " + std::to_string(state_.pendingKillCount) +
               " of your card(s) with cost >= 1 to kill!]";
    };

    // --- Enemy Death Triggers ---
    switch (target.def) {
    case 103: { // Val Verde Guerrilla: Move face-up Guerrilla Camp 1 space left, or draw a Strike if not in Wilds
        bool campInWilds = false;
        for (int s = 0; s <= 4; ++s) {
            const std::string zid = "wilds_" + std::to_string(s);
            Pile* wp = findZonePile(zid);
            if (!wp) continue;
            for (size_t ci = 0; ci < wp->cards.size(); ++ci) {
                if (wp->cards[ci].def == 102 && wp->cards[ci].faceUp) {
                    campInWilds = true;
                    const Vec2 campFrom = wp->pos;
                    CardInst camp = wp->cards[ci];
                    wp->cards.erase(wp->cards.begin() + static_cast<std::ptrdiff_t>(ci));
                    if (wp->cards.empty()) {
                        const int idx = indexOf(wp->id);
                        if (idx >= 0) {
                            visuals_.erase(wp->id);
                            state_.piles.erase(state_.piles.begin() + idx);
                        }
                    }
                    if (s > 0) {
                        attachCardToWildsSlot(s - 1, camp);
                        queueCardFlight(camp, campFrom,
                                        zoneCenterPos("wilds_" + std::to_string(s - 1)), true, true);
                        log += " [Death: Guerrilla Camp moved left from " +
                               std::string(wildsName(s)) + " to " +
                               std::string(wildsName(s - 1)) + "!]";
                    } else {
                        // Pushed left from Hills (wilds_0) into Combat Zone -> Discarded Events & Objective 1 Complete!
                        camp.attached = false;
                        camp.faceUp = true;
                        Pile& disc = ensureZonePile("discard_hazards", {2150.f, 545.f},
                                                    "Discarded Hazards/Events");
                        disc.cards.push_back(camp);
                        queueCardFlight(camp, campFrom, disc.pos, true, true);
                        if (state_.currentObjective == 1) {
                            advanceObjectiveStack();
                            log += " [Death: Guerrilla Camp moved left from Hills into Combat Zone -> Discarded Events & OBJECTIVE 1 COMPLETE!]";
                        } else {
                            log += " [Death: Guerrilla Camp moved left from Hills into Combat Zone -> Discarded Events!]";
                        }
                    }
                    break;
                }
            }
            if (campInWilds) break;
        }
        if (!campInWilds) {
            log += " [Death: Guerrilla Camp not face-up in Wilds -> Draw a Strike!]";
            drawStrikeAgainstPlayer(state_.activePlayer, seed + 71u, log);
        }
        break;
    }
    case 117: // Nothing On Earth: Death: For each Mark you have, kill one of your cards that costs 1 or more
        syncStateFromZones();
        if (state_.marksCount > 0) {
            triggerMandatoryCost1Kill(state_.marksCount, "Nothing On Earth Death");
        }
        break;
    case 208: // Young Blood: Defensive Adaptations (unless Tech in hand/played, draw 2 fewer cards)
        if (!containsStr(state_.roundHandClasses, "Tech") &&
            !containsStr(state_.playedClasses, "Tech")) {
            if (state_.activePlayer < static_cast<int>(state_.players.size())) {
                state_.players[static_cast<size_t>(state_.activePlayer)].nextHandSizePenalty += 2;
            }
            state_.nextHandSizePenalty += 2;
            log += " [Death: No Tech card in hand/turn -> draw 2 fewer cards in Cleanup!]";
        }
        break;
    case 209: // Young Blood: The Eyes of the Demon (unless Survival in hand/played, draw a Strike)
        if (!containsStr(state_.roundHandClasses, "Survival") &&
            !containsStr(state_.playedClasses, "Survival")) {
            log += " [Death: No Survival card in hand/turn -> Draw a Strike!]";
            drawStrikeAgainstPlayer(state_.activePlayer, seed + 73u, log);
        }
        break;
    case 210: // Young Blood: A Fitting Demise (unless Leadership in hand/played, kill one of your cards costing >= 1)
        if (!containsStr(state_.roundHandClasses, "Leadership") &&
            !containsStr(state_.playedClasses, "Leadership")) {
            triggerMandatoryCost1Kill(1, "A Fitting Demise Death");
        }
        break;
    case 211: // Young Blood: Something Out There Waiting For Us (unless Strength in hand/played, add Enemy)
        if (!containsStr(state_.roundHandClasses, "Strength") &&
            !containsStr(state_.playedClasses, "Strength")) {
            if (Pile* prey = findZonePile("prey"); prey && !prey->cards.empty()) {
                CardInst c = prey->cards.back();
                prey->cards.pop_back();
                c.faceUp = false;
                c.attached = false;
                log += " [Death: No Strength card in hand/turn -> Added Enemy to Wilds!]";
                pushCardIntoWildsSlot(4, c, seed + 79u, log);
            }
        }
        break;
    case 212: // Young Blood: Fun and Games (unless Intel in hand/played, turn unattached Wilds enemies face-down)
        if (!containsStr(state_.roundHandClasses, "Intel") &&
            !containsStr(state_.playedClasses, "Intel")) {
            for (int s = 0; s <= 4; ++s) {
                if (Pile* wp = findZonePile("wilds_" + std::to_string(s));
                    wp && !wp->cards.empty() && !wp->top().attached && wp->top().faceUp &&
                    wp->top().def != 102) {
                    wp->cards.back().faceUp = false;
                    visual(wp->id).flipT = 0.f;
                }
            }
            log += " [Death: No Intel card in hand/turn -> Wilds enemies turned face-down!]";
        }
        break;
    default:
        break;
    }

    syncStateFromZones();
    state_.lastActionLog = log;
    recordMove("FIGHT_ENEMY", d.name, 1, killedFromZone, "dead_prey", deadPreyCount, true, log);
    return log;
}

void Table::eliminatePlayer(int playerIdx, std::string& logOut) {
    if (playerIdx < 0 || playerIdx >= static_cast<int>(state_.players.size())) return;
    PlayerState& ps = state_.players[static_cast<size_t>(playerIdx)];
    if (!ps.alive) return;

    if (ps.cannotDieTurnsRemaining > 0) {
        logOut += " [Dutch Rare Immunity: P" + std::to_string(playerIdx + 1) +
                  " cannot die — death effect ignored!]";
        return;
    }

    ps.alive = false;
    ps.activeImmunityCardUid = 0;

    // Flip Avatar face-down to mark KIA
    if (Pile* av = findZonePile(playerZone(playerIdx, "avatar"))) {
        for (auto& c : av->cards) c.faceUp = false;
        visual(av->id).flipT = 0.f;
    }

    // Rulebook p.15: Move fallen player's Strikes to Discarded Strikes, Marks to Discarded Hazards,
    // and Hand/Played/Deck/Discard cards to Dead Characters
    bool hasWholeNewLevelOfPain = false;
    std::vector<CardInst> toDiscStrikes, toDiscHazards, toDeadChars;
    const std::string strikeZid = playerZone(playerIdx, "strikes");
    const std::string markZid = playerZone(playerIdx, "marks");
    const std::string handZid = playerZone(playerIdx, "hand");
    const std::string playedZid = playerZone(playerIdx, "played");
    const std::string deckZid = playerZone(playerIdx, "deck");
    const std::string discZid = playerZone(playerIdx, "discard");

    for (auto& p : state_.piles) {
        if (p.zone == strikeZid) {
            for (auto c : p.cards) {
                if (c.def == 187) hasWholeNewLevelOfPain = true;
                c.faceUp = true;
                toDiscStrikes.push_back(c);
            }
            p.cards.clear();
        } else if (p.zone == markZid) {
            for (auto c : p.cards) {
                c.faceUp = true;
                toDiscHazards.push_back(c);
            }
            p.cards.clear();
        } else if (p.zone == handZid || p.zone == playedZid || p.zone == deckZid ||
                   p.zone == discZid) {
            for (auto c : p.cards) {
                c.faceUp = true;
                toDeadChars.push_back(c);
            }
            p.cards.clear();
        }
    }

    if (!toDiscStrikes.empty()) {
        Pile& ds = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
        ds.cards.insert(ds.cards.end(), toDiscStrikes.begin(), toDiscStrikes.end());
    }
    if (!toDiscHazards.empty()) {
        Pile& dh = ensureZonePile("discard_hazards", {2150.f, 545.f}, "");
        dh.cards.insert(dh.cards.end(), toDiscHazards.begin(), toDiscHazards.end());
    }
    if (!toDeadChars.empty()) {
        Pile& dc = ensureZonePile("dead_characters", {1846.f, 871.f}, "");
        dc.cards.insert(dc.cards.end(), toDeadChars.begin(), toDeadChars.end());
    }

    reflowZone(handZid);
    reflowZone(playedZid);

    bool anyAlive = false;
    bool allRemainingEscaped = true;
    int escapedCount = 0;
    for (const auto& other : state_.players) {
        if (other.alive) {
            anyAlive = true;
            if (other.escapedInChoppa) {
                ++escapedCount;
            } else {
                allRemainingEscaped = false;
            }
        }
    }

    const std::string avName = catalog_.card(ps.avatarDefId).name;
    if (!anyAlive) {
        state_.phase = TurnPhase::Defeat;
        logOut += " [FATAL DAMAGE: Player " + std::to_string(playerIdx + 1) + " (" + avName +
                  ") KIA — All Operatives Fallen. Predator Wins!]";
    } else if (allRemainingEscaped && escapedCount > 0) {
        state_.phase = TurnPhase::Victory;
        logOut += " [OPERATIVE KIA: Player " + std::to_string(playerIdx + 1) + " (" + avName +
                  ") has fallen — all surviving operatives (" + std::to_string(escapedCount) +
                  ") are aboard The Choppa! MINOR VICTORY!]";
    } else {
        logOut += " [OPERATIVE KIA: Player " + std::to_string(playerIdx + 1) + " (" + avName +
                  ") has fallen! Remaining operatives fight on!]";
    }

    // Issue A7 — A Whole New Level Of Pain (def 187): "When you die, kill each other player."
    if (hasWholeNewLevelOfPain && state_.phase != TurnPhase::Defeat) {
        logOut += " [A Whole New Level Of Pain: P" + std::to_string(playerIdx + 1) +
                  "'s death triggers every other player's death!]";
        for (int otherIdx = 0; otherIdx < state_.playerCount; ++otherIdx) {
            if (otherIdx != playerIdx &&
                otherIdx < static_cast<int>(state_.players.size()) &&
                state_.players[static_cast<size_t>(otherIdx)].alive &&
                !state_.players[static_cast<size_t>(otherIdx)].escapedInChoppa) {
                eliminatePlayer(otherIdx, logOut);
            }
        }
    }
}

void Table::drawStrikeAgainstPlayer(int playerIdx, std::uint32_t seed, std::string& logOut) {
    if (state_.phase == TurnPhase::Defeat || state_.phase == TurnPhase::Victory) return;
    if (playerIdx < 0 || playerIdx >= static_cast<int>(state_.players.size())) return;
    if (!state_.players[static_cast<size_t>(playerIdx)].alive ||
        state_.players[static_cast<size_t>(playerIdx)].escapedInChoppa) {
        return;
    }

    if (playerIdx == state_.activePlayer && state_.avoidStrikesAvailable > 0) {
        std::string autoPlayLog;
        autoPlayHandCardMatchingDef(4, autoPlayLog);  // Real Nasty Habit
        autoPlayHandCardMatchingDef(65, autoPlayLog); // In the Shadows
        state_.avoidStrikesAvailable -= 1;
        logOut += " [Avoided Strike (-1 Strike Draw)!]" + autoPlayLog;
        return;
    }

    Pile* strikes = findZonePile("strikes");
    if (!strikes || strikes->cards.empty()) {
        // Rulebook p.15: When Strike deck runs out, immediately shuffle discarded strikes
        Pile* discStrikes = findZonePile("discard_strikes");
        if (!discStrikes || discStrikes->cards.empty()) return;

        std::mt19937 rng(seed);
        std::vector<CardInst> recycled = std::move(discStrikes->cards);
        discStrikes->cards.clear();
        for (auto& c : recycled) {
            c.faceUp = false;
            c.attached = false;
        }
        std::shuffle(recycled.begin(), recycled.end(), rng);

        Pile& newStrikes = ensureZonePile("strikes", {1620.f, 550.f}, "");
        newStrikes.cards = std::move(recycled);
        strikes = &newStrikes;
        logOut += " [Reshuffled Discarded Strikes!]";
    }

    if (!strikes || strikes->cards.empty()) return;

    const Vec2 strikeDeckPos = strikes->pos;
    CardInst s = strikes->cards.back();
    strikes->cards.pop_back();
    s.faceUp = true;
    s.attached = false;

    const CardDef& d = catalog_.card(s.def);
    const int dmg = std::max(0, d.attack);

    if (s.def == 184) {
        // Close Call (0 Damage) -> goes to Discarded Strikes
        Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
        discStrikes.cards.push_back(s);
        queueCardFlight(s, strikeDeckPos, discStrikes.pos, false, true);
        logOut += " [Strike: Close Call (0 dmg)!]";

        // One at a Time (def 110) in Combat Zone forces another Strike on Close Call
        for (const auto& cp : state_.piles) {
            if (cp.zone == "combat" && !cp.cards.empty() && cp.top().def == 110) {
                logOut += " [One at a Time: Draw another Strike!]";
                drawStrikeAgainstPlayer(playerIdx, seed + 13u, logOut);
                break;
            }
        }
        return;
    }

    // Issue A4 — Cover Me! (def 191, 4 Damage):
    // "The other players may discard cards with a printed total of at least 4 [Attack] to cancel this Strike."
    // Opens one-time interactive Cover Me! modal so teammates can choose which Attack cards (>= 4 ///) to discard or decline!
    if (s.def == 191 && state_.playerCount > 1) {
        int totalOtherAtk = 0;
        for (int otherP = 0; otherP < state_.playerCount; ++otherP) {
            if (otherP == playerIdx || otherP >= static_cast<int>(state_.players.size()) ||
                !state_.players[static_cast<size_t>(otherP)].alive ||
                state_.players[static_cast<size_t>(otherP)].escapedInChoppa) {
                continue;
            }
            const std::string otherHandZid = playerZone(otherP, "hand");
            for (const auto& hp : state_.piles) {
                if (hp.zone != otherHandZid) continue;
                for (const auto& c : hp.cards) {
                    const int cAtk = catalog_.card(c.def).attack;
                    if (cAtk > 0) totalOtherAtk += cAtk;
                }
            }
        }

        if (totalOtherAtk >= 4) {
            state_.pendingCoverMeChoice = true;
            state_.pendingCoverMeTargetPlayer = playerIdx;
            state_.pendingCoverMeCard = s;
            state_.coverMeSelectedUids.clear();
            logOut += " [STRIKE — COVER ME! (4 DMG): Teammates may choose Attack cards totaling >= 4 /// to discard and cancel this Strike!]";
            return;
        }
    }

    // Issue A5 — Sacrifice (def 192, 5 Damage): "Another player may take this Strike instead of you."
    // In multiplayer with at least one other living player, prompt the player to choose who receives Sacrifice!
    if (s.def == 192 && state_.playerCount > 1) {
        bool anyOtherAlive = false;
        for (int otherP = 0; otherP < state_.playerCount; ++otherP) {
            if (otherP != playerIdx && otherP < static_cast<int>(state_.players.size()) &&
                state_.players[static_cast<size_t>(otherP)].alive &&
                !state_.players[static_cast<size_t>(otherP)].escapedInChoppa) {
                anyOtherAlive = true;
                break;
            }
        }
        if (anyOtherAlive) {
            state_.pendingSacrificeChoice = true;
            state_.pendingSacrificeSourcePlayer = playerIdx;
            state_.pendingSacrificeCard = s;
            logOut += " [STRIKE — SACRIFICE (5 DMG): Choose which player takes this Strike!]";
            return;
        }
    }

    const std::string strikeZid = playerZone(playerIdx, "strikes");
    Pile& wounds = ensureZonePile(strikeZid, {335.f, layout_.boardSize.y + 185.f}, "");
    wounds.cards.push_back(s);
    queueCardFlight(s, strikeDeckPos, wounds.pos, false, true);
    syncStateFromZones();

    const PlayerState& ps = state_.players[static_cast<size_t>(playerIdx)];
    logOut += " [P" + std::to_string(playerIdx + 1) + " Strike: " + d.name + " (" +
              std::to_string(dmg) + " dmg -> " + std::to_string(ps.strikeDamage) + "/" +
              std::to_string(ps.health) + " HP)!]";

    if (ps.strikeDamage >= ps.health) {
        if (ps.cannotDieTurnsRemaining > 0) {
            logOut += " [Dutch Rare Immunity: Survived fatal damage!]";
        } else {
            eliminatePlayer(playerIdx, logOut);
            syncStateFromZones();
        }
    }

    // Issue A8 — Stunning Blow (def 186, 1 Damage): "Draw another Strike for each Mark you have."
    if (s.def == 186 && state_.phase != TurnPhase::Defeat &&
        state_.players[static_cast<size_t>(playerIdx)].alive) {
        const int marks = state_.players[static_cast<size_t>(playerIdx)].marksCount;
        if (marks > 0) {
            logOut += " [Stunning Blow: P" + std::to_string(playerIdx + 1) + " has " +
                      std::to_string(marks) + " Mark(s) -> draws " + std::to_string(marks) +
                      " extra Strike(s)!]";
            for (int m = 0; m < marks; ++m) {
                if (state_.phase == TurnPhase::Defeat ||
                    !state_.players[static_cast<size_t>(playerIdx)].alive) {
                    break;
                }
                drawStrikeAgainstPlayer(playerIdx,
                                        seed + static_cast<std::uint32_t>((m + 1) * 19), logOut);
            }
        }
    }
}

std::string Table::resolveSacrificeChoice(int chosenPlayerIdx) {
    if (!state_.pendingSacrificeChoice) {
        return "No pending Sacrifice choice.";
    }
    if (chosenPlayerIdx < 0 || chosenPlayerIdx >= static_cast<int>(state_.players.size()) ||
        !state_.players[static_cast<size_t>(chosenPlayerIdx)].alive ||
        state_.players[static_cast<size_t>(chosenPlayerIdx)].escapedInChoppa) {
        return "Choose a living player to receive Sacrifice.";
    }

    const int srcP = state_.pendingSacrificeSourcePlayer;
    CardInst s = state_.pendingSacrificeCard;
    s.faceUp = true;
    s.attached = false;
    state_.pendingSacrificeChoice = false;

    const CardDef& d = catalog_.card(s.def);
    const int dmg = std::max(0, d.attack);

    const std::string strikeZid = playerZone(chosenPlayerIdx, "strikes");
    Pile& wounds = ensureZonePile(strikeZid, {335.f, layout_.boardSize.y + 185.f}, "");
    wounds.cards.push_back(s);
    queueCardFlight(s, zoneCenterPos("strikes", {1620.f, 550.f}), wounds.pos, true, true);
    syncStateFromZones();

    const PlayerState& ps = state_.players[static_cast<size_t>(chosenPlayerIdx)];
    std::string log;
    if (chosenPlayerIdx == srcP) {
        log = "P" + std::to_string(chosenPlayerIdx + 1) + " kept SACRIFICE (" +
              std::to_string(dmg) + " dmg -> " + std::to_string(ps.strikeDamage) + "/" +
              std::to_string(ps.health) + " HP)!";
    } else {
        log = "P" + std::to_string(srcP + 1) + " chose P" +
              std::to_string(chosenPlayerIdx + 1) + " to take SACRIFICE (" +
              std::to_string(dmg) + " dmg -> " + std::to_string(ps.strikeDamage) + "/" +
              std::to_string(ps.health) + " HP)!";
    }

    if (ps.strikeDamage >= ps.health) {
        if (ps.cannotDieTurnsRemaining > 0) {
            log += " [Dutch Rare Immunity: Survived fatal damage!]";
        } else {
            eliminatePlayer(chosenPlayerIdx, log);
            syncStateFromZones();
        }
    }

    state_.lastActionLog = log;
    recordMove("SACRIFICE_CHOICE", d.name, 1, "strikes", strikeZid, ps.strikeDamage, true, log);
    return log;
}

std::string Table::resolveStrikePhase(std::uint32_t seed) {
    if (state_.phase == TurnPhase::Victory || state_.phase == TurnPhase::Defeat) {
        return state_.lastActionLog;
    }
    if (hasPendingModal()) {
        return "Resolve the active prompt before advancing to the Strike Phase.";
    }

    const int ap = state_.activePlayer;
    if (ap < static_cast<int>(state_.players.size()) &&
        state_.players[static_cast<size_t>(ap)].escapedInChoppa) {
        state_.phase = TurnPhase::CleanupPhase;
        state_.lastActionLog = "P" + std::to_string(ap + 1) +
                               " evacuated aboard The Choppa! — skipping Strike Phase.";
        return state_.lastActionLog;
    }

    std::string log = "P" + std::to_string(ap + 1) + " Strike Phase: ";

    syncStateFromZones();

    // Check Wilds for No Powder Burns, No Shrapnel (def 108: strikes if active player is Marked)
    if (state_.marksCount > 0) {
        for (int s = 0; s <= 4; ++s) {
            if (Pile* wp = findZonePile("wilds_" + std::to_string(s));
                wp && !wp->cards.empty() && wp->top().faceUp && wp->top().def == 108) {
                log += "[No Powder Burns strikes Marked player from Wilds!] ";
                drawStrikeAgainstPlayer(ap, seed + static_cast<std::uint32_t>(s), log);
                if (state_.phase == TurnPhase::Defeat ||
                    !state_.players[static_cast<size_t>(ap)].alive) {
                    break;
                }
            }
        }
    }

    // Rulebook p.11: From RIGHT to LEFT, each Enemy in the Combat Zone strikes you, one at a time.
    std::vector<Pile*> czEnemies;
    for (auto& p : state_.piles) {
        if (p.zone == "combat" && !p.cards.empty() && p.top().faceUp && p.top().def != 114) {
            czEnemies.push_back(&p);
        }
    }
    std::sort(czEnemies.begin(), czEnemies.end(),
              [](const Pile* a, const Pile* b) { return a->pos.x > b->pos.x; });

    if (czEnemies.empty()) {
        log += "Combat Zone is clear (0 Strikes).";
        if (ap < static_cast<int>(state_.players.size()) &&
            state_.players[static_cast<size_t>(ap)].alive &&
            state_.players[static_cast<size_t>(ap)].strikeDamage >=
                state_.players[static_cast<size_t>(ap)].health &&
            state_.players[static_cast<size_t>(ap)].cannotDieTurnsRemaining <= 0) {
            eliminatePlayer(ap, log);
            syncStateFromZones();
        }
    } else if (ap < static_cast<int>(state_.players.size()) &&
               state_.players[static_cast<size_t>(ap)].alive) {
        for (size_t i = 0; i < czEnemies.size(); ++i) {
            const CardDef& d = catalog_.card(czEnemies[i]->top().def);
            const int strikesCount = hasKeyword(d, "Double Strike") ? 2 : 1;
            for (int k = 0; k < strikesCount; ++k) {
                log += d.name + (strikesCount > 1 ? " (Double Strike) " : " ") + "strikes ->";
                drawStrikeAgainstPlayer(ap, seed + static_cast<std::uint32_t>(i * 7 + k), log);
                if (state_.phase == TurnPhase::Defeat ||
                    !state_.players[static_cast<size_t>(ap)].alive) {
                    break;
                }
            }
            if (state_.phase == TurnPhase::Defeat ||
                !state_.players[static_cast<size_t>(ap)].alive) {
                break;
            }
        }
    }

    // Almost No Weight. Cuts Like Steel. (def 213): "Combat Zone: At the end of the Strike Phase, discard this Enemy."
    for (int i = static_cast<int>(state_.piles.size()) - 1; i >= 0; --i) {
        Pile& p = state_.piles[static_cast<size_t>(i)];
        if (p.zone == "combat" && !p.cards.empty() && p.top().def == 213) {
            const Vec2 fromPos = p.pos;
            CardInst c = p.cards.back();
            p.cards.pop_back();
            visuals_.erase(p.id);
            state_.piles.erase(state_.piles.begin() + i);
            Pile& deadPrey = ensureZonePile("dead_prey", {1950.f, 220.f}, "");
            deadPrey.cards.push_back(c);
            queueCardFlight(c, fromPos, deadPrey.pos, true, true);
            log += " [Almost No Weight discarded at end of Strike Phase.]";
        }
    }

    if (state_.phase != TurnPhase::Defeat && state_.phase != TurnPhase::Victory) {
        state_.phase = TurnPhase::CleanupPhase;
    }
    state_.lastActionLog = log;
    recordMove("STRIKE_PHASE", "Combat Zone Strikes", static_cast<int>(czEnemies.size()), "strikes",
               playerZone(ap, "strikes"), state_.strikeDamage, true, log);
    return log;
}

std::string Table::resolveCleanupPhase(std::uint32_t seed) {
    if (state_.phase == TurnPhase::Victory || state_.phase == TurnPhase::Defeat) {
        return state_.lastActionLog;
    }
    if (state_.pendingSacrificeChoice) {
        return "Choose which player takes the SACRIFICE Strike (5 Damage) first!";
    }
    if (state_.pendingCoverMeChoice) {
        return "Resolve the COVER ME! Strike prompt first!";
    }
    if (hasPendingModal()) {
        return "Resolve the active card ability prompt before Cleanup!";
    }

    const int ap = state_.activePlayer;
    const bool apAlive =
        (ap < static_cast<int>(state_.players.size())) && state_.players[static_cast<size_t>(ap)].alive;
    const bool apEscaped =
        apAlive && state_.players[static_cast<size_t>(ap)].escapedInChoppa;

    int discardedCount = 0;
    const std::string apPlayedZid = playerZone(ap, "played");
    const std::string apHandZid = playerZone(ap, "hand");
    const std::string apDiscZid = playerZone(ap, "discard");

    std::string log;

    // 1. Discard active player's Played & Hand cards:
    //    - Cards in state_.killedPlayedUids move to Dead Characters ("dead_characters")!
    //    - Active Dutch Rare immunity card (ps.activeImmunityCardUid) stays stationed next to Avatar on turn 1 (cannotDieTurnsRemaining == 2 -> 1),
    //      and only moves to Discard when cannotDieTurnsRemaining expires (1 -> 0) at end of next turn!
    if (apAlive) {
        PlayerState& ps = state_.players[static_cast<size_t>(ap)];
        const bool keepImmunityCardOnBoard =
            (ps.activeImmunityCardUid != 0 && ps.cannotDieTurnsRemaining >= 2);

        std::vector<CardInst> apToDiscard;
        std::vector<CardInst> apToDeadChars;
        const Vec2 discTargetPos = zoneCenterPos(apDiscZid, {2207.f, 1367.f});
        const Vec2 deadCharsPos = zoneCenterPos("dead_characters", {1846.f, 871.f});

        for (auto& p : state_.piles) {
            if ((p.zone == apPlayedZid || p.zone == apHandZid) && !p.cards.empty()) {
                if (keepImmunityCardOnBoard && p.zone == apPlayedZid &&
                    p.top().uid == ps.activeImmunityCardUid) {
                    continue; // Remains stationed beside Avatar until end of next turn!
                }
                const Vec2 fromPos = p.pos;
                for (auto c : p.cards) {
                    c.faceUp = true;
                    c.attached = false;
                    if (isCardKilledInPlayed(c.uid)) {
                        apToDeadChars.push_back(c);
                        queueCardFlight(c, fromPos, deadCharsPos, true, true);
                    } else {
                        apToDiscard.push_back(c);
                        ++discardedCount;
                    }
                }
                p.cards.clear();
            }
        }
        if (!apToDiscard.empty()) {
            Pile& apDisc = ensureZonePile(apDiscZid, {2207.f, 1367.f}, "");
            apDisc.cards.insert(apDisc.cards.end(), apToDiscard.begin(), apToDiscard.end());
            queueCardFlight(apToDiscard.back(), zoneCenterPos(apPlayedZid, apDisc.pos),
                            discTargetPos, true, true);
        }
        if (!apToDeadChars.empty()) {
            Pile& deadChars = ensureZonePile("dead_characters", {1846.f, 871.f}, "");
            deadChars.cards.insert(deadChars.cards.end(), apToDeadChars.begin(),
                                   apToDeadChars.end());
        }
        reflowZone(apHandZid);
        reflowZone(apPlayedZid);
    }

    // 1b. Rulebook p.12: Return any Coordinated cards set aside in teammates' Played Areas to their owners' Discard piles
    for (int q = 0; q < state_.playerCount; ++q) {
        if (q < static_cast<int>(state_.players.size())) {
            state_.players[static_cast<size_t>(q)].coordinatedThisTurn = false;
            state_.players[static_cast<size_t>(q)].allHandCoordinateThisTurn = false;
        }
        if (q == ap) continue;
        const std::string qPlayedZid = playerZone(q, "played");
        const std::string qDiscZid = playerZone(q, "discard");
        const std::uint32_t qImmUid =
            (q < static_cast<int>(state_.players.size()))
                ? state_.players[static_cast<size_t>(q)].activeImmunityCardUid
                : 0;
        std::vector<CardInst> coordToDiscard;
        for (auto& p : state_.piles) {
            if (p.zone == qPlayedZid && !p.cards.empty()) {
                if (qImmUid != 0 && p.top().uid == qImmUid) continue;
                for (auto c : p.cards) {
                    c.faceUp = true;
                    c.attached = false;
                    coordToDiscard.push_back(c);
                }
                p.cards.clear();
            }
        }
        if (!coordToDiscard.empty()) {
            Pile& qDisc = ensureZonePile(qDiscZid, {2207.f, 1367.f}, "");
            qDisc.cards.insert(qDisc.cards.end(), coordToDiscard.begin(), coordToDiscard.end());
            reflowZone(qPlayedZid);
        }
    }

    // 2. Reset turn pools & trackers
    state_.recruitPool = 0;
    state_.attackPool = 0;
    state_.avoidStrikesAvailable = 0;
    state_.freeScansAvailable = 0;
    state_.combatZoneAttackPenalty = 0;
    state_.drawCardsOnKill = 0;
    state_.coordinatedCountThisTurn = 0;
    state_.playedClasses.clear();
    state_.playedTeams.clear();
    state_.playedCharacters.clear();
    state_.roundHandClasses.clear();
    state_.roundHandTeams.clear();
    state_.coordinatedClasses.clear();
    state_.coordinatedTeams.clear();
    state_.coordinatedCharacters.clear();
    state_.coordinatedCards.clear();
    state_.accountedPlayedUids.clear();
    state_.triggeredSuperpowerUids.clear();
    state_.killedPlayedUids.clear();
    state_.swappedStatUids.clear();
    state_.lockedAccountedUids.clear();
    state_.accountedCardRecruit.clear();
    state_.accountedCardAttack.clear();
    state_.accountedCardAvoid.clear();
    state_.accountedCardFreeScan.clear();
    state_.accountedCardCombatPenalty.clear();
    state_.accountedCardDrawOnKill.clear();
    state_.abilityUsesThisTurn.clear();

    if (apAlive && !apEscaped) {
        PlayerState& ps = state_.players[static_cast<size_t>(ap)];
        const int drawCount = std::max(1, 6 - ps.nextHandSizePenalty);
        ps.nextHandSizePenalty = 0;
        state_.nextHandSizePenalty = 0;

        log = "P" + std::to_string(ap + 1) + " Cleanup: Discarded played/hand cards and drew " +
              std::to_string(drawCount) + " new cards.";
        drawPlayerCards(ap, drawCount, seed, log);

        // Tick Dutch Rare (If It Bleeds, We Can Kill It) next-turn death immunity
        if (ps.cannotDieTurnsRemaining > 0) {
            ps.cannotDieTurnsRemaining -= 1;
            state_.cannotDieTurnsRemaining = ps.cannotDieTurnsRemaining;
            if (ps.cannotDieTurnsRemaining == 0) {
                ps.activeImmunityCardUid = 0;
                log += " [If It Bleeds, We Can Kill It expired and moved to Discard.]";
                if (ps.strikeDamage >= ps.health) {
                    eliminatePlayer(ap, log);
                    if (state_.phase == TurnPhase::Defeat || state_.phase == TurnPhase::Victory) {
                        state_.lastActionLog = log;
                        return log;
                    }
                }
            }
        }
    } else if (apEscaped) {
        log = "P" + std::to_string(ap + 1) +
              " is safely aboard The Choppa! — passing turn to next operative.";
    } else {
        log = "P" + std::to_string(ap + 1) + " fallen — passing turn to next surviving operative.";
    }

    // 3. Advance to next living, non-escaped player's Enemy Phase
    state_.turnNumber += 1;
    for (int step = 1; step <= state_.playerCount; ++step) {
        const int cand = (ap + step) % std::max(1, state_.playerCount);
        if (cand < static_cast<int>(state_.players.size()) &&
            state_.players[static_cast<size_t>(cand)].alive &&
            !state_.players[static_cast<size_t>(cand)].escapedInChoppa) {
            state_.activePlayer = cand;
            break;
        }
    }
    state_.phase = TurnPhase::EnemyPhase;
    syncStateFromZones();

    const std::string nextAvName =
        (state_.activePlayer < static_cast<int>(state_.players.size()))
            ? catalog_.card(state_.players[static_cast<size_t>(state_.activePlayer)].avatarDefId).name
            : "Operative";
    log += " Turn " + std::to_string(state_.turnNumber) + ": Player " +
           std::to_string(state_.activePlayer + 1) + " (" + nextAvName + ") — Step 1: Enemy Phase.";
    state_.lastActionLog = log;

    int apDiscTotal = 0;
    if (const Pile* dp = findZonePile(apDiscZid)) apDiscTotal = static_cast<int>(dp->cards.size());
    recordMove("CLEANUP_PHASE", "Hand & Played Cards", discardedCount, apPlayedZid, apDiscZid,
               apDiscTotal, true, log);
    return log;
}

std::string Table::stepPhase(std::uint32_t seed) {
    switch (state_.phase) {
    case TurnPhase::EnemyPhase:
        return resolveEnemyPhase(seed);
    case TurnPhase::ActionPhase:
        return resolveStrikePhase(seed);
    case TurnPhase::StrikePhase:
        return resolveStrikePhase(seed);
    case TurnPhase::CleanupPhase:
        return resolveCleanupPhase(seed);
    case TurnPhase::Victory:
    case TurnPhase::Defeat:
        return state_.lastActionLog;
    }
    return state_.lastActionLog;
}

PileVisual& Table::visual(PileId id) {
    auto it = visuals_.find(id);
    if (it != visuals_.end()) return it->second;
    PileVisual pv;
    if (const Pile* p = find(id)) {
        pv.pos = p->pos;
        pv.angle = (p->quarterTurns & 1) ? (kPi * 0.5f) : 0.f;
    }
    return visuals_.emplace(id, pv).first->second;
}

void Table::syncVisuals() {
    for (const auto& p : state_.piles) {
        (void)visual(p.id);
    }
}

void Table::updateVisuals(float dt, PileId held) {
    flightBatchCounter_ = 0;
    for (auto it = flyingCards_.begin(); it != flyingCards_.end();) {
        it->elapsed += dt;
        if (it->elapsed >= it->delay + it->duration) {
            it = flyingCards_.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& p : state_.piles) {
        PileVisual& v = visual(p.id);
        const bool isHeld = (p.id == held);
        v.pos = damp(v.pos, p.pos, isHeld ? 36.f : 20.f, dt);
        const float targetAngle = (p.quarterTurns & 1) ? (kPi * 0.5f) : 0.f;
        v.angle = damp(v.angle, targetAngle, 20.f, dt);
        v.lift = damp(v.lift, isHeld ? 1.f : 0.f, 18.f, dt);
        v.flipT = std::min(1.f, v.flipT + dt * 5.5f);
        v.topOffset = damp(v.topOffset, {0.f, 0.f}, 22.f, dt);
    }
}

} // namespace lc
