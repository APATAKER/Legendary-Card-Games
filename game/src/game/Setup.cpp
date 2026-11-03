#include "game/Table.h"

#include <algorithm>
#include <optional>
#include <random>
#include <unordered_set>

namespace lc {

namespace {

std::vector<CardInst> instantiateGroup(const Catalog& cat, const CardGroup& g, TableState& st,
                                       bool faceUpOverride, bool useOverride) {
    std::vector<CardInst> out;
    const bool faceUp = useOverride ? faceUpOverride : g.faceUp;
    for (int defId : g.cards) {
        const CardDef& d = cat.card(defId);
        for (int q = 0; q < d.qty; ++q) {
            out.push_back({st.nextUid++, defId, faceUp, false});
        }
    }
    return out;
}

Pile& addPile(TableState& st, Vec2 pos, std::vector<CardInst> cards, std::string zone = {},
              int quarterTurns = 0, std::string label = {}) {
    Pile p;
    p.id = st.nextPileId++;
    p.pos = pos;
    p.quarterTurns = quarterTurns;
    p.cards = std::move(cards);
    p.zone = std::move(zone);
    p.label = std::move(label);
    st.piles.push_back(std::move(p));
    return st.piles.back();
}

Vec2 zoneCenter(const BoardLayout& layout, const std::string& id, Vec2 fallback) {
    if (const Zone* z = layout.zone(id)) return z->rect.center();
    return fallback;
}

/// Official Rulebook p.6 Young Blood / Mercenary scaling chart by player count (1..5 players).
/// Returns {miniDeck1Count, miniDeck2Count, miniDeck3Count}.
struct YoungBloodCounts {
    int act1 = 0;
    int act2 = 0;
    int act3 = 0;
};

YoungBloodCounts youngBloodCountsForPlayers(int playerCount) {
    switch (std::clamp(playerCount, 1, 5)) {
    case 1:
        return {0, 0, 0};
    case 2:
        return {0, 1, 2};
    case 3:
        return {2, 3, 4};
    case 4:
    case 5:
        return {4, 5, 6};
    default:
        return {0, 0, 0};
    }
}

bool isValidAvatarForFaction(int defId, bool playAsPredators) {
    if ((defId % 2) != 0) return false;
    if (playAsPredators) {
        return defId >= 84 && defId <= 92;
    }
    return defId >= 64 && defId <= 82;
}

} // namespace

std::vector<int> defaultAvatarsForScenario(int movie, bool playAsPredators) {
    if (playAsPredators) {
        if (movie == 2) {
            return {84, 88, 86, 90, 92}; // City, Gunslinger, Elder, Jungle, Primeval
        }
        return {90, 86, 92, 84, 88}; // Jungle, Elder, Primeval, City, Gunslinger
    }
    if (movie == 2) {
        return {66, 80, 74, 68, 78}; // Detective, SWAT, OWLF, Gangster, Reporter
    }
    return {72, 82, 70, 64, 76}; // Lieutenant, Tracker, Guerrilla, CIA Agent, Radioman
}

std::vector<int> resolveUniquePlayerAvatars(const GameSetupConfig& cfg) {
    const int nPlayers = std::clamp(cfg.playerCount, 1, 5);
    std::vector<int> pool;
    if (cfg.playAsPredators) {
        pool = (cfg.movie == 2) ? std::vector<int>{84, 88, 86, 90, 92}
                                : std::vector<int>{90, 86, 92, 84, 88};
    } else {
        pool = (cfg.movie == 2) ? std::vector<int>{66, 80, 74, 68, 78, 72, 82, 70, 64, 76}
                                : std::vector<int>{72, 82, 70, 64, 76, 66, 80, 74, 68, 78};
    }

    std::unordered_set<int> used;
    std::vector<int> result;
    result.reserve(static_cast<size_t>(nPlayers));

    for (int p = 0; p < nPlayers; ++p) {
        int requested = -1;
        if (p < static_cast<int>(cfg.playerAvatars.size())) {
            requested = cfg.playerAvatars[static_cast<size_t>(p)];
        }
        if (p == 0 && cfg.playerAvatars.empty()) {
            requested = cfg.avatarDefId;
        }

        int picked = -1;
        if (isValidAvatarForFaction(requested, cfg.playAsPredators) && !used.count(requested)) {
            picked = requested;
        } else {
            for (int cand : pool) {
                if (!used.count(cand)) {
                    picked = cand;
                    break;
                }
            }
        }
        used.insert(picked);
        result.push_back(picked);
    }

    return result;
}

TableState buildGameSetup(const Catalog& cat, const BoardLayout& layoutIn, std::uint32_t seed,
                          const GameSetupConfig& cfg) {
    TableState st;
    st.setupConfig = cfg;
    st.playerCount = std::clamp(cfg.playerCount, 1, 5);
    st.setupConfig.playerCount = st.playerCount;
    st.turnNumber = 1;
    st.activePlayer = 0;
    st.phase = TurnPhase::EnemyPhase;
    st.currentObjective = 1;

    // Ensure we have exact per-player zone coordinates even if the caller passed a 1P layout
    BoardLayout localLayout;
    const BoardLayout* activeLayout = &layoutIn;
    if (static_cast<int>(layoutIn.playerTrays.size()) != st.playerCount) {
        localLayout = layoutIn;
        configurePlayerZones(localLayout, st.playerCount);
        activeLayout = &localLayout;
    }
    const BoardLayout& layout = *activeLayout;

    const std::vector<int> uniqueAvatars = resolveUniquePlayerAvatars(st.setupConfig);
    st.setupConfig.playerAvatars = uniqueAvatars;
    if (!uniqueAvatars.empty()) {
        st.setupConfig.avatarDefId = uniqueAvatars[0];
    }

    const std::string movieTitle =
        (cfg.movie == 2) ? "Predator 2 (Los Angeles)" : "Predator 1 (Val Verde)";
    const std::string factionTitle = cfg.playAsPredators ? "Predators (Hunter)" : "Humans (Co-op)";
    if (st.playerCount == 5) {
        st.lastActionLog = movieTitle + " [" + factionTitle +
                           ", 5P] — Preparation Round (Enemy Phase skipped on Round 1).";
    } else {
        st.lastActionLog = movieTitle + " [" + factionTitle + ", " +
                           std::to_string(st.playerCount) + "P] ready — Player 1 (" +
                           cat.card(uniqueAvatars[0]).name + ") turn.";
    }

    std::mt19937 rng(seed);

    auto takeGroup = [&](const std::string& key, bool faceUp) -> std::vector<CardInst> {
        const CardGroup* g = cat.group(key);
        if (!g) return {};
        return instantiateGroup(cat, *g, st, faceUp, true);
    };

    // 1. Setup Location & Objectives (Predator 1 vs Predator 2)
    const std::string setupKey = (cfg.movie == 2) ? "setup-p2" : "setup-p1";
    if (const CardGroup* setup = cat.group(setupKey)) {
        std::vector<CardInst> objectives;
        for (int defId : setup->cards) {
            const CardDef& d = cat.card(defId);
            if (d.name.find("Location") != std::string::npos) {
                addPile(st, zoneCenter(layout, "location", {360.f, 550.f}),
                        {{st.nextUid++, defId, true, false}}, "location", 1);
            } else if (!cfg.playAsPredators) {
                objectives.push_back({st.nextUid++, defId, true, false});
            }
        }
        // Stack Objective 3 at bottom -> Objective 1 on top (back() is top)
        std::reverse(objectives.begin(), objectives.end());
        if (!objectives.empty()) {
            addPile(st, zoneCenter(layout, "objectives", {330.f, 220.f}), std::move(objectives),
                    "objectives");
        }
    }

    // 2. Enemy / Prey Deck (Act 3 at bottom, Act 2 middle, Act 1 on top + Young Blood / Mercenary Chart)
    {
        std::vector<CardInst> act1, act2, act3, extraPool;
        if (!cfg.playAsPredators) {
            if (cfg.movie == 2) {
                act1 = takeGroup("mini-mini-deck-predator-2-humans-act-1-war-zone", false);
                act2 = takeGroup("mini-mini-deck-predator-2-humans-act-2-personal-little-war", false);
                act3 = takeGroup("mini-mini-deck-predator-2-humans-act-3-other-world-life-form", false);
            } else {
                act1 = takeGroup("mini-mini-deck-predator-humans-act-1-expendable-assets", false);
                act2 = takeGroup("mini-mini-deck-predator-humans-act-2-flares-frags-and-claymores", false);
                act3 = takeGroup("mini-mini-deck-predator-humans-act-3-get-to-the-choppa-", false);
            }
            extraPool = takeGroup("g-young-blood", false);
        } else {
            if (cfg.movie == 2) {
                act1 = takeGroup("mini-mini-deck-predator-2-predators-act-1-drawn-by-heat-and-conflict", false);
                act2 = takeGroup("mini-mini-deck-predator-2-predators-act-2-this-is-history", false);
                act3 = takeGroup("mini-mini-deck-predator-2-predators-act-3-a-taste-for-beef", false);
            } else {
                act1 = takeGroup("mini-mini-deck-predator-predators-act-1-no-sport", false);
                act2 = takeGroup("mini-mini-deck-predator-predators-act-2-payback-time", false);
                act3 = takeGroup("mini-mini-deck-predator-predators-act-3-what-the-hell-are-you-", false);
            }
            extraPool = takeGroup("g-mercenary", false);
        }

        std::shuffle(extraPool.begin(), extraPool.end(), rng);
        const YoungBloodCounts yb = youngBloodCountsForPlayers(st.playerCount);

        auto dealExtras = [&](std::vector<CardInst>& miniDeck, int count) {
            for (int i = 0; i < count && !extraPool.empty(); ++i) {
                miniDeck.push_back(extraPool.back());
                extraPool.pop_back();
            }
        };
        dealExtras(act3, yb.act3);
        dealExtras(act2, yb.act2);
        dealExtras(act1, yb.act1);

        std::shuffle(act1.begin(), act1.end(), rng);
        std::shuffle(act2.begin(), act2.end(), rng);
        std::shuffle(act3.begin(), act3.end(), rng);

        std::vector<CardInst> preyDeck;
        preyDeck.insert(preyDeck.end(), act3.begin(), act3.end());
        preyDeck.insert(preyDeck.end(), act2.begin(), act2.end());
        preyDeck.insert(preyDeck.end(), act1.begin(), act1.end());
        if (!preyDeck.empty()) {
            addPile(st, zoneCenter(layout, "prey", {1620.f, 220.f}), std::move(preyDeck), "prey");
        }
    }

    // 3. Strikes Deck (Enemy Strikes for Humans, Prey Strikes for Predators)
    {
        auto strikes = takeGroup(cfg.playAsPredators ? "g-prey-strike" : "g-enemy-strike", false);
        std::shuffle(strikes.begin(), strikes.end(), rng);
        if (!strikes.empty()) {
            addPile(st, zoneCenter(layout, "strikes", {1620.f, 550.f}), std::move(strikes),
                    "strikes");
        }
    }

    // 4. Commanders / Killer Instincts Deck
    {
        auto commanders =
            takeGroup(cfg.playAsPredators ? "g-killer-instincts" : "g-commanders", false);
        std::shuffle(commanders.begin(), commanders.end(), rng);
        if (!commanders.empty()) {
            addPile(st, zoneCenter(layout, "commanders", {330.f, 870.f}), std::move(commanders),
                    "commanders");
        }
    }

    // 5. Barracks / Battleground Deck + Deal 5 to HQ / Hunting Grounds
    {
        std::vector<CardInst> barracks;
        if (!cfg.playAsPredators) {
            const auto heroGroups =
                (cfg.movie == 2)
                    ? std::vector<const char*>{"char-danny-predator-2-", "char-harrigan-predator-2-",
                                               "char-keyes-predator-2-", "char-lambert-predator-2-"}
                    : std::vector<const char*>{"char-blain-predator-", "char-dillon-predator-",
                                               "char-dutch-predator-", "char-mac-predator-"};
            for (const char* k : heroGroups) {
                auto cards = takeGroup(k, false);
                barracks.insert(barracks.end(), cards.begin(), cards.end());
            }
        } else {
            for (const char* k : {"g-gear", "g-traps", "g-challenges", "g-tests"}) {
                auto cards = takeGroup(k, false);
                barracks.insert(barracks.end(), cards.begin(), cards.end());
            }
        }
        std::shuffle(barracks.begin(), barracks.end(), rng);

        for (int i = 0; i < 5 && !barracks.empty(); ++i) {
            CardInst c = barracks.back();
            barracks.pop_back();
            c.faceUp = true;
            const std::string zid = "hq_" + std::to_string(i);
            addPile(st, zoneCenter(layout, zid, {600.f + i * 190.f, 880.f}), {c}, zid);
        }
        if (!barracks.empty()) {
            addPile(st, zoneCenter(layout, "barracks", {1620.f, 870.f}), std::move(barracks),
                    "barracks");
        }
    }

    // 6. Separate 7-Zone Player Board Areas for ALL 1..5 Players, each with their own
    //    unique Avatar, unique Role Card, 7 Experience + 5 Brute Strength, 6-card Hand, & 7-card Deck.
    {
        auto exp = takeGroup("g-starting-experience", false);
        auto brute = takeGroup("g-starting-brute-strength", false);
        auto avatars =
            takeGroup(cfg.playAsPredators ? "roles-predator-avatars" : "roles-human-avatars", true);
        auto roles = takeGroup(
            cfg.playAsPredators ? "roles-predator-role-cards" : "roles-human-role-cards", false);

        auto extractDef = [](std::vector<CardInst>& vec, int targetDef,
                             std::optional<CardInst>& out) {
            auto it = std::find_if(vec.begin(), vec.end(),
                                   [targetDef](const CardInst& c) { return c.def == targetDef; });
            if (it != vec.end()) {
                out = *it;
                vec.erase(it);
            }
        };

        st.players.clear();
        st.players.reserve(static_cast<size_t>(st.playerCount));

        for (int p = 0; p < st.playerCount; ++p) {
            const int chosenAvatarDef = uniqueAvatars[static_cast<size_t>(p)];
            const int chosenRoleDef = chosenAvatarDef + 1;

            PlayerState ps;
            ps.avatarDefId = chosenAvatarDef;
            avatarStatsForDef(chosenAvatarDef, ps.health, ps.defense);
            ps.alive = true;
            st.players.push_back(ps);

            std::optional<CardInst> pAvatar;
            std::optional<CardInst> pRoleCard;
            extractDef(avatars, chosenAvatarDef, pAvatar);
            extractDef(roles, chosenRoleDef, pRoleCard);

            const std::string avatarZid = playerZoneId(p, "avatar");
            const std::string handZid = playerZoneId(p, "hand");
            const std::string deckZid = playerZoneId(p, "deck");

            if (pAvatar) {
                pAvatar->faceUp = true;
                addPile(st, zoneCenter(layout, avatarZid, {167.f, 1232.f}), {*pAvatar}, avatarZid,
                        0, cat.card(chosenAvatarDef).name);
            }

            std::vector<CardInst> pDeck;
            pDeck.reserve(13);
            for (int i = 0; i < 7 && !exp.empty(); ++i) {
                pDeck.push_back(exp.back());
                exp.pop_back();
            }
            for (int i = 0; i < 5 && !brute.empty(); ++i) {
                pDeck.push_back(brute.back());
                brute.pop_back();
            }
            if (pRoleCard) {
                pRoleCard->faceUp = false;
                pDeck.push_back(*pRoleCard);
            }
            std::shuffle(pDeck.begin(), pDeck.end(), rng);

            // Deal 6 cards face-up into this player's Hand Area
            const Zone* hz = layout.zone(handZid);
            const Vec2 handCenter = hz ? hz->rect.center() : Vec2{1185.f, 1502.f};
            const float handStartX = hz ? (hz->rect.x + 150.f) : 595.f;
            const float handStepX = 210.f;
            for (int i = 0; i < 6 && !pDeck.empty(); ++i) {
                CardInst c = pDeck.back();
                pDeck.pop_back();
                c.faceUp = true;
                addPile(st, {handStartX + static_cast<float>(i) * handStepX, handCenter.y}, {c},
                        handZid, 0);
            }

            // Place remaining 7 cards face-down in this player's Deck Area
            if (!pDeck.empty()) {
                addPile(st, zoneCenter(layout, deckZid, {2002.f, 1367.f}), std::move(pDeck),
                        deckZid, 0);
            }
        }

        if (!st.players.empty()) {
            st.avatarHealth = st.players[0].health;
            st.avatarDefense = st.players[0].defense;
        }
    }

    return st;
}

TableState buildDefaultSetup(const Catalog& cat, const BoardLayout& layout, std::uint32_t seed,
                             int playerCount) {
    GameSetupConfig cfg;
    cfg.movie = 1;
    cfg.playAsPredators = false;
    cfg.playerCount = std::clamp(playerCount, 1, 5);
    cfg.playerAvatars = defaultAvatarsForScenario(1, false);
    cfg.avatarDefId = cfg.playerAvatars[0];
    return buildGameSetup(cat, layout, seed, cfg);
}

} // namespace lc
