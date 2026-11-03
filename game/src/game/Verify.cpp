#include "game/Verify.h"

#include "core/Log.h"
#include "game/Assets.h"
#include "game/Table.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace lc {

namespace {

bool rectsOverlap(const Rect& a, const Rect& b) {
    return !(a.right() <= b.x || b.right() <= a.x || a.bottom() <= b.y || b.bottom() <= a.y);
}

bool rectContains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f &&
           inner.right() <= outer.right() + 0.5f && inner.bottom() <= outer.bottom() + 0.5f;
}

bool hasKw(const CardDef& d, std::string_view kw) {
    for (const auto& k : d.keywords) {
        if (k == kw) return true;
    }
    return false;
}

void clearWildsAndCombat(Table& table) {
    auto& piles = table.mutableState().piles;
    piles.erase(std::remove_if(piles.begin(), piles.end(),
                               [](const Pile& p) {
                                   return p.zone == "combat" || p.zone.rfind("wilds_", 0) == 0 ||
                                          p.zone == "discard_hazards";
                               }),
                piles.end());
}

PileId placeSingleInZone(Table& table, int defId, const std::string& zoneId, bool faceUp,
                         bool attached = false) {
    const Zone* z = table.layout().zone(zoneId);
    const Vec2 pos = z ? z->rect.center() : Vec2{900.f, 200.f};
    const PileId pid = table.spawnCard(defId, pos, faceUp);
    if (Pile* p = table.find(pid)) {
        p->zone = zoneId;
        p->cards.back().attached = attached;
    }
    return pid;
}

} // namespace

