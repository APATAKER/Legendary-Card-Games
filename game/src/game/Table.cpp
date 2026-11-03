#include "game/Table.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

    // It Changed Colors (def 111): Ongoing: -2 Attack for each different Class played this turn
    if (defId == 111) {
        atk = std::max(0, atk - static_cast<int>(state_.playedClasses.size()) * 2);
    }

    // Now It Wants Us (def 119): Wilds: -1 Attack for each space away from Combat Zone
    if (defId == 119) {
        const int wIdx = wildsIndexFromZone(p.zone);
        if (wIdx >= 0) {
            atk = std::max(0, atk - (wIdx + 1));
        }
    }

    // Knock Knock (def 9): Enemies in the Combat Zone have -1 Attack this turn
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

    std::vector<Pile*> zPiles;
    for (auto& p : state_.piles) {
        if (p.zone == zoneId && !p.cards.empty()) zPiles.push_back(&p);
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
                const int dmg = (d.attack > 0) ? d.attack : 1;
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

    // 2. Sync Active Player's Played / Active Area (credit newly dropped cards, un-credit removed cards)
    const std::string activePlayedZid = activePlayerZone("played");
    const std::string activeHandZid = activePlayerZone("hand");

    std::vector<CardInst> currentPlayed;
    state_.playedClasses.clear();
    state_.playedTeams.clear();
    state_.playedCharacters.clear();

    for (const auto& p : state_.piles) {
        if (p.zone != activePlayedZid) continue;
        for (const auto& c : p.cards) {
            currentPlayed.push_back(c);
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

            if (std::find(state_.accountedPlayedUids.begin(), state_.accountedPlayedUids.end(),
                          c.uid) == state_.accountedPlayedUids.end()) {
                state_.accountedPlayedUids.push_back(c.uid);
                state_.recruitPool += std::max(0, d.recruit);
                state_.attackPool += std::max(0, d.attack);
            }
        }
    }

    // If a card was dragged OUT of active player's played area back to their Hand, refund its base stats
    for (auto it = state_.accountedPlayedUids.begin(); it != state_.accountedPlayedUids.end();) {
        const std::uint32_t uid = *it;
        const bool stillInPlayed = std::any_of(
            currentPlayed.begin(), currentPlayed.end(),
            [uid](const CardInst& c) { return c.uid == uid; });
        if (!stillInPlayed) {
            for (const auto& p : state_.piles) {
                if (p.zone == activeHandZid) {
                    for (const auto& c : p.cards) {
                        if (c.uid == uid) {
                            const CardDef& d = catalog_.card(c.def);
                            state_.recruitPool =
                                std::max(0, state_.recruitPool - std::max(0, d.recruit));
                            state_.attackPool =
                                std::max(0, state_.attackPool - std::max(0, d.attack));
                        }
                    }
                }
            }
            it = state_.accountedPlayedUids.erase(it);
        } else {
            ++it;
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
            pv.pos = barracksPos;
            pv.flipT = 0.f;
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

void Table::moveCardToCombatZone(CardInst card, std::uint32_t seed, std::string& logOut) {
    card.faceUp = true;
    const CardDef& d = catalog_.card(card.def);

    // If an Event or Hazard is pushed into the Combat Zone, it resolves its effect immediately
    if (isEventDef(card.def)) {
        card.attached = false;
        Pile& disc = ensureZonePile("discard_hazards", {2150.f, 545.f}, "Discarded Hazards/Events");
        disc.cards.push_back(card);
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
        logOut += " [Defensive Position moved to Operations!]";
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
    np.cards = {card};
    state_.piles.push_back(std::move(np));
    visual(state_.piles.back().id).flipT = 0.f;

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
        Vec2 pos{461.f + static_cast<float>(slotIdx) * 210.f + 105.f, 234.f};
        if (const Zone* z = layout_.zone(zid)) pos = z->rect.center();

        Pile np;
        np.id = state_.nextPileId++;
        np.pos = pos;
        np.zone = zid;
        np.cards = {card};
        state_.piles.push_back(std::move(np));
        landedPileId = state_.piles.back().id;
        (void)visual(landedPileId);
    }

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
            CardInst c = prey->cards.back();
            prey->cards.pop_back();
            c.faceUp = false;
            c.attached = false;
            logOut += " [Event: Enemy ambushed into Hills face-down!]";
            pushCardIntoWildsSlot(0, c, seed, logOut);
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
        logOut += " Revealed Defensive Position -> moved to Operations!";
        return;
    }

    // Objective 3 Minor Goal: The Choppa! (def 114) -> Move to Combat Zone
    if (revealed.def == 114) {
        popWildsTopCard();
        moveCardToCombatZone(revealed, seed, logOut);
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
        for (int pIdx = 0; pIdx < state_.playerCount; ++pIdx) {
            if (pIdx >= static_cast<int>(state_.players.size())) break;
            if (!state_.players[static_cast<size_t>(pIdx)].alive ||
                state_.players[static_cast<size_t>(pIdx)].marksCount <= 0) {
                continue;
            }
            const std::string deckZid = playerZone(pIdx, "deck");
            const std::string discZid = playerZone(pIdx, "discard");
            if (Pile* pDeck = findZonePile(deckZid)) {
                int discarded = 0;
                std::vector<CardInst> kept;
                for (int i = 0; i < 3 && !pDeck->cards.empty(); ++i) {
                    CardInst top = pDeck->cards.back();
                    pDeck->cards.pop_back();
                    if (catalog_.card(top.def).cost >= 1) {
                        top.faceUp = true;
                        Pile& pDisc = ensureZonePile(discZid, {530.f, 1367.f}, "");
                        pDisc.cards.push_back(top);
                        pDeck = findZonePile(deckZid);
                        ++discarded;
                    } else {
                        kept.push_back(top);
                    }
                    if (!pDeck) break;
                }
                if (Pile* curDeck = findZonePile(deckZid); curDeck && !kept.empty()) {
                    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
                        curDeck->cards.push_back(*it);
                    }
                } else if (!kept.empty()) {
                    Pile& newDeck = ensureZonePile(deckZid, {530.f, 1367.f}, "");
                    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
                        newDeck.cards.push_back(*it);
                    }
                }
                logOut += " [Like a Hunter: P" + std::to_string(pIdx + 1) + " discarded " +
                          std::to_string(discarded) + " cost 1+ card(s)!]";
            }
        }
    }

    logOut += " Revealed " + d.name + " in " + wildsName(slotIdx) + ".";
}

