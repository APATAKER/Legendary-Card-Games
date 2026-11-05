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
        t.syncStateFromZones();
        t.mutableState().attackPool = 6; // Exactly 5 /// for Soviet Advisor + 1 /// for Guerrilla Camp!

        // 1st fight clears Soviet Advisor (5 ///) off the top, exposing Guerrilla Camp
        t.fightEnemy(riverPid, 951u);
        const Pile* riverMid = t.findZonePile("wilds_2");
        check(t.state().attackPool == 1 && t.isWildsSpaceClear(2) && riverMid &&
                  riverMid->cards.size() == 1 && riverMid->top().def == 102 &&
                  t.state().currentObjective == 1,
              "Clear-Space Step 1: Defeated top Soviet Advisor (5 ///), leaving Guerrilla Camp alone at River with 1 /// remaining");

        // 2nd fight spends 1 /// Attack on clear River -> Guerrilla Camp sent to Discarded Events & complete Objective 1!
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

    // =========================================================================
    // PART 3: ISSUES A1–A9 PLAYTHROUGH DISCREPANCY FIXES
    // =========================================================================
    out << "\n=== PART 3: ISSUES A1-A9 PLAYTHROUGH DISCREPANCY FIXES ===\n";

    // A1: Per-round hand/played Class & Team metadata + [Class]/[Team] count >= 2 rule (Scenario 1, 2, 3)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 1100u, cfg1));
        t.mutableState().phase = TurnPhase::ActionPhase;

        // Scenario 1: 1x Sexual Tyrannosaurus (id 0, Survival) + 5x Experience -> countTurnClass("Survival") == 1 -> cannot activate!
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        const PileId s1Pid = placeSingleInZone(t, 188, "p0_strikes", true, false); // 2 dmg
        const std::uint32_t strike1Uid = t.find(s1Pid)->top().uid;
        const PileId blain1Pid = placeSingleInZone(t, 0, "p0_hand", true, false);
        const std::uint32_t blain1Uid = t.find(blain1Pid)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        const bool hasSurvivalInRoundMeta =
            std::find(t.state().roundHandClasses.begin(), t.state().roundHandClasses.end(),
                      "Survival") != t.state().roundHandClasses.end();
        const bool hasDutchTeamInRoundMeta =
            std::find(t.state().roundHandTeams.begin(), t.state().roundHandTeams.end(),
                      "Dutch's Team") != t.state().roundHandTeams.end();
        check(hasSurvivalInRoundMeta && hasDutchTeamInRoundMeta &&
                  !t.canActivateCardAbility(blain1Uid),
              "Issue A1 (Scenario 1): 1x Sexual Tyrannosaurus alone does NOT activate [Survival] ability");

        // Scenario 2 & 3: Add a 2nd Sexual Tyrannosaurus (id 0, Survival) + a 2nd Strike -> countTurnClass("Survival") == 2 -> BOTH cards trigger once!
        std::uint32_t strike2Uid = 0;
        if (Pile* sp = t.findZonePile("p0_strikes")) {
            strike2Uid = t.mutableState().nextUid++;
            sp->cards.push_back(CardInst{strike2Uid, 188, true, false});
        }
        const PileId blain2Pid = placeSingleInZone(t, 0, "p0_hand", true, false);
        const std::uint32_t blain2Uid = t.find(blain2Pid)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        check(t.canActivateCardAbility(blain1Uid) && t.canActivateCardAbility(blain2Uid),
              "Issue A1 (Scenario 3): With 2x Sexual Tyrannosaurus in hand, both cards' [Survival] abilities are ready (1 trigger each)");

        t.activateCardAbility(blain1Uid, 1101u);
        t.resolveHealStrikeChoice(0, strike1Uid, 1102u);
        t.activateCardAbility(blain2Uid, 1103u);
        t.resolveHealStrikeChoice(0, strike2Uid, 1104u);

        check(t.state().strikeDamage == 0 && !t.canActivateCardAbility(blain1Uid) &&
                  !t.canActivateCardAbility(blain2Uid),
              "Issue A1 (Scenario 3 Execution): Both Sexual Tyrannosaurus cards triggered once and healed 2 Strikes");
    }

    // A2: Open Wound (id 194) deals 0 damage and prevents other Strikes from being healed first
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 1200u, cfg1));
        t.mutableState().phase = TurnPhase::ActionPhase;

        check(catalog.card(194).attack == 0, "Issue A2 (Catalog): Open Wound (194) attack == 0");

        const PileId owPid = placeSingleInZone(t, 194, "p0_strikes", true, false); // Open Wound (0 dmg)
        const std::uint32_t owUid = t.find(owPid)->top().uid;
        t.syncStateFromZones();
        check(t.state().strikeDamage == 0,
              "Issue A2 (0 Damage): Open Wound in p0_strikes contributes 0 strikeDamage");

        // Add a 3-damage Strike (189) alongside Open Wound (194) and activate Mac 4 Uncommon (14) via Brothers in Arms
        std::uint32_t lacUid = 0;
        if (Pile* sp = t.findZonePile("p0_strikes")) {
            lacUid = t.mutableState().nextUid++;
            sp->cards.push_back(CardInst{lacUid, 189, true, false});
        }
        t.syncStateFromZones();
        check(t.state().strikeDamage == 3,
              "Issue A2 (Combined Damage): Open Wound (0) + Laceration (3) == 3 strikeDamage");

        placeSingleInZone(t, 4, "p0_hand", true, false); // Dillon
        const PileId macPid = placeSingleInZone(t, 14, "p0_hand", true, false);
        const std::uint32_t macUid = t.find(macPid)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        t.activateCardAbility(macUid, 1201u);
        // Attempting to heal the 3-damage Strike (lacUid) must be rejected while Open Wound (owUid) is present!
        t.resolveHealStrikeChoice(0, lacUid, 1202u);
        check(t.state().pendingHealChoice,
              "Issue A2 (Heal Restriction): Attempting to heal non-Open-Wound Strike is blocked while Open Wound is present");
        t.resolveHealStrikeChoice(0, owUid, 1203u);

        const Pile* spAfter = t.findZonePile("p0_strikes");
        check(!t.state().pendingHealChoice && spAfter && spAfter->cards.size() == 1 &&
                  spAfter->top().def == 189 && t.state().strikeDamage == 3,
              "Issue A2 (Open Wound Healed First): Open Wound (194) healed to discard_strikes before 3-damage Strike (189)");
    }

    // A3: Permanent Damage (id 190) has 3 damage, can be healed to discard_strikes, and draws a replacement Strike when healed
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 1300u, cfg1));
        t.mutableState().phase = TurnPhase::ActionPhase;

        check(catalog.card(190).attack == 3, "Issue A3 (Catalog): Permanent Damage (190) attack == 3");

        // Put a known 2-damage Strike (188) on top of the Strike deck, and Permanent Damage (190) in p0_strikes
        if (Pile* sDeck = t.findZonePile("strikes")) {
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 188, false, false});
        }
        const PileId permPid = placeSingleInZone(t, 190, "p0_strikes", true, false);
        const std::uint32_t permUid = t.find(permPid)->top().uid;
        t.syncStateFromZones();
        check(t.state().strikeDamage == 3,
              "Issue A3 (Damage): Permanent Damage deals 3 strikeDamage");

        placeSingleInZone(t, 8, "p0_hand", true, false); // Dutch 2 Common (Stick Around)
        const PileId macPid = placeSingleInZone(t, 14, "p0_hand", true, false);
        const std::uint32_t macUid = t.find(macPid)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        t.activateCardAbility(macUid, 1301u);
        t.resolveHealStrikeChoice(0, permUid, 1302u);

        bool permInDiscardStrikes = false;
        if (const Pile* ds = t.findZonePile("discard_strikes")) {
            for (const auto& c : ds->cards) {
                if (c.def == 190) permInDiscardStrikes = true;
            }
        }
        const Pile* spAfter = t.findZonePile("p0_strikes");
        check(permInDiscardStrikes && spAfter && spAfter->cards.size() == 1 &&
                  spAfter->top().def == 188 && t.state().strikeDamage == 2,
              "Issue A3 (Heal & Draw Strike): Permanent Damage healed to discard_strikes and drew replacement Strike (188)");
    }

    // A4: Cover Me! (id 191, 4 dmg) opens interactive modal where teammates select >= 4 Attack from their hands to discard and cancel
    {
        BoardLayout lay3 = baseLayout;
        configurePlayerZones(lay3, 3);
        GameSetupConfig cfg3;
        cfg3.movie = 1;
        cfg3.playAsPredators = false;
        cfg3.playerCount = 3;
        cfg3.playerAvatars = defaultAvatarsForScenario(1, false);
        cfg3.avatarDefId = cfg3.playerAvatars[0];

        Table t(catalog, lay3);
        t.reset(buildGameSetup(catalog, lay3, 1400u, cfg3));
        clearWildsAndCombat(t);

        check(catalog.card(191).attack == 4, "Issue A4 (Catalog): Cover Me! (191) attack == 4");

        // Give P2 and P3 one 2-Attack card each (Dutch 2 Common id 8, attack 2) in their hands
        const PileId p2AtkPid = placeSingleInZone(t, 8, "p1_hand", true, false);
        const PileId p3AtkPid = placeSingleInZone(t, 8, "p2_hand", true, false);
        const std::uint32_t p2AtkUid = t.find(p2AtkPid)->top().uid;
        const std::uint32_t p3AtkUid = t.find(p3AtkPid)->top().uid;
        t.reflowZone("p1_hand");
        t.reflowZone("p2_hand");

        int otherHandsBefore = 0;
        for (const auto& p : t.state().piles) {
            if (p.zone == "p1_hand" || p.zone == "p2_hand")
                otherHandsBefore += static_cast<int>(p.cards.size());
        }

        // Put Cover Me! (191) on top of Strike deck and have an Enemy strike P1
        if (Pile* sDeck = t.findZonePile("strikes")) {
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 191, false, false});
        }
        placeSingleInZone(t, 104, "combat", true, false);
        t.mutableState().phase = TurnPhase::StrikePhase;
        t.resolveStrikePhase(1401u);

        check(t.state().pendingCoverMeChoice,
              "Issue A4 (Cover Me! Modal): Drawing Cover Me! opens interactive teammate discard modal");

        t.toggleCoverMeCard(p2AtkUid);
        t.toggleCoverMeCard(p3AtkUid);
        check(t.selectedCoverMeAttackTotal() == 4,
              "Issue A4 (Cover Me! Selection): Selected teammate cards total 4 /// Attack");
        t.resolveCoverMeChoice(true, 1402u);

        int otherHandsAfter = 0;
        for (const auto& p : t.state().piles) {
            if (p.zone == "p1_hand" || p.zone == "p2_hand")
                otherHandsAfter += static_cast<int>(p.cards.size());
        }
        bool coverMeInDiscard = false;
        if (const Pile* ds = t.findZonePile("discard_strikes")) {
            for (const auto& c : ds->cards) {
                if (c.def == 191) coverMeInDiscard = true;
            }
        }
        check(!t.state().pendingCoverMeChoice && t.state().players[0].strikeDamage == 0 &&
                  coverMeInDiscard && otherHandsAfter == otherHandsBefore - 2,
              "Issue A4 (Cover Me! Canceled): Teammates discarded chosen >= 4 Attack from hand (un-refilled) to cancel Cover Me! into discard_strikes");
    }

    // A5: Sacrifice (id 192) deals 5 damage, prompts player to choose any living player, and kills chosen player if HP <= 0
    {
        BoardLayout lay2 = baseLayout;
        configurePlayerZones(lay2, 2);
        GameSetupConfig cfg2;
        cfg2.movie = 1;
        cfg2.playAsPredators = false;
        cfg2.playerCount = 2;
        cfg2.playerAvatars = defaultAvatarsForScenario(1, false);
        cfg2.avatarDefId = cfg2.playerAvatars[0];

        Table t(catalog, lay2);
        t.reset(buildGameSetup(catalog, lay2, 1500u, cfg2));
        clearWildsAndCombat(t);

        check(catalog.card(192).attack == 5, "Issue A5 (Catalog): Sacrifice (192) attack == 5");

        // Wound P1 for 6 damage and P2 for 5 damage (so taking 5-damage Sacrifice will bring P2 to 10/10 dmg -> HP <= 0 -> KIA!)
        t.mutableState().players[0].manualStrikeOffset = 6;
        t.mutableState().players[1].manualStrikeOffset = 5;
        t.syncStateFromZones();

        if (Pile* sDeck = t.findZonePile("strikes")) {
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 192, false, false});
        }
        placeSingleInZone(t, 104, "combat", true, false);
        t.mutableState().phase = TurnPhase::StrikePhase;
        t.resolveStrikePhase(1501u);

        check(t.state().pendingSacrificeChoice && t.state().pendingSacrificeSourcePlayer == 0 &&
                  t.state().pendingSacrificeCard.def == 192,
              "Issue A5 (Sacrifice Choice Prompt): Drawing Sacrifice in multiplayer opens player choice prompt");

        // Choose Player 2 (index 1) to take Sacrifice (5 damage) -> P2 takes 5 damage and dies because HP <= 0!
        t.resolveSacrificeChoice(1);

        check(!t.state().pendingSacrificeChoice && t.state().players[0].alive &&
                  t.state().players[0].strikeDamage == 6 && !t.state().players[1].alive,
              "Issue A5 (Sacrifice Chosen Player Can Die): Choosing P2 dealt 5 damage to P2 and triggered P2's death when HP <= 0");
    }

    // A6: Festering Wound (id 193) deals 0 damage initially, then discards at start of next Enemy Phase to draw 2 Strikes
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 1600u, cfg1));
        clearWildsAndCombat(t);

        check(catalog.card(193).attack == 0, "Issue A6 (Catalog): Festering Wound (193) attack == 0");

        placeSingleInZone(t, 193, "p0_strikes", true, false);
        t.syncStateFromZones();
        check(t.state().strikeDamage == 0,
              "Issue A6 (0 Initial Damage): Festering Wound deals 0 damage while in p0_strikes");

        // Put two 2-damage Strikes (188) on top of Strike deck and start P1's next Enemy Phase
        if (Pile* sDeck = t.findZonePile("strikes")) {
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 188, false, false});
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 188, false, false});
        }
        t.mutableState().phase = TurnPhase::EnemyPhase;
        t.resolveEnemyPhase(1601u);

        bool festeringInDiscard = false;
        if (const Pile* ds = t.findZonePile("discard_strikes")) {
            for (const auto& c : ds->cards) {
                if (c.def == 193) festeringInDiscard = true;
            }
        }
        const Pile* spAfter = t.findZonePile("p0_strikes");
        check(festeringInDiscard && spAfter && spAfter->cards.size() == 2 &&
                  t.state().strikeDamage == 4,
              "Issue A6 (Enemy Phase Trigger): Festering Wound discarded at start of Enemy Phase and drew 2 new Strikes (4 dmg total)");
    }

    // A7: A Whole New Level Of Pain (id 187, 1 dmg) kills all other players when its holder dies (unless death-immune)
    {
        BoardLayout lay3 = baseLayout;
        configurePlayerZones(lay3, 3);
        GameSetupConfig cfg3;
        cfg3.movie = 1;
        cfg3.playAsPredators = false;
        cfg3.playerCount = 3;
        cfg3.playerAvatars = defaultAvatarsForScenario(1, false);
        cfg3.avatarDefId = cfg3.playerAvatars[0];

        Table t(catalog, lay3);
        t.reset(buildGameSetup(catalog, lay3, 1700u, cfg3));
        clearWildsAndCombat(t);

        check(catalog.card(187).attack == 1,
              "Issue A7 (Catalog): A Whole New Level Of Pain (187) attack == 1");

        // Give P1 A Whole New Level Of Pain (187) + fatal damage -> P1 dies, triggering P2 & P3 deaths -> Defeat!
        placeSingleInZone(t, 187, "p0_strikes", true, false);
        t.mutableState().players[0].manualStrikeOffset = 10;
        t.syncStateFromZones();
        t.mutableState().phase = TurnPhase::StrikePhase;
        t.resolveStrikePhase(1701u);

        check(!t.state().players[0].alive && !t.state().players[1].alive &&
                  !t.state().players[2].alive && t.state().phase == TurnPhase::Defeat,
              "Issue A7 (Chain Death): Player dying with A Whole New Level Of Pain killed all other unprotected players and ended the game");
    }

    // A8: Stunning Blow (id 186, 1 dmg) draws 1 extra Strike for each Mark the receiving player has
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 1800u, cfg1));
        clearWildsAndCombat(t);

        check(catalog.card(186).attack == 1, "Issue A8 (Catalog): Stunning Blow (186) attack == 1");

        // Give P1 2 Marks
        t.mutableState().players[0].manualMarksOffset = 2;
        t.syncStateFromZones();

        // Stack Strike deck: top is Stunning Blow (186, 1 dmg), beneath it are two 2-dmg Strikes (188)
        if (Pile* sDeck = t.findZonePile("strikes")) {
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 188, false, false});
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 188, false, false});
            sDeck->cards.push_back(CardInst{t.mutableState().nextUid++, 186, false, false});
        }
        placeSingleInZone(t, 104, "combat", true, false);
        t.mutableState().phase = TurnPhase::StrikePhase;
        t.resolveStrikePhase(1801u);

        const Pile* spAfter = t.findZonePile("p0_strikes");
        check(spAfter && spAfter->cards.size() == 3 && t.state().strikeDamage == 5,
              "Issue A8 (Stunning Blow Marks): Receiving Stunning Blow with 2 Marks dealt 1 dmg and drew 2 extra Strikes (1 + 2 + 2 = 5 dmg)");
    }

    // A9: Class Icon / Color Mappings (Green Fist = Strength, Red = Survival, Blue = Leadership, Yellow = Intel, Black = Tech)
    {
        check(catalog.card(0).classType == "Survival" &&
                  catalog.card(3).classType == "Strength" &&
                  catalog.card(4).classType == "Survival" &&
                  catalog.card(6).classType == "Strength" &&
                  catalog.card(9).classType == "Strength" &&
                  catalog.card(11).classType == "Survival" &&
                  catalog.card(13).classType == "Strength" &&
                  catalog.card(14).classType == "Survival",
              "Issue A9 (Barracks Heroes): Blain, Dillon, Dutch, and Mac Strength/Survival classes match card icons");
        check(catalog.card(64).classType == "Survival" &&
                  catalog.card(66).classType == "Intel" &&
                  catalog.card(70).classType == "Strength" &&
                  catalog.card(80).classType == "Leadership" &&
                  catalog.card(84).classType == "Intel",
              "Issue A9 (Avatars & Roles): CIA Agent, Detective, Guerrilla, SWAT Officer, and City Hunter classes match card icons");
        check(catalog.card(209).classType == "Survival" &&
                  catalog.card(211).classType == "Strength" &&
                  catalog.card(226).classType == "Survival" &&
                  catalog.card(229).classType == "Strength" &&
                  catalog.card(231).classType == "Survival" &&
                  catalog.card(233).classType == "Strength",
              "Issue A9 (Young Blood & Starters): Young Blood (209, 211), Commanders (226, 229), and Killer Instincts (231, 233) match card icons");
    }

    // =========================================================================
    // PART 4: B-LEVEL & CARD-BY-CARD REVIEW MECHANICS
    // =========================================================================
    out << "\n=== PART 4: B-LEVEL & CARD-BY-CARD REVIEW MECHANICS ===\n";

    // 4A. Auto-Available Hand Stats & On-Spend Auto-Playing (Starters Prioritized First)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2000u, cfg1));
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());

        // Hand: 2x Experience (225, 1*), 1x Pushing Pencils (5, 2*), 1x Brute Strength (224, 1///)
        placeSingleInZone(t, 225, "p0_hand", true, false);
        placeSingleInZone(t, 225, "p0_hand", true, false);
        placeSingleInZone(t, 5, "p0_hand", true, false);
        placeSingleInZone(t, 224, "p0_hand", true, false);
        t.reflowZone("p0_hand");
        t.mutableState().phase = TurnPhase::ActionPhase;
        t.syncStateFromZones();

        check(t.state().recruitPool == 4 && t.state().attackPool == 1,
              "Auto-Available Hand Stats: Recruit (4 *) and Attack (1 ///) are immediately available in Action Phase without clicking Play All");

        // Recruit a Commander (costs 3 *) -> must auto-play both 1* Experience starters first, plus 1 hero card!
        if (const Pile* cmd = t.findZonePile("commanders")) {
            t.recruitCard(cmd->id, 2001u);
        }
        int startersInHand = 0, startersInPlayed = 0;
        for (const auto& p : t.state().piles) {
            if (p.zone == "p0_hand" && !p.cards.empty() && p.top().def == 225) ++startersInHand;
            if (p.zone == "p0_played" && !p.cards.empty() && p.top().def == 225) ++startersInPlayed;
        }
        check(t.state().recruitPool == 1 && startersInHand == 0 && startersInPlayed == 2,
              "On-Spend Auto-Play: Recruiting a 3* Commander auto-played both Experience (225) Starters first");
    }

    // 4B. Brothers in Arms Exact Scaling Rule (0..3 triggers across Blain, Dillon, Dutch, Mac)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2100u, cfg1));
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().phase = TurnPhase::ActionPhase;

        // Scenario 1 & 2: 2x Bad Idea Dutch (id 10, base 1 ///) -> 0 other Barracks chars -> 2 * 1 = 2 ///
        placeSingleInZone(t, 10, "p0_hand", true, false);
        placeSingleInZone(t, 10, "p0_hand", true, false);
        t.syncStateFromZones();
        check(t.state().attackPool == 2,
              "Brothers in Arms (Scenario 2): 2x Bad Idea (Dutch) with 0 other Barracks characters = 1 /// each (2 /// total)");

        // Scenario 3 & 4: Add 2x Sexual Tyrannosaurus Blain (id 0, 2 * Recruit each) -> 1 other character (BLAIN) -> each Bad Idea gets 1 + 2 = 3 ///!
        placeSingleInZone(t, 0, "p0_hand", true, false);
        placeSingleInZone(t, 0, "p0_hand", true, false);
        t.syncStateFromZones();
        // Total Attack = 2 * 3 (Bad Idea) = 6 ///; Total Recruit = 2 * 2 (Blain) = 4 *
        check(t.state().attackPool == 6 && t.state().recruitPool == 4,
              "Brothers in Arms (Scenario 4): Adding 2x Blain gives each Bad Idea +2 /// (3 /// each = 6 /// total)");

        // Scenario 7: Clear hand and put 1x Bad Idea (Dutch 10), 1x Before Anybody Knows (Dillon 6), 1x Sexual Tyrannosaurus (Blain 0), 1x Same Kind of Jungle (Mac 13)
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().accountedCardRecruit.clear();
        t.mutableState().accountedCardAttack.clear();
        t.mutableState().accountedPlayedUids.clear();
        t.mutableState().recruitPool = 0;
        t.mutableState().attackPool = 0;

        const PileId dutch10 = placeSingleInZone(t, 10, "p0_hand", true, false); // Bad Idea (1 + 2*3 = 7 ///)
        const PileId dillon6 = placeSingleInZone(t, 6, "p0_hand", true, false);  // Before Anybody Knows (1 + 2*3 = 7 *)
        placeSingleInZone(t, 0, "p0_hand", true, false);                         // Blain (2 *)
        placeSingleInZone(t, 13, "p0_hand", true, false);                        // Mac (2 ///)
        t.syncStateFromZones();

        int dutchRec = 0, dutchAtk = 0, dillonRec = 0, dillonAtk = 0;
        t.effectiveCardStats(t.find(dutch10)->top(), &dutchRec, &dutchAtk);
        t.effectiveCardStats(t.find(dillon6)->top(), &dillonRec, &dillonAtk);
        check(dutchAtk == 7 && dillonRec == 7 && t.state().recruitPool == 9 &&
                  t.state().attackPool == 9,
              "Brothers in Arms (Scenario 7): With all 4 Barracks characters (Blain, Dillon, Dutch, Mac), Bad Idea reaches 7 /// and Before Anybody Knows reaches 7 *!");
    }

    // 4C. Stick Around (id 8): Optional Choose Card to Kill Modal + KILLED Badge in Played Area Until Cleanup
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2200u, cfg1));
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().phase = TurnPhase::ActionPhase;

        const PileId stickPid = placeSingleInZone(t, 8, "p0_hand", true, false); // Stick Around (Dutch's Team, 2 ///)
        const PileId expPid = placeSingleInZone(t, 225, "p0_hand", true, false); // Experience (1 *)
        const PileId blainPid = placeSingleInZone(t, 0, "p0_hand", true, false); // Blain (Dutch's Team, 2 *)
        const std::uint32_t stickUid = t.find(stickPid)->top().uid;
        const std::uint32_t expUid = t.find(expPid)->top().uid;
        (void)blainPid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        // Playing Stick Around does NOT auto-kill Experience!
        t.playCard(stickPid, 2201u);
        const Pile* deadBefore = t.findZonePile("dead_characters");
        check((deadBefore == nullptr || deadBefore->cards.empty()) &&
                  t.canActivateCardAbility(stickUid),
              "Stick Around (Optional): Playing Stick Around does NOT auto-kill a card; ability is optional");

        // Activate Stick Around and choose Experience to kill -> stays in Played Area with KILLED badge and keeps its 1 * Recruit (total 1 + 2 = 3 *) for the turn!
        t.activateCardAbility(stickUid, 2202u);
        check(t.state().pendingKillChoice, "Stick Around Modal: Opens Choose Card to Kill modal");
        t.resolveKillCardChoice(expUid);
        check(!t.state().pendingKillChoice && t.isCardKilledInPlayed(expUid) &&
                  t.state().recruitPool == 3,
              "Stick Around (KILLED Badge in Played): Chosen card stays in Played Area with KILLED badge and keeps its 1 * Recruit for the turn");

        // At Cleanup, the KILLED card moves to dead_characters instead of p0_discard!
        t.resolveStrikePhase(2203u);
        t.resolveCleanupPhase(2204u);
        bool expInDeadChars = false;
        if (const Pile* dc = t.findZonePile("dead_characters")) {
            for (const auto& c : dc->cards) {
                if (c.uid == expUid) expInDeadChars = true;
            }
        }
        check(expInDeadChars,
              "Stick Around Cleanup: KILLED card in Played Area moved to dead_characters at Cleanup Phase");
    }

    // 4D. Pushing Pencils (id 5): Sneak Peek Modal (Keep Hidden vs Free Scan Face-Up)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2300u, cfg1));
        clearWildsAndCombat(t);
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().phase = TurnPhase::ActionPhase;

        placeSingleInZone(t, 104, "wilds_2", false, false); // Hidden card in River
        const PileId pp1 = placeSingleInZone(t, 5, "p0_hand", true, false);
        const PileId pp2 = placeSingleInZone(t, 5, "p0_hand", true, false);
        const std::uint32_t pp1Uid = t.find(pp1)->top().uid;
        const std::uint32_t pp2Uid = t.find(pp2)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        // 1st Pushing Pencils: Sneak Peek River and choose Keep Hidden (false)
        t.activateCardAbility(pp1Uid, 2301u);
        t.selectSneakPeekWildsSlot(2);
        t.resolveSneakPeekDecision(false, 2302u);
        const Pile* w2Hidden = t.findZonePile("wilds_2");
        check(w2Hidden && !w2Hidden->top().faceUp,
              "Pushing Pencils (Sneak Peek Keep Hidden): Card inspected privately and kept face-down without revealing");

        // 2nd Pushing Pencils: Sneak Peek River and choose Open Face-Up & Scan (true)
        t.activateCardAbility(pp2Uid, 2303u);
        t.selectSneakPeekWildsSlot(2);
        t.resolveSneakPeekDecision(true, 2304u);
        const Pile* w2Revealed = t.findZonePile("wilds_2");
        check(w2Revealed && w2Revealed->top().faceUp,
              "Pushing Pencils (Sneak Peek Free Scan): Card opened face-up for free");
    }

    // 4E. Same Kind of Jungle (id 13): Switch Two Wilds Cards Modal (Wilds 0..4 Only)
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2400u, cfg1));
        clearWildsAndCombat(t);
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().phase = TurnPhase::ActionPhase;

        placeSingleInZone(t, 103, "wilds_0", true, false);  // Hills: 103 (face-up)
        placeSingleInZone(t, 104, "wilds_4", false, false); // Underground: 104 (face-down)
        const PileId mac13 = placeSingleInZone(t, 13, "p0_hand", true, false);
        placeSingleInZone(t, 9, "p0_hand", true, false); // 2nd Strength card (Knock Knock)
        const std::uint32_t mac13Uid = t.find(mac13)->top().uid;
        t.reflowZone("p0_hand");
        t.syncStateFromZones();

        t.activateCardAbility(mac13Uid, 2401u);
        t.resolveWildsSwapSlotClick(0);
        t.resolveWildsSwapSlotClick(4);

        const Pile* w0 = t.findZonePile("wilds_0");
        const Pile* w4 = t.findZonePile("wilds_4");
        check(w0 && w0->top().def == 104 && !w0->top().faceUp && w4 && w4->top().def == 103 &&
                  w4->top().faceUp,
              "Same Kind of Jungle: Switched face-up Enemy in Hills (wilds_0) with hidden card in Underground (wilds_4)");
    }

    // 4F. If It Bleeds, We Can Kill It (id 11): Stationed Beside Avatar Across 2 Turns
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2500u, cfg1));
        clearWildsAndCombat(t);
        t.mutableState().phase = TurnPhase::ActionPhase;

        const PileId dutch11Pid = placeSingleInZone(t, 11, "p0_hand", true, false);
        const std::uint32_t dutch11Uid = t.find(dutch11Pid)->top().uid;
        t.reflowZone("p0_hand");
        t.playCard(dutch11Pid, 2501u);

        check(t.state().players[0].cannotDieTurnsRemaining == 2 &&
                  t.state().players[0].activeImmunityCardUid == dutch11Uid,
              "If It Bleeds, We Can Kill It (Turn 1): Stationed beside Avatar with cannotDieTurnsRemaining == 2");

        // Deal fatal damage (15 dmg) on Turn 1 -> player survives and card stays on board through Turn 1 Cleanup!
        t.mutableState().players[0].manualStrikeOffset = 15;
        t.syncStateFromZones();
        t.resolveStrikePhase(2502u);
        t.resolveCleanupPhase(2503u);

        bool stillInPlayedAfterTurn1 = false;
        for (const auto& p : t.state().piles) {
            if (p.zone == "p0_played" && !p.cards.empty() && p.top().uid == dutch11Uid) {
                stillInPlayedAfterTurn1 = true;
            }
        }
        check(t.state().players[0].alive &&
                  t.state().players[0].cannotDieTurnsRemaining == 1 && stillInPlayedAfterTurn1,
              "If It Bleeds, We Can Kill It (Turn 1 Cleanup): Did NOT discard itself at Turn 1 Cleanup; player survived fatal damage");

        // Ensure p0_deck has extra cards so Turn 2 Cleanup doesn't immediately reshuffle p0_discard into p0_deck
        if (Pile* dk = t.findZonePile("p0_deck")) {
            for (int k = 0; k < 6; ++k) {
                CardInst extra;
                extra.uid = t.mutableState().nextUid++;
                extra.def = 225;
                extra.faceUp = false;
                dk->cards.push_back(extra);
            }
        }

        // Turn 2: Heal fatal damage before end of Turn 2 -> at Turn 2 Cleanup, card moves to Discard and player survives!
        t.resolveEnemyPhase(2504u);
        t.mutableState().players[0].manualStrikeOffset = 0;
        t.syncStateFromZones();
        t.resolveStrikePhase(2505u);
        t.resolveCleanupPhase(2506u);

        bool inDiscardAfterTurn2 = false;
        if (const Pile* disc = t.findZonePile("p0_discard")) {
            for (const auto& c : disc->cards) {
                if (c.uid == dutch11Uid) inDiscardAfterTurn2 = true;
            }
        }
        check(t.state().players[0].alive &&
                  t.state().players[0].cannotDieTurnsRemaining == 0 && inDiscardAfterTurn2,
              "If It Bleeds, We Can Kill It (Turn 2 Cleanup): Moved to Discard pile after end of next turn");
    }

    // 4G. The Choppa! (id 114): 3 * Recruit Cost in Combat Zone + Avatar Placed Beneath The Choppa!
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2600u, cfg1));
        clearWildsAndCombat(t);
        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().currentObjective = 3;
        t.mutableState().recruitPool = 3;

        check(catalog.card(114).cost == 3 && catalog.card(114).attack == -1,
              "The Choppa! (Catalog): Cost is 3 * Recruit and Attack is -1");

        const PileId choppaPid = placeSingleInZone(t, 114, "combat", true, false);
        t.fightEnemy(choppaPid, 2601u);

        const Pile* choppaAfter = t.find(choppaPid);
        check(t.state().phase == TurnPhase::Victory &&
                  t.state().players[0].escapedInChoppa && choppaAfter &&
                  choppaAfter->cards.size() == 2 && choppaAfter->cards.front().attached,
              "The Choppa! (Combat Zone): Spent 3 * Recruit, placed Avatar beneath The Choppa!, and completed Objective 3 (Minor Victory)");
    }

    // 4H. Three Hundred City Blocks (id 115): Attach to Final Enemy (121) & 4 * Recruit Blast Cost
    {
        Table t(catalog, lay1);
        t.reset(buildGameSetup(catalog, lay1, 2700u, cfg1));
        clearWildsAndCombat(t);
        auto& piles = t.mutableState().piles;
        piles.erase(std::remove_if(piles.begin(), piles.end(),
                                   [](const Pile& p) { return p.zone == "p0_hand"; }),
                    piles.end());
        t.mutableState().phase = TurnPhase::ActionPhase;
        t.mutableState().currentObjective = 3;

        check(catalog.card(115).cost == 4 && hasKw(catalog.card(115), "Attach"),
              "Three Hundred City Blocks (Catalog): Cost is 4 * and carries 'Attach' keyword");

        // Place Final Enemy (One Ugly Mother 121) face-up in Ruins (wilds_1) and scan Three Hundred City Blocks (115) in Hills (wilds_0)
        const PileId bossPid = placeSingleInZone(t, 121, "wilds_1", true, false);
        const PileId blkPid = placeSingleInZone(t, 115, "wilds_0", false, false);
        t.mutableState().attackPool = 20;
        t.mutableState().recruitPool = 4;
        t.scanWilds(blkPid, 2701u);

        const Pile* bossWith115 = t.find(bossPid);
        check(t.state().threeHundredCityBlocksAttachedToFinalEnemy && bossWith115 &&
                  bossWith115->cards.size() == 2 && bossWith115->cards.front().def == 115,
              "Three Hundred City Blocks Reveal: Attached directly beneath face-up Final Enemy (One Ugly Mother)");

        // Kill Final Enemy with 4 * Recruit available -> pays 4 * Recruit and wins Major Victory!
        t.fightEnemy(bossPid, 2702u);
        check(t.state().phase == TurnPhase::Victory && t.state().recruitPool == 0 &&
                  t.state().players[0].alive,
              "Three Hundred City Blocks Ongoing: Killing Final Enemy paid 4 * Recruit and achieved Major Victory");
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