int runMechanicsVerification(const std::filesystem::path& reportPath) {
    std::ostringstream out;
    int passed = 0;
    int failed = 0;

    auto check = [&](bool cond, const std::string& desc) {
        if (cond) {
            ++passed;
            out << "[PASS] " << desc << "\n";
            LC_LOG("[PASS] %s", desc.c_str());
        } else {
            ++failed;
            out << "[FAIL] " << desc << "\n";
            LC_ERROR("[FAIL] %s", desc.c_str());
        }
    };

    const auto assetDir = findAssetDir();
    auto catOpt = loadCatalog(assetDir / "catalog.json");
    auto layOpt = loadBoardLayout(assetDir / "board_layout.json");
    check(catOpt.has_value() && layOpt.has_value(), "Load catalog.json and board_layout.json");
    if (!catOpt || !layOpt) return 1;

    const Catalog& catalog = *catOpt;
    BoardLayout baseLayout = *layOpt;

    // =========================================================================
    // PART 1: MULTIPLAYER (1, 2, 3, 4, 5 PLAYERS) SEPARATE BOARDS & CHARACTERS
    // =========================================================================
    out << "\n=== PART 1: MULTIPLAYER (1..5 PLAYERS) SEPARATE BOARDS & UNIQUE CHARACTERS ===\n";

    for (int movie : {1, 2}) {
        for (bool predators : {false, true}) {
            for (int nPlayers = 1; nPlayers <= 5; ++nPlayers) {
                BoardLayout lay = baseLayout;
                configurePlayerZones(lay, nPlayers);

                GameSetupConfig cfg;
                cfg.movie = movie;
                cfg.playAsPredators = predators;
                cfg.playerCount = nPlayers;
                cfg.playerAvatars = defaultAvatarsForScenario(movie, predators);
                cfg.avatarDefId = cfg.playerAvatars[0];

                const std::string tag = "M" + std::to_string(movie) +
                                        (predators ? "-Pred-" : "-Hum-") +
                                        std::to_string(nPlayers) + "P";

                check(static_cast<int>(lay.playerTrays.size()) == nPlayers,
                      tag + ": PlayerTray count == " + std::to_string(nPlayers));

                // Verify non-overlapping trays strictly below the main board
                bool traysClean = true;
                for (int i = 0; i < nPlayers; ++i) {
                    if (lay.playerTrays[static_cast<size_t>(i)].rect.y <= lay.boardSize.y) {
                        traysClean = false;
                    }
                    for (int j = i + 1; j < nPlayers; ++j) {
                        if (rectsOverlap(lay.playerTrays[static_cast<size_t>(i)].rect,
                                         lay.playerTrays[static_cast<size_t>(j)].rect)) {
                            traysClean = false;
                        }
                    }
                }
                check(traysClean, tag + ": All " + std::to_string(nPlayers) +
                                      " player board trays are separate & non-overlapping");

                // Verify all 7 zones per player lie inside that player's tray
                bool zonesInsideTray = true;
                const char* roles[] = {"avatar", "strikes", "marks",  "played",
                                       "hand",   "deck",    "discard"};
                for (int p = 0; p < nPlayers; ++p) {
                    const Rect& tr = lay.playerTrays[static_cast<size_t>(p)].rect;
                    for (const char* r : roles) {
                        const Zone* z = lay.zone(playerZoneId(p, r));
                        if (!z || !rectContains(tr, z->rect)) {
                            zonesInsideTray = false;
                        }
                    }
                }
                check(zonesInsideTray, tag + ": Every player has all 7 dedicated zones inside their tray");

                // Build setup and verify unique Avatars & unique Role cards across all players
                Table table(catalog, lay);
                table.reset(buildGameSetup(catalog, lay, 1000u + static_cast<std::uint32_t>(nPlayers), cfg));
                const TableState& st = table.state();

                check(st.playerCount == nPlayers &&
                          static_cast<int>(st.players.size()) == nPlayers,
                      tag + ": TableState initialized " + std::to_string(nPlayers) + " PlayerState entries");

                std::unordered_set<int> seenAvatars;
                std::unordered_set<int> seenRoles;
                bool startersValid = true;

                for (int p = 0; p < nPlayers; ++p) {
                    const int avId = st.players[static_cast<size_t>(p)].avatarDefId;
                    seenAvatars.insert(avId);

                    const Pile* avPile = table.findZonePile(playerZoneId(p, "avatar"));
                    if (!avPile || avPile->cards.size() != 1 || avPile->top().def != avId ||
                        !avPile->top().faceUp) {
                        startersValid = false;
                    }

                    int handCards = 0;
                    int deckCards = 0;
                    int expCount = 0;
                    int bruteCount = 0;
                    int roleDefFound = -1;

                    for (const auto& pile : st.piles) {
                        if (pile.zone == playerZoneId(p, "hand")) {
                            for (const auto& c : pile.cards) {
                                ++handCards;
                                if (!c.faceUp) startersValid = false;
                                if (c.def == 225)
                                    ++expCount;
                                else if (c.def == 224)
                                    ++bruteCount;
                                else
                                    roleDefFound = c.def;
                            }
                        } else if (pile.zone == playerZoneId(p, "deck")) {
                            for (const auto& c : pile.cards) {
                                ++deckCards;
                                if (c.faceUp) startersValid = false;
                                if (c.def == 225)
                                    ++expCount;
                                else if (c.def == 224)
                                    ++bruteCount;
                                else
                                    roleDefFound = c.def;
                            }
                        }
                    }

                    if (handCards != 6 || deckCards != 7 || expCount != 7 || bruteCount != 5 ||
                        roleDefFound != avId + 1) {
                        startersValid = false;
                    } else {
                        seenRoles.insert(roleDefFound);
                        const CardDef& rd = catalog.card(roleDefFound);
                        if (rd.attack < 0 && rd.recruit < 0) startersValid = false;
                    }
                }

                check(static_cast<int>(seenAvatars.size()) == nPlayers &&
                          static_cast<int>(seenRoles.size()) == nPlayers && startersValid,
                      tag + ": All " + std::to_string(nPlayers) +
                          " players have unique Avatars, unique Role cards with stats, 6-card Hand, & 7-card Deck");
            }
        }
    }

    // Multiplayer Turn Rotation, Cross-Player Coordinate, & Per-Player Elimination (3P)
    {
        BoardLayout lay3 = baseLayout;
        configurePlayerZones(lay3, 3);
        GameSetupConfig cfg3;
        cfg3.movie = 1;
        cfg3.playAsPredators = false;
        cfg3.playerCount = 3;
        cfg3.playerAvatars = defaultAvatarsForScenario(1, false);
        cfg3.avatarDefId = cfg3.playerAvatars[0];

        Table t3(catalog, lay3);
        t3.reset(buildGameSetup(catalog, lay3, 777u, cfg3));

        check(t3.state().activePlayer == 0 && t3.state().turnNumber == 1,
              "3P Turn Flow: Starts on Turn 1 with Player 1 (index 0)");

        t3.resolveEnemyPhase(101u);
        t3.playAllHandCards(102u);
        t3.resolveStrikePhase(103u);
        t3.resolveCleanupPhase(104u);

        int p0Hand = 0, p0Disc = 0, p1Hand = 0, p2Hand = 0;
        for (const auto& p : t3.state().piles) {
            if (p.zone == "p0_hand") p0Hand += static_cast<int>(p.cards.size());
            if (p.zone == "p0_discard") p0Disc += static_cast<int>(p.cards.size());
            if (p.zone == "p1_hand") p1Hand += static_cast<int>(p.cards.size());
            if (p.zone == "p2_hand") p2Hand += static_cast<int>(p.cards.size());
        }
        check(t3.state().activePlayer == 1 && t3.state().turnNumber == 2 && p0Hand == 6 &&
                  p0Disc == 6 && p1Hand == 6 && p2Hand == 6,
              "3P Turn Flow: Cleanup rotated activePlayer 0 -> 1, P1 redrew 6, P2 & P3 boards untouched");

        // Cross-player Coordinate during P2's turn (P3 lends a Coordinate card from p2_hand)
        t3.resolveEnemyPhase(201u);
        int coordDefId = -1;
        for (const auto& cd : catalog.cards) {
            if (hasKw(cd, "Coordinate") && (cd.recruit > 0 || cd.attack > 0)) {
                coordDefId = cd.id;
                break;
            }
        }
        check(coordDefId >= 0, "Found Coordinate hero card in catalog (defId=" +
                                   std::to_string(coordDefId) + ")");

        const int prevRec = t3.state().recruitPool;
        const int prevAtk = t3.state().attackPool;
        const PileId lentId = placeSingleInZone(t3, coordDefId, "p2_hand", true, false);
        t3.reflowZone("p2_hand");
        t3.coordinateCard(lentId, 202u);

        int p2HandAfterCoord = 0, p2PlayedAfterCoord = 0;
        for (const auto& p : t3.state().piles) {
            if (p.zone == "p2_hand") p2HandAfterCoord += static_cast<int>(p.cards.size());
            if (p.zone == "p2_played") p2PlayedAfterCoord += static_cast<int>(p.cards.size());
        }
        check(p2PlayedAfterCoord == 1 && p2HandAfterCoord == 7 &&
                  (t3.state().recruitPool > prevRec || t3.state().attackPool > prevAtk),
              "3P Coordinate: Teammate P3 lent card to p2_played, drew 1 replacement, and active P2 gained virtual stats");

        t3.resolveStrikePhase(203u);
        t3.resolveCleanupPhase(204u);

        bool coordReturnedToP3Discard = false;
        if (const Pile* p2Disc = t3.findZonePile("p2_discard")) {
            for (const auto& c : p2Disc->cards) {
                if (c.def == coordDefId) coordReturnedToP3Discard = true;
            }
        }
        check(t3.state().activePlayer == 2 && coordReturnedToP3Discard,
              "3P Coordinate Cleanup: Lent Coordinate card returned to lender P3's discard (p2_discard) & turn rotated to P3");

        // Per-player elimination: kill P3 while P1 & P2 survive -> game continues and skips P3!
        t3.resolveEnemyPhase(301u);
        t3.mutableState().players[2].manualStrikeOffset = 50;
        t3.syncStateFromZones();
        t3.resolveStrikePhase(302u);

        check(!t3.state().players[2].alive && t3.state().phase != TurnPhase::Defeat,
              "3P Elimination: Eliminating P3 marks P3 KIA without ending mission while P1 & P2 live");

        t3.resolveCleanupPhase(303u);
        check(t3.state().activePlayer == 0,
              "3P Elimination: Turn rotation cleanly rotates to living P1");

        // Advance P1 -> P2 -> skips KIA P3 -> back to P1!
        t3.resolveEnemyPhase(304u);
        t3.resolveStrikePhase(305u);
        t3.resolveCleanupPhase(306u);
        check(t3.state().activePlayer == 1, "3P Elimination: Turn rotated P1 -> P2");
        t3.resolveEnemyPhase(307u);
        t3.resolveStrikePhase(308u);
        t3.resolveCleanupPhase(309u);
        check(t3.state().activePlayer == 0,
              "3P Elimination: Turn rotation skipped KIA P3 (P2 -> P1)");
    }

    // =========================================================================
    // PART 2: ATTACH MECHANIC & GUERRILLA CAMP (ID 102, EXPENDABLE ASSETS - 1)
    // =========================================================================
    out << "\n=== PART 2: ATTACH MECHANIC & GUERRILLA CAMP (ID 102) ===\n";

    check(hasKw(catalog.card(102), "Attach") && hasKw(catalog.card(107), "Attach"),
          "Catalog: Guerrilla Camp (102) and Defensive Position (107) carry 'Attach' keyword");

    BoardLayout lay1 = baseLayout;
    configurePlayerZones(lay1, 1);
    GameSetupConfig cfg1;
    cfg1.movie = 1;
    cfg1.playAsPredators = false;
    cfg1.playerCount = 1;
    cfg1.playerAvatars = defaultAvatarsForScenario(1, false);
    cfg1.avatarDefId = cfg1.playerAvatars[0];

    // 2A. Face-Down Guerrilla Camp moves normally right-to-left during Phase 1
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 500u, cfg1));
        clearWildsAndCombat(t);

        placeSingleInZone(t, 102, "wilds_4", false, false);
        t.mutableState().phase = TurnPhase::EnemyPhase;
        t.resolveEnemyPhase(501u);

        const Pile* w3 = t.findZonePile("wilds_3");
        check(w3 && w3->cards.size() == 1 && w3->top().def == 102 && !w3->top().faceUp &&
                  !w3->top().attached,
              "Guerrilla Camp (Face-Down): Moves normally from Underground (wilds_4) to Graveyard (wilds_3) during Phase 1");
    }

    // 2B. Reveal Powers: Attaches to River (0 Events), Graveyard (1 Event), Underground (2 Events)
    for (int evCount = 0; evCount <= 2; ++evCount) {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 600u + static_cast<std::uint32_t>(evCount), cfg1));
        clearWildsAndCombat(t);

        // Put `evCount` Expendable Assets - 1 Event cards (id 105) in discard_hazards
        if (evCount > 0) {
            const PileId dpid = placeSingleInZone(t, 105, "discard_hazards", true, false);
            if (evCount == 2) {
                if (Pile* dp = t.find(dpid)) {
                    dp->cards.push_back(CardInst{t.mutableState().nextUid++, 105, true, false});
                }
            }
        }

        const PileId campInHills = placeSingleInZone(t, 102, "wilds_0", false, false);
        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().attackPool = 10;
        t.scanWilds(campInHills, 610u);

        const int expectedSlot = 2 + evCount; // 2=River, 3=Graveyard, 4=Underground
        const std::string expectedZid = "wilds_" + std::to_string(expectedSlot);
        const Pile* dst = t.findZonePile(expectedZid);
        const Pile* src = t.findZonePile("wilds_0");

        check((src == nullptr || src->cards.empty()) && dst && dst->cards.size() == 1 &&
                  dst->top().def == 102 && dst->top().faceUp && dst->top().attached,
              "Guerrilla Camp Reveal (" + std::to_string(evCount) +
                  " discarded Events): Attached face-up to " + expectedZid);
    }

    // 2C. Face-Down Guerrilla Camp pushed off Hills into Combat Zone reveals & attaches to River (+ Events)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 650u, cfg1));
        clearWildsAndCombat(t);

        placeSingleInZone(t, 105, "discard_hazards", true, false); // 1 discarded Event -> Graveyard (wilds_3)
        placeSingleInZone(t, 102, "wilds_0", false, false);
        placeSingleInZone(t, 104, "wilds_1", false, false);
        placeSingleInZone(t, 104, "wilds_2", false, false);
        placeSingleInZone(t, 104, "wilds_3", false, false);
        placeSingleInZone(t, 104, "wilds_4", false, false);

        t.mutableState().phase = TurnPhase::EnemyPhase;
        t.resolveEnemyPhase(651u); // Full Wilds pushes wilds_0 into Combat Zone!

        const Pile* w3 = t.findZonePile("wilds_3");
        const Pile* combat = t.findZonePile("combat");
        check((combat == nullptr || combat->cards.empty()) && w3 && w3->cards.size() == 2 &&
                  w3->cards.front().def == 102 && w3->cards.front().attached &&
                  w3->cards.back().def == 104 && !w3->cards.back().attached,
              "Guerrilla Camp pushed out of Hills: Revealed & attached at bottom of Graveyard (2-card stack [102, 104])");
    }

    // 2D. 2-Card Stack & Regular Phase 1 Movement Immunity
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 700u, cfg1));
        clearWildsAndCombat(t);

        // Attach Guerrilla Camp to River (wilds_2), place Cartel Guard (104) at Graveyard (wilds_3) & Underground (wilds_4)
        placeSingleInZone(t, 102, "wilds_2", true, true);
        placeSingleInZone(t, 104, "wilds_3", true, false);
        placeSingleInZone(t, 103, "wilds_4", false, false);

        // Ensure top of prey deck is a plain enemy (104) so Phase 1 is deterministic
        if (Pile* prey = t.findZonePile("prey")) {
            prey->cards.push_back(CardInst{t.mutableState().nextUid++, 104, false, false});
        }

        t.mutableState().phase = TurnPhase::EnemyPhase;
        t.resolveEnemyPhase(701u);

        const Pile* riverAfter1 = t.findZonePile("wilds_2");
        check(riverAfter1 && riverAfter1->cards.size() == 2 &&
                  riverAfter1->cards.front().def == 102 && riverAfter1->cards.front().attached &&
                  riverAfter1->cards.back().def == 104 && !riverAfter1->cards.back().attached,
              "Phase 1 Stacking: Incoming enemy stacked ON TOP of attached Guerrilla Camp at River ([102 bottom, 104 top])");

        // Second Phase 1 draw must push ONLY the top card (104) from River (wilds_2) to Ruins (wilds_1),
        // while Guerrilla Camp (102) stays immune in River!
        if (Pile* prey = t.findZonePile("prey")) {
            prey->cards.push_back(CardInst{t.mutableState().nextUid++, 104, false, false});
        }
        t.mutableState().phase = TurnPhase::EnemyPhase;
        t.resolveEnemyPhase(702u);

        const Pile* ruinsAfter2 = t.findZonePile("wilds_1");
        const Pile* riverAfter2 = t.findZonePile("wilds_2");
        check(ruinsAfter2 && ruinsAfter2->cards.size() == 1 && ruinsAfter2->top().def == 104 &&
                  riverAfter2 && riverAfter2->cards.size() == 2 &&
                  riverAfter2->cards.front().def == 102 && riverAfter2->cards.front().attached &&
                  riverAfter2->cards.back().def == 103,
              "Phase 1 Immunity: Top card pushed over River -> Ruins while attached Guerrilla Camp stayed anchored at bottom of River!");
    }

    // 2E. Event Card from Expendable Assets - 1 (id 105) Moves Attached Guerrilla Camp Right
    //     (Tested when Event is scanned directly ON TOP of Guerrilla Camp at River -> moves to occupied Graveyard!)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 800u, cfg1));
        clearWildsAndCombat(t);

        const PileId riverPid = placeSingleInZone(t, 102, "wilds_2", true, true);
        if (Pile* rp = t.find(riverPid)) {
            // Stack hidden Event (105) on top of attached Guerrilla Camp (102) at River
            rp->cards.push_back(CardInst{t.mutableState().nextUid++, 105, false, false});
        }
        placeSingleInZone(t, 104, "wilds_3", false, false); // Occupied Graveyard

        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().attackPool = 10;
        t.scanWilds(riverPid, 801u);

        const Pile* riverNow = t.findZonePile("wilds_2");
        const Pile* graveNow = t.findZonePile("wilds_3");
        check((riverNow == nullptr || riverNow->cards.empty()) && graveNow &&
                  graveNow->cards.size() == 2 && graveNow->cards.front().def == 102 &&
                  graveNow->cards.front().attached && graveNow->cards.back().def == 104 &&
                  t.countDiscardedEvents() == 1,
              "Event Reveal (Expendable Assets - 1): Scanning Event on River popped Event to discard_hazards and moved Guerrilla Camp right to bottom of Graveyard!");
    }

    // 2F. Discard Path 1: Killing Val Verde Guerrilla (id 103) Moves Guerrilla Camp Left -> Combat Zone -> Discarded Events + Objective 1 Complete
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 900u, cfg1));
        clearWildsAndCombat(t);

        // Guerrilla Camp attached at bottom of Ruins (wilds_1) underneath Cartel Guard (104)
        const PileId ruinsPid = placeSingleInZone(t, 102, "wilds_1", true, true);
        if (Pile* rp = t.find(ruinsPid)) {
            rp->cards.push_back(CardInst{t.mutableState().nextUid++, 104, true, false});
        }
        const PileId vvg1 = placeSingleInZone(t, 103, "wilds_3", true, false);
        const PileId vvg2 = placeSingleInZone(t, 103, "combat", true, false);

        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().attackPool = 20;

        // Kill 1st Val Verde Guerrilla -> Camp moves left from bottom of Ruins (wilds_1) to Hills (wilds_0)
        t.fightEnemy(vvg1, 901u);
        const Pile* ruinsAfter1 = t.findZonePile("wilds_1");
        const Pile* hillsAfter1 = t.findZonePile("wilds_0");
        check(ruinsAfter1 && ruinsAfter1->cards.size() == 1 && ruinsAfter1->top().def == 104 &&
                  hillsAfter1 && hillsAfter1->cards.size() == 1 && hillsAfter1->top().def == 102 &&
                  hillsAfter1->top().attached && t.state().currentObjective == 1,
              "Val Verde Guerrilla Death #1: Moved attached Guerrilla Camp left from bottom of Ruins -> Hills");

        // Kill 2nd Val Verde Guerrilla -> Camp moves left from Hills into Combat Zone -> Discarded Events & Objective 1 Complete!
        t.fightEnemy(vvg2, 902u);
        const Pile* hillsAfter2 = t.findZonePile("wilds_0");
        const Pile* discHaz = t.findZonePile("discard_hazards");
        bool campInDiscardedEvents = false;
        if (discHaz) {
            for (const auto& c : discHaz->cards) {
                if (c.def == 102 && !c.attached) campInDiscardedEvents = true;
            }
        }
        check((hillsAfter2 == nullptr || hillsAfter2->cards.empty()) && campInDiscardedEvents &&
                  t.state().currentObjective == 2,
              "Val Verde Guerrilla Death #2: Moved Guerrilla Camp left from Hills -> Combat Zone -> Discarded Events & completed Objective 1!");
    }

    // 2G. Discard Path 2: Clear Location + Spend 1 Attack Power -> Discarded Events + Objective 1 Complete
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 950u, cfg1));
        clearWildsAndCombat(t);

        // Guerrilla Camp attached at River (wilds_2) underneath Soviet Advisor (104, 5 ATK)
        const PileId riverPid = placeSingleInZone(t, 102, "wilds_2", true, true);
        if (Pile* rp = t.find(riverPid)) {
            rp->cards.push_back(CardInst{t.mutableState().nextUid++, 104, true, false});
        }

        check(!t.isWildsSpaceClear(2),
              "Clear-Space Check: River is NOT clear while Soviet Advisor sits on top of Guerrilla Camp");

        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().attackPool = 6; // Exactly 5 /// for Soviet Advisor + 1 /// for Guerrilla Camp!

        // 1st fight clears Soviet Advisor (5 ///) off the top, exposing Guerrilla Camp
        t.fightEnemy(riverPid, 951u);
        const Pile* riverMid = t.findZonePile("wilds_2");
        check(t.state().attackPool == 1 && t.isWildsSpaceClear(2) && riverMid &&
                  riverMid->cards.size() == 1 && riverMid->top().def == 102 &&
                  t.state().currentObjective == 1,
              "Clear-Space Step 1: Defeated top Soviet Advisor (5 ///), leaving Guerrilla Camp alone at River with 1 /// remaining");

        // 2nd fight spends 1 /// Attack to send Guerrilla Camp to Discarded Events & complete Objective 1!
        t.fightEnemy(riverMid->id, 952u);
        const Pile* riverEnd = t.findZonePile("wilds_2");
        const Pile* discHaz = t.findZonePile("discard_hazards");
        bool campInDiscardedEvents = false;
        if (discHaz) {
            for (const auto& c : discHaz->cards) {
                if (c.def == 102 && !c.attached) campInDiscardedEvents = true;
            }
        }
        check(t.state().attackPool == 0 && (riverEnd == nullptr || riverEnd->cards.empty()) &&
                  campInDiscardedEvents && t.state().currentObjective == 2,
              "Clear-Space Step 2: Spent 1 /// Attack on clear River -> Guerrilla Camp sent to Discarded Events & completed Objective 1!");
    }

    out << "\n=============================================================\n";
    out << "VERIFICATION SUMMARY: " << passed << " PASSED, " << failed << " FAILED\n";
    out << "=============================================================\n";

    if (!reportPath.empty()) {
        std::filesystem::create_directories(reportPath.parent_path());
        std::ofstream f(reportPath, std::ios::trunc);
        f << out.str();
    }

    return (failed == 0) ? 0 : 1;
}

} // namespace lc