std::string Table::resolveEnemyPhase(std::uint32_t seed) {
    if (state_.phase == TurnPhase::Victory || state_.phase == TurnPhase::Defeat) {
        return state_.lastActionLog;
    }

    std::string log;

    // Official Rulebook p.6: In a 5-player game, Round 1 (first 5 turns) skips the Enemy Phase
    if (state_.playerCount == 5 && state_.turnNumber <= 5) {
        state_.phase = TurnPhase::ActionPhase;
        log = "Turn " + std::to_string(state_.turnNumber) + " (P" +
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
        visual(state_.piles.back().id).pos = deckPos;
    }

    reflowZone(handZid);
}

void Table::healPlayerStrikes(int playerIdx, int count, bool leastDamageFirst,
                              std::string& logOut) {
    // Choose target player: if requested player has 0 wounds in multiplayer, allow healing most wounded teammate
    int targetP = std::clamp(playerIdx, 0, std::max(0, state_.playerCount - 1));
    Pile* wounds = findZonePile(playerZone(targetP, "strikes"));
    if (!wounds || wounds->cards.empty()) {
        int bestDmg = 0;
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

        int bestIdx = -1;
        int bestDmg = leastDamageFirst ? 999 : -1;
        for (int i = 0; i < static_cast<int>(wounds->cards.size()); ++i) {
            const int defId = wounds->cards[static_cast<size_t>(i)].def;
            if (defId == 190) continue; // Permanent Damage (def 190) cannot be healed
            const int dmg = std::max(0, catalog_.card(defId).attack);
            if ((leastDamageFirst && dmg < bestDmg) || (!leastDamageFirst && dmg > bestDmg)) {
                bestDmg = dmg;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;

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
        logOut += " [Healed P" + std::to_string(targetP + 1) +
                  " Strike: " + catalog_.card(healed.def).name + "!]";
    }

    syncStateFromZones();
}

void Table::applyPlayedCardAbility(int defId, bool classAlreadyPlayed, bool teamAlreadyPlayed,
                                   int otherBarracksChars, std::uint32_t seed,
                                   std::string& logOut) {
    const int ap = state_.activePlayer;
    const std::string handZid = activePlayerZone("hand");
    const std::string deckZid = activePlayerZone("deck");
    const std::string discZid = activePlayerZone("discard");

    switch (defId) {
    case 0: // Sexual Tyrannosaurus (Blain): [Strength]: Heal Strike with least damage
        if (classAlreadyPlayed) {
            healPlayerStrikes(ap, 1, true, logOut);
        }
        break;
    case 1: // Ol' Painless Is Waiting (Blain): [Leadership]: Draw 2 cards
        if (classAlreadyPlayed) {
            drawPlayerCards(ap, 2, seed, logOut);
            logOut += " [Leadership: Drew 2 cards!]";
        }
        break;
    case 2: // Dug In Like An Alabama Tick (Blain): Brothers in Arms: Scan any space
        if (otherBarracksChars > 0) {
            state_.freeScansAvailable += otherBarracksChars;
            logOut += " [Brothers in Arms: +" + std::to_string(otherBarracksChars) + " Free Scan!]";
        }
        break;
    case 3: { // Ain't Got Time to Bleed (Blain Rare): Flip top 2 Strikes into discard & gain Attack
        int bonusAtk = 0;
        for (int i = 0; i < 2; ++i) {
            Pile* strikes = findZonePile("strikes");
            if (!strikes || strikes->cards.empty()) break;
            CardInst s = strikes->cards.back();
            strikes->cards.pop_back();
            s.faceUp = true;
            bonusAtk += std::max(0, catalog_.card(s.def).attack);
            Pile& discStrikes = ensureZonePile("discard_strikes", {1846.f, 547.f}, "");
            discStrikes.cards.push_back(s);
        }
        state_.attackPool += bonusAtk;
        logOut += " [Ain't Got Time to Bleed: Flipped 2 Strikes for +" + std::to_string(bonusAtk) +
                  " Attack!]";
        break;
    }
    case 4: // Real Nasty Habit (Dillon): Avoid any one Strike
        state_.avoidStrikesAvailable += 1;
        logOut += " [+1 Avoid Strike ready!]";
        break;
    case 5: // Pushing Pencils (Dillon): [Tech]: Look at hidden card & scan it
        if (classAlreadyPlayed) {
            state_.freeScansAvailable += 1;
            logOut += " [Tech Combo: +1 Free Scan!]";
        }
        break;
    case 6: // Before Anybody Knows We Were There (Dillon): Brothers in Arms: +2 Recruit
        if (otherBarracksChars > 0) {
            const int bonus = otherBarracksChars * 2;
            state_.recruitPool += bonus;
            logOut += " [Brothers in Arms: +" + std::to_string(bonus) + " Recruit!]";
        }
        break;
    case 8: // Stick Around (Dutch): [Dutch's Team]: You may kill a card in your hand (thin starter)
        if (teamAlreadyPlayed) {
            for (auto& p : state_.piles) {
                if (p.zone == handZid && !p.cards.empty() &&
                    (p.top().def == 224 || p.top().def == 225)) {
                    CardInst killed = p.cards.back();
                    p.cards.pop_back();
                    Pile& deadChars = ensureZonePile("dead_characters", {1846.f, 871.f}, "");
                    deadChars.cards.push_back(killed);
                    logOut += " [Stick Around: Killed starter '" + catalog_.card(killed.def).name +
                              "' from hand!]";
                    reflowZone(handZid);
                    break;
                }
            }
        }
        break;
    case 9: // Knock Knock (Dutch): Enemies in Combat Zone have -1 Attack this turn
        state_.combatZoneAttackPenalty += 1;
        logOut += " [Knock Knock: Combat Zone enemies have -1 Attack this turn!]";
        break;
    case 10: // Bad Idea (Dutch): Brothers in Arms: +2 Attack
        if (otherBarracksChars > 0) {
            const int bonus = otherBarracksChars * 2;
            state_.attackPool += bonus;
            logOut += " [Brothers in Arms: +" + std::to_string(bonus) + " Attack!]";
        }
        break;
    case 11: // If It Bleeds, We Can Kill It (Dutch Rare): Can't die until end of next turn
        if (ap < static_cast<int>(state_.players.size())) {
            state_.players[static_cast<size_t>(ap)].cannotDieTurnsRemaining = 2;
        }
        state_.cannotDieTurnsRemaining = 2;
        logOut += " [DUTCH RARE: You cannot die until the end of your next turn!]";
        break;
    case 12: // Gonna Cut Your Name Into Him! (Mac): Draw a card
        drawPlayerCards(ap, 1, seed, logOut);
        logOut += " [Drew 1 card!]";
        break;
    case 14: // I See You (Mac): Brothers in Arms: Heal a Strike
        if (otherBarracksChars > 0) {
            healPlayerStrikes(ap, otherBarracksChars, false, logOut);
        }
        break;
    case 15: // Gonna Have Me Some Fun (Mac Rare): Each time you kill an Enemy this turn, draw 2
        state_.drawCardsOnKill += 2;
        logOut += " [Gonna Have Me Some Fun: Draw 2 cards per Enemy killed this turn!]";
        break;

    // --- Unique Role Card Abilities (All 15 Characters) ---
    case 65: // In the Shadows (CIA Agent Role): Call For Backup: Avoid any one Strike
        if (state_.coordinatedCountThisTurn > 0) {
            state_.avoidStrikesAvailable += 1;
            logOut += " [Call For Backup: +1 Avoid Strike!]";
        }
        break;
    case 67: // Knocking on Doors (Detective Role): Call For Backup: Draw a card
        if (state_.coordinatedCountThisTurn > 0) {
            drawPlayerCards(ap, 1, seed + 19u, logOut);
            logOut += " [Call For Backup: Drew 1 card!]";
        }
        break;
    case 69: // Running the Streets (Gangster Role): Call For Backup: Kill one of your cards
        if (state_.coordinatedCountThisTurn > 0) {
            for (auto& p : state_.piles) {
                if (p.zone == handZid && !p.cards.empty() &&
                    (p.top().def == 224 || p.top().def == 225)) {
                    CardInst killed = p.cards.back();
                    p.cards.pop_back();
                    Pile& deadChars = ensureZonePile("dead_characters", {1846.f, 871.f}, "");
                    deadChars.cards.push_back(killed);
                    logOut += " [Call For Backup: Killed starter '" +
                              catalog_.card(killed.def).name + "'!]";
                    reflowZone(handZid);
                    break;
                }
            }
        }
        break;
    case 71: // Fighting the Good Fight (Guerrilla Role): Call For Backup: +1 Attack
        if (state_.coordinatedCountThisTurn > 0) {
            state_.attackPool += 1;
            logOut += " [Call For Backup: +1 Attack!]";
        }
        break;
    case 73: // Command Decisions (Lieutenant Role): Call For Backup: +2 Recruit
        if (state_.coordinatedCountThisTurn > 0) {
            state_.recruitPool += 2;
            logOut += " [Call For Backup: +2 Recruit!]";
        }
        break;
    case 75: // The Highest Tech (OWLF Agent Role): Call For Backup: +1 Attack
        if (state_.coordinatedCountThisTurn > 0) {
            state_.attackPool += 1;
            logOut += " [Call For Backup: +1 Attack!]";
        }
        break;
    case 77: // Position and Situation (Radioman Role): Call For Backup: Put card from discard on top of deck
        if (state_.coordinatedCountThisTurn > 0) {
            if (Pile* disc = findZonePile(discZid); disc && !disc->cards.empty()) {
                CardInst top = disc->cards.back();
                disc->cards.pop_back();
                top.faceUp = false;
                Pile& deck = ensureZonePile(deckZid, {2002.f, 1367.f}, "");
                deck.cards.push_back(top);
                logOut += " [Call For Backup: Placed " + catalog_.card(top.def).name +
                          " on top of Deck!]";
            }
        }
        break;
    case 79: // Sources and Contacts (Reporter Role): Call For Backup: Heal a Strike from any player
        if (state_.coordinatedCountThisTurn > 0) {
            healPlayerStrikes(ap, 1, false, logOut);
        }
        break;
    case 81: // Partners in Crime (SWAT Officer Role): Call For Backup: Gain a Commander
        if (state_.coordinatedCountThisTurn > 0) {
            if (Pile* cmd = findZonePile("commanders"); cmd && !cmd->cards.empty()) {
                CardInst gained = cmd->cards.back();
                cmd->cards.pop_back();
                gained.faceUp = true;
                Pile& disc = ensureZonePile(discZid, {2207.f, 1367.f}, "");
                disc.cards.push_back(gained);
                logOut += " [Call For Backup: Gained Commander!]";
            }
        }
        break;
    case 83: // It Ain't No Man (Tracker Role): Call For Backup: Scan any space
        if (state_.coordinatedCountThisTurn > 0) {
            state_.freeScansAvailable += 1;
            logOut += " [Call For Backup: +1 Free Scan!]";
        }
        break;
    case 85: // City Hunter Role
    case 87: // Elder Hunter Role
    case 89: // Gunslinger Hunter Role
    case 91: // Jungle Hunter Role
    case 93: // Primeval Hunter Role
        drawPlayerCards(ap, 1, seed + 23u, logOut);
        logOut += " [Activate: Drew 1 card!]";
        break;
    default:
        break;
    }
}

std::string Table::playCard(PileId handPileId, std::uint32_t seed) {
    if (state_.phase == TurnPhase::EnemyPhase) {
        resolveEnemyPhase(seed);
    }
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Cards can only be played during the Action Phase.";
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
        if (hasKeyword(catalog_.card(hp->top().def), "Coordinate")) {
            return coordinateCard(handPileId, seed);
        }
        return "That card belongs to Player " + std::to_string(ownerP + 1) +
               " (currently Player " + std::to_string(state_.activePlayer + 1) +
               "'s turn — teammates can lend Coordinate cards!).";
    }

    if (hp->zone != handZid) {
        return "Select a card in Player " + std::to_string(state_.activePlayer + 1) +
               "'s Hand to play.";
    }

    const CardInst card = hp->top();
    const CardDef& d = catalog_.card(card.def);

    hp->zone = playedZid;
    state_.accountedPlayedUids.push_back(card.uid);
    reflowZone(handZid);
    reflowZone(playedZid);

    const int gainRec = std::max(0, d.recruit);
    const int gainAtk = std::max(0, d.attack);
    state_.recruitPool += gainRec;
    state_.attackPool += gainAtk;

    const bool classAlreadyPlayed = containsStr(state_.playedClasses, d.classType);
    const bool teamAlreadyPlayed = containsStr(state_.playedTeams, d.team);
    const bool isNewBarracksHero =
        isBarracksCharacterDef(card.def) && !containsStr(state_.playedCharacters, d.subtitle);

    if (!d.classType.empty() && !classAlreadyPlayed) {
        state_.playedClasses.push_back(d.classType);
    }
    if (!d.team.empty() && !teamAlreadyPlayed) {
        state_.playedTeams.push_back(d.team);
    }
    if (isNewBarracksHero && !d.subtitle.empty()) {
        state_.playedCharacters.push_back(d.subtitle);
    }

    // Count other unique Barracks characters played this turn for Brothers in Arms (Rulebook p.13)
    int otherBarracksChars = 0;
    for (const auto& ch : state_.playedCharacters) {
        if (ch != d.subtitle) ++otherBarracksChars;
    }

    std::string log = "P" + std::to_string(state_.activePlayer + 1) + " played " +
                      (d.title.empty() ? d.name : d.title) + " (+" + std::to_string(gainRec) +
                      " Recruit, +" + std::to_string(gainAtk) + " Attack).";

    applyPlayedCardAbility(card.def, classAlreadyPlayed, teamAlreadyPlayed, otherBarracksChars,
                           seed, log);

    // Rulebook p.13: Retroactive Brothers in Arms trigger when a new unique Barracks character enters play
    if (isNewBarracksHero) {
        for (const auto& p : state_.piles) {
            if (p.zone != playedZid || p.cards.empty() || p.id == handPileId) continue;
            const int prevDef = p.top().def;
            const CardDef& pd = catalog_.card(prevDef);
            if (pd.subtitle == d.subtitle) continue;
            if (prevDef == 2) {
                state_.freeScansAvailable += 1;
                log += " [Dug In Brothers in Arms: +1 Free Scan!]";
            } else if (prevDef == 6) {
                state_.recruitPool += 2;
                log += " [Before Anybody Knows Brothers in Arms: +2 Recruit!]";
            } else if (prevDef == 10) {
                state_.attackPool += 2;
                log += " [Bad Idea Brothers in Arms: +2 Attack!]";
            } else if (prevDef == 14) {
                healPlayerStrikes(state_.activePlayer, 1, false, log);
            }
        }
    }

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
        !state_.players[static_cast<size_t>(ownerP)].alive) {
        return "Eliminated players cannot Coordinate.";
    }
    if (state_.playerCount > 1 && ownerP == state_.activePlayer) {
        return "Rulebook p.12: Coordinate lends a card from ANOTHER player's hand to the active player!";
    }
    if (ownerP < static_cast<int>(state_.players.size()) &&
        state_.players[static_cast<size_t>(ownerP)].coordinatedThisTurn) {
        return "Player " + std::to_string(ownerP + 1) +
               " has already Coordinated a card this turn (max 1 per player per turn).";
    }

    const CardInst card = hp->top();
    const CardDef& d = catalog_.card(card.def);
    if (!hasKeyword(d, "Coordinate")) {
        return d.name + " does not have the Coordinate keyword.";
    }

    state_.coordinatedCountThisTurn += 1;
    if (ownerP < static_cast<int>(state_.players.size())) {
        state_.players[static_cast<size_t>(ownerP)].coordinatedThisTurn = true;
    }

    bool worldOfHurtActive = false;
    for (const auto& p : state_.piles) {
        if (!p.cards.empty() && p.top().faceUp && p.top().def == 120 &&
            (p.zone == "combat" || wildsIndexFromZone(p.zone) >= 0)) {
            worldOfHurtActive = true;
            break;
        }
    }

    const std::string ownerHandZid = playerZone(ownerP, "hand");
    const std::string ownerPlayedZid = playerZone(ownerP, "played");

    std::string log;
    if (ownerP == state_.activePlayer) {
        // Solo 1P fallback
        log = playCard(handPileId, seed);
    } else {
        // Multiplayer Rulebook p.12: Teammate sets card aside in their Played Area, draws 1 replacement,
        // and active player plays a virtual copy of it!
        hp->zone = ownerPlayedZid;
        reflowZone(ownerHandZid);
        reflowZone(ownerPlayedZid);

        const int gainRec = std::max(0, d.recruit);
        const int gainAtk = std::max(0, d.attack);
        state_.recruitPool += gainRec;
        state_.attackPool += gainAtk;

        const bool classAlreadyPlayed = containsStr(state_.playedClasses, d.classType);
        const bool teamAlreadyPlayed = containsStr(state_.playedTeams, d.team);
        const bool isNewBarracksHero =
            isBarracksCharacterDef(card.def) && !containsStr(state_.playedCharacters, d.subtitle);

        if (!d.classType.empty() && !classAlreadyPlayed) {
            state_.playedClasses.push_back(d.classType);
        }
        if (!d.team.empty() && !teamAlreadyPlayed) {
            state_.playedTeams.push_back(d.team);
        }
        if (isNewBarracksHero && !d.subtitle.empty()) {
            state_.playedCharacters.push_back(d.subtitle);
        }

        int otherBarracksChars = 0;
        for (const auto& ch : state_.playedCharacters) {
            if (ch != d.subtitle) ++otherBarracksChars;
        }

        log = "P" + std::to_string(state_.activePlayer + 1) + " gained virtual copy of " +
              (d.title.empty() ? d.name : d.title) + " (+" + std::to_string(gainRec) +
              " Recruit, +" + std::to_string(gainAtk) + " Attack).";
        applyPlayedCardAbility(card.def, classAlreadyPlayed, teamAlreadyPlayed, otherBarracksChars,
                               seed, log);
    }

    if (!worldOfHurtActive) {
        drawPlayerCards(ownerP, 1, seed + 99u, log);
        log = "[COORDINATE P" + std::to_string(ownerP + 1) + " -> P" +
              std::to_string(state_.activePlayer + 1) + "] Lent " + d.name +
              ", P" + std::to_string(ownerP + 1) + " drew 1 replacement! " + log;
    } else {
        log = "[COORDINATE P" + std::to_string(ownerP + 1) + " -> P" +
              std::to_string(state_.activePlayer + 1) + "] Lent " + d.name +
              " (A World of Hurt blocked replacement draw). " + log;
    }

    // Retroactive Call For Backup triggers if active player already played their Role card earlier this turn
    const std::string activePlayedZid = activePlayerZone("played");
    std::vector<int> activeRoleDefs;
    for (const auto& p : state_.piles) {
        if (p.zone == activePlayedZid && !p.cards.empty()) {
            const int rDef = p.top().def;
            if (rDef >= 65 && rDef <= 83 && (rDef % 2 == 1)) {
                activeRoleDefs.push_back(rDef);
            }
        }
    }
    if (state_.coordinatedCountThisTurn == 1) {
        for (int rDef : activeRoleDefs) {
            applyPlayedCardAbility(rDef, false, false, 0, seed + 131u, log);
        }
    }

    state_.lastActionLog = log;
    recordMove("COORDINATE", d.title.empty() ? d.name : d.title, 1, ownerHandZid, ownerPlayedZid,
               1, true, log);
    return log;
}

std::string Table::recruitCard(PileId targetPileId, std::uint32_t /*seed*/) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Recruiting is only allowed during the Action Phase.";
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

    const std::string srcZone = p->zone;
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
    const int discCount = static_cast<int>(discard.cards.size());

    // Rulebook p.9: Immediately refill any empty HQ space with the top card of the Barracks
    if (isHQ) {
        refillHQ();
    }

    state_.lastActionLog = "P" + std::to_string(state_.activePlayer + 1) + " recruited " +
                           catalog_.card(recruited.def).name + " for " + std::to_string(cost) +
                           " * -> placed in P" + std::to_string(state_.activePlayer + 1) +
                           " Discard (HQ refilled).";
    recordMove("RECRUIT_CARD", catalog_.card(recruited.def).name, 1, srcZone, discZid, discCount,
               true, state_.lastActionLog);
    return state_.lastActionLog;
}

std::string Table::scanWilds(PileId targetPileId, std::uint32_t seed) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Scanning is only allowed during the Action Phase.";
    }
    Pile* p = find(targetPileId);
    if (!p || p->cards.empty()) return "No card in that space.";

    const int wIdx = wildsIndexFromZone(p->zone);
    if (wIdx < 0) return "Only hidden cards in The Wilds can be scanned.";
    if (p->top().faceUp) return "That space is already face-up.";

    const int cost = scanCostForZone(p->zone);
    bool usedFreeScan = false;
    if (state_.freeScansAvailable > 0) {
        state_.freeScansAvailable -= 1;
        usedFreeScan = true;
    } else if (state_.attackPool >= cost) {
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
                          ? ("Scanned " + std::string(wildsName(wIdx)) + " (Free Scan)!")
                          : ("Scanned " + std::string(wildsName(wIdx)) + " for " +
                             std::to_string(cost) + " Attack ///!");
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

    state_.recruitPool -= 6;
    state_.defensivePositionAttachedToHills = true;

    // Move Defensive Position (def 107) from Operations ("testing") and attach it to Hills ("wilds_0")!
    std::optional<CardInst> defPosCard;
    for (size_t i = 0; i < state_.piles.size(); ++i) {
        Pile& ops = state_.piles[i];
        if (ops.zone == "testing" && !ops.cards.empty() && ops.top().def == 107) {
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
    }

    advanceObjectiveStack();
    state_.lastActionLog =
        "OBJECTIVE 2 COMPLETE! Defensive Position attached to Hills (auto-scans Hills). Now on Objective 3: Get to the Choppa!";
    recordMove("COMPLETE_OBJ_2", "Defensive Position", 1, "testing", "wilds_0", 1, true,
               state_.lastActionLog);
    return state_.lastActionLog;
}

std::string Table::fightEnemy(PileId targetPileId, std::uint32_t seed) {
    if (state_.phase != TurnPhase::ActionPhase) {
        return "Fighting enemies is only allowed during the Action Phase.";
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
    if (useRecruitToFight) {
        if (state_.recruitPool < reqAtk) {
            state_.lastActionLog = d.name + " outside the River requires " +
                                   std::to_string(reqAtk) + " Recruit * to fight!";
            return state_.lastActionLog;
        }
        state_.recruitPool -= reqAtk;
    } else {
        if (state_.attackPool < reqAtk) {
            state_.lastActionLog = "Need " + std::to_string(reqAtk) + " Attack /// to defeat " +
                                   d.name + " (have " + std::to_string(state_.attackPool) + " ///).";
            return state_.lastActionLog;
        }
        state_.attackPool -= reqAtk;
    }

    std::string log = "Defeated " + d.name + " (" + std::to_string(reqAtk) +
                      (useRecruitToFight ? " *" : " ///") + " spent)!";

    // Objective 1 Goal: Guerrilla Camp (def 102) -> Discard to Discarded Events ("discard_hazards")!
    if (target.def == 102) {
        const std::string fromWilds = p->zone;
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
        if (state_.currentObjective == 1) {
            advanceObjectiveStack();
            log = "OBJECTIVE 1 COMPLETE! Wiped out the Guerrilla Camp (1 /// Attack on clear " +
                  std::string(wildsName(wIdx)) +
                  ") -> Discarded Events! Now on Objective 2: Flares, Frags, and Claymores.";
        } else {
            log = "Wiped out the Guerrilla Camp (1 /// Attack) -> moved to Discarded Events!";
        }
        state_.lastActionLog = log;
        recordMove("WIPE_OUT_CAMP", d.name, 1, fromWilds, "discard_hazards",
                   static_cast<int>(disc.cards.size()), true, log);
        return log;
    }

    // Objective 3 Minor Goal: The Choppa! (def 114)
    if (target.def == 114) {
        int enemiesInCombat = 0;
        for (const auto& cp : state_.piles) {
            if (cp.zone == "combat" && !cp.cards.empty() && cp.top().def != 114) {
                ++enemiesInCombat;
            }
        }
        for (int i = 0; i < enemiesInCombat; ++i) {
            drawStrikeAgainstPlayer(state_.activePlayer,
                                    seed + static_cast<std::uint32_t>(i), log);
            if (state_.phase == TurnPhase::Defeat) break;
        }
        if (state_.phase != TurnPhase::Defeat) {
            int livingPlayers = 0;
            for (const auto& ps : state_.players) {
                if (ps.alive) ++livingPlayers;
            }
            state_.phase = TurnPhase::Victory;
            log = "MINOR VICTORY! Evacuated aboard The Choppa! (Score: " +
                  std::to_string(std::max(1, livingPlayers)) + " pts)";
        }
        state_.lastActionLog = log;
        return log;
    }

    // Final Enemy: One Ugly Mother (def 121)
    if (target.def == 121) {
        syncStateFromZones();
        if (state_.marksCount > 0) {
            // "Ongoing: If you would kill this Enemy while Marked, destroy a Mark instead."
            const std::string marksZid = activePlayerZone("marks");
            if (Pile* marks = findZonePile(marksZid); marks && !marks->cards.empty()) {
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
            } else if (state_.activePlayer < static_cast<int>(state_.players.size())) {
                state_.players[static_cast<size_t>(state_.activePlayer)].manualMarksOffset -= 1;
            }
            syncStateFromZones();
            log = "Fought One Ugly Mother while Marked -> destroyed 1 Mark (" +
                  std::to_string(state_.marksCount) +
                  " remaining)! Attack again with 0 Marks to kill him!";
            state_.lastActionLog = log;
            return log;
        }

        // Remove Final Enemy -> MAJOR VICTORY!
        p->cards.pop_back();
        if (p->cards.empty()) {
            const int idx = indexOf(p->id);
            if (idx >= 0) {
                visuals_.erase(p->id);
                state_.piles.erase(state_.piles.begin() + idx);
            }
        }
        Pile& deadPrey = ensureZonePile("dead_prey", {1950.f, 220.f}, "");
        deadPrey.cards.push_back(target);
        int livingPlayers = 0;
        for (const auto& ps : state_.players) {
            if (ps.alive) ++livingPlayers;
        }
        state_.phase = TurnPhase::Victory;
        state_.lastActionLog =
            "MAJOR VICTORY! You killed One Ugly Mother and completed Objective 3! (Score: " +
            std::to_string(5 + std::max(1, livingPlayers)) + " pts)";
        return state_.lastActionLog;
    }

    // Remove killed enemy from board (preserving any attached card underneath!) and move to Dead Enemies
    const std::string killedFromZone = inCombat ? "combat" : ("wilds_" + std::to_string(wIdx));
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
    const int deadPreyCount = static_cast<int>(deadPrey.cards.size());

    // Mac 1 Rare (Gonna Have Me Some Fun): Draw 2 cards each time you kill an Enemy this turn
    if (state_.drawCardsOnKill > 0) {
        drawPlayerCards(state_.activePlayer, state_.drawCardsOnKill, seed + 55u, log);
        log += " [Gonna Have Me Some Fun: Drew " + std::to_string(state_.drawCardsOnKill) +
               " cards!]";
    }

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
    case 208: // Young Blood: Defensive Adaptations (unless Tech played, draw 2 fewer cards)
        if (!containsStr(state_.playedClasses, "Tech")) {
            if (state_.activePlayer < static_cast<int>(state_.players.size())) {
                state_.players[static_cast<size_t>(state_.activePlayer)].nextHandSizePenalty += 2;
            }
            state_.nextHandSizePenalty += 2;
            log += " [Death: No Tech card played -> draw 2 fewer cards in Cleanup!]";
        }
        break;
    case 209: // Young Blood: The Eyes of the Demon (unless Strength played, draw a Strike)
        if (!containsStr(state_.playedClasses, "Strength")) {
            log += " [Death: No Strength card played -> Draw a Strike!]";
            drawStrikeAgainstPlayer(state_.activePlayer, seed + 73u, log);
        }
        break;
    case 211: // Young Blood: Something Out There Waiting For Us (unless Survival played, add Enemy)
        if (!containsStr(state_.playedClasses, "Survival")) {
            if (Pile* prey = findZonePile("prey"); prey && !prey->cards.empty()) {
                CardInst c = prey->cards.back();
                prey->cards.pop_back();
                c.faceUp = false;
                c.attached = false;
                log += " [Death: No Survival card played -> Added Enemy to Wilds!]";
                pushCardIntoWildsSlot(4, c, seed + 79u, log);
            }
        }
        break;
    case 212: // Young Blood: Fun and Games (unless Intel played, turn unattached Wilds enemies face-down)
        if (!containsStr(state_.playedClasses, "Intel")) {
            for (int s = 0; s <= 4; ++s) {
                if (Pile* wp = findZonePile("wilds_" + std::to_string(s));
                    wp && !wp->cards.empty() && !wp->top().attached && wp->top().faceUp &&
                    wp->top().def != 102) {
                    wp->cards.back().faceUp = false;
                    visual(wp->id).flipT = 0.f;
                }
            }
            log += " [Death: No Intel card played -> Wilds enemies turned face-down!]";
        }
        break;
    default:
        break;
    }

    state_.lastActionLog = log;
    recordMove("FIGHT_ENEMY", d.name, 1, killedFromZone, "dead_prey", deadPreyCount, true, log);
    return log;
}

void Table::eliminatePlayer(int playerIdx, std::string& logOut) {
    if (playerIdx < 0 || playerIdx >= static_cast<int>(state_.players.size())) return;
    PlayerState& ps = state_.players[static_cast<size_t>(playerIdx)];
    if (!ps.alive) return;

    ps.alive = false;

    // Flip Avatar face-down to mark KIA
    if (Pile* av = findZonePile(playerZone(playerIdx, "avatar"))) {
        for (auto& c : av->cards) c.faceUp = false;
        visual(av->id).flipT = 0.f;
    }

    // Rulebook p.15: Move fallen player's Strikes to Discarded Strikes, Marks to Discarded Hazards,
    // and Hand/Played/Deck/Discard cards to Dead Characters
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
    for (const auto& other : state_.players) {
        if (other.alive) {
            anyAlive = true;
            break;
        }
    }

    const std::string avName = catalog_.card(ps.avatarDefId).name;
    if (!anyAlive) {
        state_.phase = TurnPhase::Defeat;
        logOut += " [FATAL DAMAGE: Player " + std::to_string(playerIdx + 1) + " (" + avName +
                  ") KIA — All Operatives Fallen. Predator Wins!]";
    } else {
        logOut += " [OPERATIVE KIA: Player " + std::to_string(playerIdx + 1) + " (" + avName +
                  ") has fallen! Remaining operatives fight on!]";
    }
}

void Table::drawStrikeAgainstPlayer(int playerIdx, std::uint32_t seed, std::string& logOut) {
    if (state_.phase == TurnPhase::Defeat || state_.phase == TurnPhase::Victory) return;
    if (playerIdx < 0 || playerIdx >= static_cast<int>(state_.players.size())) return;
    if (!state_.players[static_cast<size_t>(playerIdx)].alive) return;

    if (playerIdx == state_.activePlayer && state_.avoidStrikesAvailable > 0) {
        state_.avoidStrikesAvailable -= 1;
        logOut += " [Avoided Strike!]";
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

    const std::string strikeZid = playerZone(playerIdx, "strikes");
    Pile& wounds = ensureZonePile(strikeZid, {335.f, layout_.boardSize.y + 185.f}, "");
    wounds.cards.push_back(s);
    syncStateFromZones();

    const PlayerState& ps = state_.players[static_cast<size_t>(playerIdx)];
    logOut += " [P" + std::to_string(playerIdx + 1) + " Strike: " + d.name + " (" +
              std::to_string(dmg) + " dmg -> " + std::to_string(ps.strikeDamage) + "/" +
              std::to_string(ps.health) + " HP)!]";

    // Stunning Blow (def 186): Discard a card from your hand
    if (s.def == 186) {
        const std::string handZid = playerZone(playerIdx, "hand");
        const std::string discZid = playerZone(playerIdx, "discard");
        for (auto& hp : state_.piles) {
            if (hp.zone == handZid && !hp.cards.empty()) {
                CardInst disc = hp.cards.back();
                hp.cards.pop_back();
                Pile& pDisc = ensureZonePile(discZid, {2207.f, 1367.f}, "");
                pDisc.cards.push_back(disc);
                reflowZone(handZid);
                logOut += " [Stunning Blow: P" + std::to_string(playerIdx + 1) +
                          " discarded 1 card from hand!]";
                break;
            }
        }
    }

    if (ps.strikeDamage >= ps.health) {
        if (ps.cannotDieTurnsRemaining > 0) {
            logOut += " [Dutch Rare Immunity: Survived fatal damage!]";
        } else {
            eliminatePlayer(playerIdx, logOut);
            syncStateFromZones();
        }
    }
}

std::string Table::resolveStrikePhase(std::uint32_t seed) {
    if (state_.phase == TurnPhase::Victory || state_.phase == TurnPhase::Defeat) {
        return state_.lastActionLog;
    }

    const int ap = state_.activePlayer;
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
            CardInst c = p.cards.back();
            p.cards.pop_back();
            visuals_.erase(p.id);
            state_.piles.erase(state_.piles.begin() + i);
            Pile& deadPrey = ensureZonePile("dead_prey", {1950.f, 220.f}, "");
            deadPrey.cards.push_back(c);
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

    const int ap = state_.activePlayer;
    const bool apAlive =
        (ap < static_cast<int>(state_.players.size())) && state_.players[static_cast<size_t>(ap)].alive;

    int discardedCount = 0;
    const std::string apPlayedZid = playerZone(ap, "played");
    const std::string apHandZid = playerZone(ap, "hand");
    const std::string apDiscZid = playerZone(ap, "discard");

    // 1. Discard active player's Played & Hand cards to active player's Discard
    if (apAlive) {
        std::vector<CardInst> apToDiscard;
        for (auto& p : state_.piles) {
            if ((p.zone == apPlayedZid || p.zone == apHandZid) && !p.cards.empty()) {
                for (auto c : p.cards) {
                    c.faceUp = true;
                    c.attached = false;
                    apToDiscard.push_back(c);
                    ++discardedCount;
                }
                p.cards.clear();
            }
        }
        if (!apToDiscard.empty()) {
            Pile& apDisc = ensureZonePile(apDiscZid, {2207.f, 1367.f}, "");
            apDisc.cards.insert(apDisc.cards.end(), apToDiscard.begin(), apToDiscard.end());
        }
        reflowZone(apHandZid);
        reflowZone(apPlayedZid);
    }

    // 1b. Rulebook p.12: Return any Coordinated cards set aside in teammates' Played Areas to their owners' Discard piles
    for (int q = 0; q < state_.playerCount; ++q) {
        if (q < static_cast<int>(state_.players.size())) {
            state_.players[static_cast<size_t>(q)].coordinatedThisTurn = false;
        }
        if (q == ap) continue;
        const std::string qPlayedZid = playerZone(q, "played");
        const std::string qDiscZid = playerZone(q, "discard");
        std::vector<CardInst> coordToDiscard;
        for (auto& p : state_.piles) {
            if (p.zone == qPlayedZid && !p.cards.empty()) {
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
    state_.accountedPlayedUids.clear();

    std::string log;
    if (apAlive) {
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
            if (ps.cannotDieTurnsRemaining == 0 && ps.strikeDamage >= ps.health) {
                eliminatePlayer(ap, log);
                if (state_.phase == TurnPhase::Defeat) {
                    state_.lastActionLog = log;
                    return log;
                }
            }
        }
    } else {
        log = "P" + std::to_string(ap + 1) + " fallen — passing turn to next surviving operative.";
    }

    // 3. Advance to next living player's Enemy Phase
    state_.turnNumber += 1;
    for (int step = 1; step <= state_.playerCount; ++step) {
        const int cand = (ap + step) % std::max(1, state_.playerCount);
        if (cand < static_cast<int>(state_.players.size()) &&
            state_.players[static_cast<size_t>(cand)].alive) {
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
