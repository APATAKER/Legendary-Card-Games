#include "app/App.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace lc {

namespace {

bool containsCaseInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

struct AvatarOption {
    int defId;
    const char* name;
    const char* roleName;
    int defaultMovie; // 1 or 2
};

constexpr AvatarOption kHumanAvatars[] = {
    {72, "Lieutenant (Movie 1)", "Field Command", 1},
    {82, "Tracker (Movie 1)", "Eagle-Eyed", 1},
    {70, "Guerrilla (Movie 1)", "Freedom Fighter", 1},
    {64, "CIA Agent (Movie 1)", "Calculated Risk", 1},
    {76, "Radioman (Movie 1)", "Urgent Transmission", 1},
    {66, "Detective (Movie 2)", "Hot Lead", 2},
    {80, "SWAT Officer (Movie 2)", "Breach and Clear", 2},
    {74, "OWLF Agent (Movie 2)", "Tracking the Predator", 2},
    {68, "Gangster (Movie 2)", "Shakedown", 2},
    {78, "Reporter (Movie 2)", "Eye in the Sky", 2},
};

constexpr AvatarOption kPredatorAvatars[] = {
    {90, "Jungle Hunter (Movie 1)", "Jungle Hunter", 1},
    {86, "Elder Hunter (Movie 1)", "Elder Hunter", 1},
    {92, "Primeval Hunter (Movie 1)", "Primeval Hunter", 1},
    {84, "City Hunter (Movie 2)", "City Hunter", 2},
    {88, "Gunslinger Hunter (Movie 2)", "Gunslinger Hunter", 2},
};

const char* shortAvatarTitle(const Catalog& catalog, int avatarDefId) {
    if (avatarDefId >= 0 && avatarDefId < static_cast<int>(catalog.cards.size())) {
        const CardDef& d = catalog.card(avatarDefId);
        if (!d.title.empty()) return d.title.c_str();
        return d.name.c_str();
    }
    return "Operative";
}

const char* wildsName(int slotIdx) {
    switch (slotIdx) {
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
        return "The Wilds";
    }
}

} // namespace

void App::renderOverlay() {
    renderWorldLabels();
    renderTopBar();
    renderTacticalHUD();
    renderMoveLogWindow();
    renderSetupWizardModal();
    renderSacrificeModal();
    renderAbilityModals();
    renderContextMenu();
    renderInspectModal();
    renderPileBrowserModal();
    renderCatalogModal();
    renderBugReportModal();
    renderStatusBar();
}

void App::renderWorldLabels() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float z = camera_.zoom();
    const TableState& st = table_->state();

    // Render bold Player Board Area header banners above each separate player tray (1..5 players)
    if (z > 0.11f) {
        for (const auto& tray : layout_.playerTrays) {
            const int pIdx = tray.playerIndex;
            const bool isActive = (pIdx == st.activePlayer);
            int avId = 72;
            int maxHp = 9;
            int defVal = 5;
            int strikes = 0;
            int marks = 0;
            bool alive = true;
            if (pIdx >= 0 && pIdx < static_cast<int>(st.players.size())) {
                const PlayerState& ps = st.players[static_cast<size_t>(pIdx)];
                avId = ps.avatarDefId;
                maxHp = ps.health;
                defVal = ps.defense;
                strikes = ps.strikeDamage;
                marks = ps.marksCount;
                alive = ps.alive;
            }
            const int curHp = std::max(0, maxHp - strikes);
            char header[160];
            if (!alive) {
                std::snprintf(header, sizeof header, "PLAYER %d: %s  |  KIA (ELIMINATED)", pIdx + 1,
                              shortAvatarTitle(catalog_, avId));
            } else if (isActive) {
                std::snprintf(header, sizeof header,
                              "PLAYER %d: %s  |  HP %d/%d  |  DEF %d  |  Marks %d   [ACTIVE TURN]",
                              pIdx + 1, shortAvatarTitle(catalog_, avId), curHp, maxHp, defVal,
                              marks);
            } else {
                std::snprintf(header, sizeof header,
                              "PLAYER %d: %s  |  HP %d/%d  |  DEF %d  |  Marks %d", pIdx + 1,
                              shortAvatarTitle(catalog_, avId), curHp, maxHp, defVal, marks);
            }

            const Vec2 sp = camera_.worldToScreen({tray.rect.x + 16.f, tray.rect.y - 38.f});
            const ImVec2 tsz = ImGui::CalcTextSize(header);
            const ImU32 bgCol = !alive ? IM_COL32(45, 12, 16, 235)
                                       : (isActive ? IM_COL32(14, 42, 26, 240)
                                                   : IM_COL32(10, 20, 15, 225));
            const ImU32 borderCol = !alive ? IM_COL32(239, 68, 68, 235)
                                           : (isActive ? IM_COL32(74, 222, 128, 250)
                                                       : IM_COL32(65, 115, 85, 195));
            const ImU32 textCol = !alive ? IM_COL32(252, 165, 165, 255)
                                         : (isActive ? IM_COL32(187, 247, 208, 255)
                                                     : IM_COL32(205, 230, 215, 240));
            const ImVec2 bMin(sp.x - 8.f, sp.y - 4.f);
            const ImVec2 bMax(sp.x + tsz.x + 10.f, sp.y + tsz.y + 4.f);
            dl->AddRectFilled(bMin, bMax, bgCol, 6.f);
            dl->AddRect(bMin, bMax, borderCol, 6.f, 0, isActive ? 2.0f : 1.3f);
            dl->AddText(ImVec2(sp.x, sp.y), textCol, header);
        }
    }

    // Always render clean titles on all Player Area zones, plus board zones when Show Zones is on
    if (z > 0.18f) {
        for (const auto& zone : layout_.zones) {
            const bool playerZone = isPlayerZoneId(zone.id);
            if (!playerZone && !showZones_) continue;

            const Vec2 sp = camera_.worldToScreen({zone.rect.x + 8.f, zone.rect.y - 22.f});
            const ImVec2 tsz = ImGui::CalcTextSize(zone.label.c_str());
            dl->AddRectFilled(ImVec2(sp.x - 4.f, sp.y - 2.f),
                              ImVec2(sp.x + tsz.x + 6.f, sp.y + tsz.y + 2.f),
                              IM_COL32(8, 15, 11, 215), 4.f);
            dl->AddText(ImVec2(sp.x, sp.y),
                        playerZone ? IM_COL32(140, 240, 175, 245) : IM_COL32(190, 245, 205, 235),
                        zone.label.c_str());
        }
    }

    if (z < 0.16f) return;

    for (const auto& p : st.piles) {
        if (p.cards.empty()) continue;
        const PileVisual& pv = table_->visual(p.id);
        const Vec2 sz = table_->pileSize(p);

        // Multi-selection badge (SEL)
        if (isSelected(p.id)) {
            const Vec2 selWorld = pv.pos + Vec2{sz.x * 0.5f - 28.f, sz.y * 0.5f - 20.f};
            const Vec2 sp = camera_.worldToScreen(selWorld);
            const ImVec2 tsz = ImGui::CalcTextSize("SEL");
            const ImVec2 bMin(sp.x - tsz.x * 0.5f - 5.f, sp.y - tsz.y * 0.5f - 2.f);
            const ImVec2 bMax(sp.x + tsz.x * 0.5f + 5.f, sp.y + tsz.y * 0.5f + 2.f);
            dl->AddRectFilled(bMin, bMax, IM_COL32(14, 116, 144, 245), 5.f);
            dl->AddRect(bMin, bMax, IM_COL32(56, 189, 248, 255), 5.f, 0, 1.5f);
            dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                        IM_COL32(240, 253, 255, 255), "SEL");
        }

        // Attached bottom-card banner when a Wilds location has a 2-card stack [AttachedBottom, TopCard]
        if (z > 0.20f && p.cards.size() >= 2 && p.cards.front().attached) {
            const CardInst& bot = p.cards.front();
            const CardDef& botDef = catalog_.card(bot.def);
            const std::string attLabel =
                bot.faceUp ? ("ATTACHED: " + (botDef.title.empty() ? botDef.name : botDef.title))
                           : std::string("ATTACHED");
            const Vec2 attWorld = pv.pos + Vec2{0.f, -sz.y * 0.5f - 30.f};
            const Vec2 sp = camera_.worldToScreen(attWorld);
            const ImVec2 tsz = ImGui::CalcTextSize(attLabel.c_str());
            const ImVec2 bMin(sp.x - tsz.x * 0.5f - 6.f, sp.y - tsz.y * 0.5f - 3.f);
            const ImVec2 bMax(sp.x + tsz.x * 0.5f + 6.f, sp.y + tsz.y * 0.5f + 3.f);
            dl->AddRectFilled(bMin, bMax, IM_COL32(38, 22, 5, 240), 5.f);
            dl->AddRect(bMin, bMax, IM_COL32(245, 158, 11, 250), 5.f, 0, 1.6f);
            dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                        IM_COL32(254, 243, 199, 255), attLabel.c_str());
        } else if (z > 0.22f && p.cards.size() == 1 && p.top().attached) {
            // Standalone attached card pill at bottom edge of the card
            const Vec2 attWorld = pv.pos + Vec2{0.f, sz.y * 0.5f - 18.f};
            const Vec2 sp = camera_.worldToScreen(attWorld);
            const ImVec2 tsz = ImGui::CalcTextSize("ATTACHED");
            const ImVec2 bMin(sp.x - tsz.x * 0.5f - 6.f, sp.y - tsz.y * 0.5f - 2.f);
            const ImVec2 bMax(sp.x + tsz.x * 0.5f + 6.f, sp.y + tsz.y * 0.5f + 2.f);
            dl->AddRectFilled(bMin, bMax, IM_COL32(38, 22, 5, 238), 5.f);
            dl->AddRect(bMin, bMax, IM_COL32(245, 158, 11, 245), 5.f, 0, 1.5f);
            dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                        IM_COL32(254, 243, 199, 255), "ATTACHED");
        }

        // Card count badge on top-right corner of multi-card piles
        if (p.cards.size() > 1) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%zu", p.cards.size());
            const Vec2 cornerWorld = pv.pos + Vec2{sz.x * 0.5f - 22.f, -sz.y * 0.5f + 22.f};
            const Vec2 sp = camera_.worldToScreen(cornerWorld);
            const ImVec2 tsz = ImGui::CalcTextSize(buf);
            const float padX = 6.f, padY = 3.f;
            const ImVec2 bMin(sp.x - tsz.x * 0.5f - padX, sp.y - tsz.y * 0.5f - padY);
            const ImVec2 bMax(sp.x + tsz.x * 0.5f + padX, sp.y + tsz.y * 0.5f + padY);
            dl->AddRectFilled(bMin, bMax, IM_COL32(12, 22, 18, 230), 6.f);
            dl->AddRect(bMin, bMax, IM_COL32(90, 210, 130, 210), 6.f, 0, 1.5f);
            dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                        IM_COL32(235, 255, 240, 255), buf);
        }

        // Caption below labeled piles
        if (!p.label.empty() && z > 0.20f) {
            const Vec2 labelWorld = pv.pos + Vec2{0.f, sz.y * 0.5f + 20.f};
            const Vec2 sp = camera_.worldToScreen(labelWorld);
            const ImVec2 tsz = ImGui::CalcTextSize(p.label.c_str());
            const ImVec2 bMin(sp.x - tsz.x * 0.5f - 6.f, sp.y - tsz.y * 0.5f - 2.f);
            const ImVec2 bMax(sp.x + tsz.x * 0.5f + 6.f, sp.y + tsz.y * 0.5f + 2.f);
            dl->AddRectFilled(bMin, bMax, IM_COL32(10, 18, 14, 200), 4.f);
            dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                        IM_COL32(210, 232, 218, 245), p.label.c_str());
        }

        // On-board tactical rule badge (Cost / Scan / Attack / Wipe Out) on top-left corner
        if (z > 0.22f && !p.zone.empty()) {
            const CardInst& top = p.top();
            const CardDef& def = catalog_.card(top.def);
            char badge[64] = {};
            ImU32 borderCol = IM_COL32(245, 190, 45, 220);

            if ((p.zone == "commanders" || p.zone.rfind("hq_", 0) == 0) && top.faceUp) {
                const int cost = (p.zone == "commanders") ? 3 : std::max(0, def.cost);
                std::snprintf(badge, sizeof badge, "Cost: %d *", cost);
                if (st.phase == TurnPhase::ActionPhase && st.recruitPool >= cost) {
                    borderCol = IM_COL32(74, 222, 128, 245);
                }
            } else if (p.zone.rfind("wilds_", 0) == 0) {
                if (!top.faceUp) {
                    const int scanCost = table_->scanCostForZone(p.zone);
                    std::snprintf(badge, sizeof badge, "Scan: %d ///", scanCost);
                    if (st.phase == TurnPhase::ActionPhase &&
                        (st.freeScansAvailable > 0 || st.attackPool >= scanCost)) {
                        borderCol = IM_COL32(74, 222, 128, 245);
                    }
                } else if (def.id == 102) {
                    std::snprintf(badge, sizeof badge, "Wipe Out: 1 ///");
                    if (st.phase == TurnPhase::ActionPhase && st.attackPool >= 1 &&
                        p.cards.size() == 1) {
                        borderCol = IM_COL32(74, 222, 128, 245);
                    }
                } else if (def.id == 107 && top.attached) {
                    std::snprintf(badge, sizeof badge, "Auto-Scan Hills");
                    borderCol = IM_COL32(245, 158, 11, 235);
                } else {
                    const int atk = table_->effectiveEnemyAttack(p);
                    if (atk >= 0) {
                        if (def.id == 118 && p.zone != "wilds_2") {
                            std::snprintf(badge, sizeof badge, "Fight: %d *", atk);
                            borderCol = (st.phase == TurnPhase::ActionPhase && st.recruitPool >= atk)
                                            ? IM_COL32(74, 222, 128, 245)
                                            : IM_COL32(245, 190, 45, 235);
                        } else {
                            std::snprintf(badge, sizeof badge, "ATK: %d ///", atk);
                            borderCol = (st.phase == TurnPhase::ActionPhase && st.attackPool >= atk)
                                            ? IM_COL32(74, 222, 128, 245)
                                            : IM_COL32(248, 113, 113, 235);
                        }
                    }
                }
            } else if (p.zone == "combat" && top.faceUp) {
                if (def.id == 114) {
                    std::snprintf(badge, sizeof badge, "Choppa: 3 *");
                    if (st.phase == TurnPhase::ActionPhase && st.recruitPool >= 3) {
                        borderCol = IM_COL32(74, 222, 128, 245);
                    }
                } else {
                    const int atk = table_->effectiveEnemyAttack(p);
                    if (atk >= 0) {
                        if (def.id == 118) {
                            std::snprintf(badge, sizeof badge, "Fight: %d *", atk);
                            borderCol = (st.phase == TurnPhase::ActionPhase && st.recruitPool >= atk)
                                            ? IM_COL32(74, 222, 128, 245)
                                            : IM_COL32(245, 190, 45, 235);
                        } else {
                            std::snprintf(badge, sizeof badge, "ATK: %d ///", atk);
                            borderCol = (st.phase == TurnPhase::ActionPhase && st.attackPool >= atk)
                                            ? IM_COL32(74, 222, 128, 245)
                                            : IM_COL32(248, 113, 113, 235);
                        }
                    }
                }
            } else if (table_->isCardKilledInPlayed(top.uid)) {
                std::snprintf(badge, sizeof badge, "KILLED (-> Dead)");
                borderCol = IM_COL32(239, 68, 68, 255);
            } else if (table_->canActivateCardAbility(top.uid)) {
                std::snprintf(badge, sizeof badge, "ABILITY READY");
                borderCol = IM_COL32(56, 189, 248, 255);
            } else if (table_->isCardStatsSwapped(top.uid)) {
                int effR = 0, effA = 0;
                table_->effectiveCardStats(top, &effR, &effA);
                std::snprintf(badge, sizeof badge, "SWAPPED %d*/%d///", effR, effA);
                borderCol = IM_COL32(192, 132, 252, 245);
            }

            if (badge[0] != '\0') {
                const Vec2 badgeWorld = pv.pos + Vec2{-sz.x * 0.5f + 46.f, -sz.y * 0.5f + 18.f};
                const Vec2 sp = camera_.worldToScreen(badgeWorld);
                const ImVec2 tsz = ImGui::CalcTextSize(badge);
                const ImVec2 bMin(sp.x - tsz.x * 0.5f - 6.f, sp.y - tsz.y * 0.5f - 3.f);
                const ImVec2 bMax(sp.x + tsz.x * 0.5f + 6.f, sp.y + tsz.y * 0.5f + 3.f);
                dl->AddRectFilled(bMin, bMax, IM_COL32(10, 18, 14, 235), 5.f);
                dl->AddRect(bMin, bMax, borderCol, 5.f, 0, 1.6f);
                dl->AddText(ImVec2(sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f),
                            IM_COL32(245, 250, 245, 255), badge);
            }
        }
    }
}

void App::renderTopBar() {
    ImGui::SetNextWindowPos(ImVec2(14.f, 12.f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##TopBar", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.60f, 1.f), "LEGENDARY ENCOUNTERS");
        ImGui::SameLine(0.f, 12.f);

        if (ImGui::Button("New Game Setup...")) {
            showSetupWizard_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Board [0]")) fitBoard(false);
        ImGui::SameLine();
        if (ImGui::Button("All Boards [1]")) fitPlayerArea();
        ImGui::SameLine();
        if (ImGui::Button("Players [2]")) fitBoxShelf();

        const TableState& st = table_->state();
        if (st.playerCount > 1) {
            ImGui::SameLine(0.f, 8.f);
            for (int p = 0; p < st.playerCount; ++p) {
                const bool isAct = (p == st.activePlayer);
                if (isAct) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.30f, 1.f));
                }
                char pBtn[16];
                std::snprintf(pBtn, sizeof pBtn, "P%d", p + 1);
                if (ImGui::Button(pBtn)) {
                    fitPlayer(p);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    table_->setActivePlayer(p);
                    fitPlayer(p);
                }
                if (ImGui::IsItemHovered()) {
                    const int avId = (p < static_cast<int>(st.players.size()))
                                         ? st.players[static_cast<size_t>(p)].avatarDefId
                                         : 72;
                    ImGui::SetTooltip("Left-Click: Focus Player %d (%s)  |  Right-Click: Set Active Turn",
                                      p + 1, shortAvatarTitle(catalog_, avId));
                }
                if (isAct) {
                    ImGui::PopStyleColor();
                }
                if (p + 1 < st.playerCount) ImGui::SameLine(0.f, 4.f);
            }
        }

        ImGui::SameLine(0.f, 12.f);
        ImGui::Checkbox("Snap", &snapToSlots_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hold Alt while dragging to drop anywhere without snapping.");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Drag Stack [M]", &moveWholeStackMode_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled (or holding Shift), dragging a multi-card pile moves the entire stack.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedPiles_.size() < 2);
        const std::string stackBtn =
            "Stack (" + std::to_string(selectedPiles_.size()) + ") [G]";
        if (ImGui::Button(stackBtn.c_str())) {
            stackSelectedPiles();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Ctrl + Left-Click multiple cards/piles, then click here or press [G] to merge them into one stack.");
        }

        ImGui::SameLine(0.f, 10.f);
        ImGui::Checkbox("Log [L]", &showMoveLog_);
        ImGui::SameLine();
        ImGui::Checkbox("Zones [F2]", &showZones_);

        ImGui::SameLine(0.f, 10.f);
        ImGui::BeginDisabled(!table_->canUndo());
        if (ImGui::Button("Undo")) table_->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!table_->canRedo());
        if (ImGui::Button("Redo")) table_->redo();
        ImGui::EndDisabled();

        ImGui::SameLine(0.f, 10.f);
        if (ImGui::Button("Catalog (331)")) {
            showCatalogModal_ = true;
        }

        ImGui::SameLine(0.f, 10.f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.22f, 0.14f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.30f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.36f, 0.22f, 1.f));
        const int openBugs = table_->openBugReportCount();
        const std::string bugBtnLbl =
            (openBugs > 0) ? ("Report Bug (" + std::to_string(openBugs) + ") [B]")
                           : std::string("Report Bug [B]");
        if (ImGui::Button(bugBtnLbl.c_str())) {
            const PileId focusPile =
                (draggedPile_ != kNoPile)
                    ? draggedPile_
                    : (hoveredPile_ != kNoPile ? hoveredPile_ : inspectPile_);
            openBugReportModal(focusPile, inspectCardDef_);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Record a self-contained bug report (with board snapshot & recent moves) or browse past reports [B].");
        }
    }
    ImGui::End();
}

void App::renderTacticalHUD() {
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x - 14.f, 12.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(365.f, 0.f), ImVec2(410.f, vp.y - 60.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Tactical Game State (Unlocked & Synced)###TacticalHUD", nullptr, flags)) {
        TableState& st = table_->mutableState();
        const int actP = std::clamp(st.activePlayer, 0, std::max(0, st.playerCount - 1));

        // Scenario Header
        const char* movieStr = (st.setupConfig.movie == 2) ? "Predator 2 (LA)" : "Predator 1 (Val Verde)";
        const char* modeStr = st.setupConfig.playAsPredators ? "Predators" : "Humans";
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.68f, 1.f), "%s — %s (%dP)", movieStr, modeStr,
                           st.playerCount);

        // Multiplayer Squad Roster & Turn Switcher
        if (st.playerCount > 1 && !st.players.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.35f, 1.f), "Squad Roster (Active: P%d — %s):",
                               actP + 1, shortAvatarTitle(catalog_, st.players[static_cast<size_t>(actP)].avatarDefId));
            for (int p = 0; p < st.playerCount && p < static_cast<int>(st.players.size()); ++p) {
                ImGui::PushID(p);
                const PlayerState& ps = st.players[static_cast<size_t>(p)];
                const int curHp = std::max(0, ps.health - ps.strikeDamage);
                const bool isAct = (p == actP);
                const ImVec4 rowCol = !ps.alive ? ImVec4(0.85f, 0.35f, 0.35f, 1.f)
                                                : (isAct ? ImVec4(0.45f, 0.96f, 0.62f, 1.f)
                                                         : ImVec4(0.82f, 0.90f, 0.85f, 1.f));
                ImGui::TextColored(rowCol, "%sP%d %-14s HP %d/%d DEF %d",
                                   isAct ? "> " : "  ", p + 1,
                                   shortAvatarTitle(catalog_, ps.avatarDefId), curHp, ps.health,
                                   ps.defense);
                ImGui::SameLine(272.f);
                if (ImGui::SmallButton("Cam")) {
                    fitPlayer(p);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(isAct || !ps.alive);
                if (ImGui::SmallButton("Turn")) {
                    table_->setActivePlayer(p);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::Separator();
        }

        // Turn & Objective Direct Controls
        ImGui::Text("Turn:");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##Turn")) st.turnNumber = std::max(1, st.turnNumber - 1);
        ImGui::SameLine();
        ImGui::Text("%d", st.turnNumber);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##Turn")) st.turnNumber += 1;

        ImGui::SameLine(0.f, 16.f);
        ImGui::Text("Objective:");
        ImGui::SameLine();
        if (ImGui::SmallButton("1##Obj")) st.currentObjective = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("2##Obj")) st.currentObjective = 2;
        ImGui::SameLine();
        if (ImGui::SmallButton("3##Obj")) st.currentObjective = 3;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.35f, 1.f), "[Obj %d]", st.currentObjective);

        // Unlocked Phase Selector Combo
        const char* phaseItems[] = {
            "1. Enemy Phase (Draw to Wilds)",
            "2. Action Phase (Play/Recruit/Scan/Fight)",
            "3. Strike Phase (Combat R->L)",
            "4. Cleanup Phase (Discard & Draw 6)",
            "Victory",
            "Defeat",
        };
        int phaseIdx = std::clamp(static_cast<int>(st.phase), 0, 5);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##PhaseCombo", &phaseIdx, phaseItems, 6)) {
            st.phase = static_cast<TurnPhase>(phaseIdx);
            table_->recordMove("HUD_EDIT", "Phase Changed", 0, "HUD", "HUD", 0, true,
                               phaseItems[phaseIdx]);
        }

        ImGui::Separator();

        // Unlocked Active Player Avatar Health, Strikes, and Marks (Two-Way Synced with Player Area!)
        const int hpLeft = std::max(0, st.avatarHealth - st.strikeDamage);
        const ImVec4 hpCol = (hpLeft <= 3) ? ImVec4(0.98f, 0.35f, 0.35f, 1.f)
                                           : ImVec4(0.45f, 0.95f, 0.60f, 1.f);
        ImGui::TextColored(hpCol, "P%d HP: %d / %d (DEF %d)", actP + 1, hpLeft, st.avatarHealth,
                           st.avatarDefense);
        ImGui::SameLine(190.f);
        ImGui::Text("Max HP:");
        ImGui::SameLine();
        if (ImGui::SmallButton("-##MaxHP")) {
            st.avatarHealth = std::max(1, st.avatarHealth - 1);
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].health = st.avatarHealth;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##MaxHP")) {
            st.avatarHealth += 1;
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].health = st.avatarHealth;
            }
        }

        const std::string strikeZid = table_->activePlayerZone("strikes");
        const std::string markZid = table_->activePlayerZone("marks");

        ImGui::TextColored(ImVec4(0.98f, 0.45f, 0.45f, 1.f), "Strikes (Damage): %d", st.strikeDamage);
        ImGui::SameLine(190.f);
        if (ImGui::SmallButton("-1 Strike")) {
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].manualStrikeOffset -= 1;
            }
            st.manualStrikeOffset -= 1;
            table_->syncStateFromZones();
            table_->recordMove("HUD_EDIT", "Manual Strike -1", 0, "HUD", strikeZid,
                               st.strikeDamage, true,
                               "Strikes=" + std::to_string(st.strikeDamage));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+1 Strike")) {
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].manualStrikeOffset += 1;
            }
            st.manualStrikeOffset += 1;
            table_->syncStateFromZones();
            table_->recordMove("HUD_EDIT", "Manual Strike +1", 0, "HUD", strikeZid,
                               st.strikeDamage, true,
                               "Strikes=" + std::to_string(st.strikeDamage));
        }

        ImGui::TextColored(ImVec4(0.98f, 0.58f, 0.25f, 1.f), "Marks: %d", st.marksCount);
        ImGui::SameLine(190.f);
        if (ImGui::SmallButton("-1 Mark")) {
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].manualMarksOffset -= 1;
            }
            st.manualMarksOffset -= 1;
            table_->syncStateFromZones();
            table_->recordMove("HUD_EDIT", "Manual Mark -1", 0, "HUD", markZid,
                               st.marksCount, false, "Marks=" + std::to_string(st.marksCount));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+1 Mark")) {
            if (actP < static_cast<int>(st.players.size())) {
                st.players[static_cast<size_t>(actP)].manualMarksOffset += 1;
            }
            st.manualMarksOffset += 1;
            table_->syncStateFromZones();
            table_->recordMove("HUD_EDIT", "Manual Mark +1", 0, "HUD", markZid,
                               st.marksCount, false, "Marks=" + std::to_string(st.marksCount));
        }

        // Unlocked Resource Pools (Recruit * & Attack ///)
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.22f, 1.f), "Recruit Pool (*): %d", st.recruitPool);
        ImGui::SameLine(190.f);
        if (ImGui::SmallButton("-1 *")) st.recruitPool = std::max(0, st.recruitPool - 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("+1 *")) st.recruitPool += 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("0##ResetRec")) st.recruitPool = 0;

        ImGui::TextColored(ImVec4(0.98f, 0.38f, 0.38f, 1.f), "Attack Pool (///): %d", st.attackPool);
        ImGui::SameLine(190.f);
        if (ImGui::SmallButton("-1 ///")) st.attackPool = std::max(0, st.attackPool - 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("+1 ///")) st.attackPool += 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("0##ResetAtk")) st.attackPool = 0;

        // Expected Strikes This Turn Indicator (shows Raw Strikes, Avoid Strike reduction, and Net Strikes)
        int rawStrikes = 0, avoidCount = 0;
        const int netStrikes = table_->expectedStrikesThisTurn(&rawStrikes, &avoidCount);
        const ImVec4 expStrikeCol = (netStrikes == 0)
                                        ? ImVec4(0.45f, 0.95f, 0.62f, 1.f)
                                        : ImVec4(0.98f, 0.58f, 0.25f, 1.f);
        if (avoidCount > 0) {
            ImGui::TextColored(expStrikeCol,
                               "Expected Strikes This Turn: %d (-%d Avoid) = %d Net", rawStrikes,
                               avoidCount, netStrikes);
        } else {
            ImGui::TextColored(expStrikeCol, "Expected Strikes This Turn: %d", netStrikes);
        }
        if (st.freeScansAvailable > 0) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.98f, 1.f), "Free Scans Ready: %d",
                               st.freeScansAvailable);
        }

        // Active Player Zone Card Counts Summary
        const std::string handZid = table_->activePlayerZone("hand");
        const std::string playedZid = table_->activePlayerZone("played");
        const std::string deckZid = table_->activePlayerZone("deck");
        const std::string discZid = table_->activePlayerZone("discard");

        // Optional Card Abilities Panel (Hand + Played Area + Coordinated virtual copies)
        if (st.phase == TurnPhase::ActionPhase) {
            struct ReadyAbil {
                std::uint32_t uid;
                std::string cardName;
                std::string label;
                bool inHand;
            };
            std::vector<ReadyAbil> readyAbils;
            for (const auto& p : st.piles) {
                if ((p.zone == handZid || p.zone == playedZid) && !p.cards.empty()) {
                    std::string lbl;
                    if (table_->canActivateCardAbility(p.top().uid, &lbl, nullptr)) {
                        const CardDef& cd = catalog_.card(p.top().def);
                        readyAbils.push_back({p.top().uid,
                                              cd.title.empty() ? cd.name : cd.title, lbl,
                                              p.zone == handZid});
                    }
                }
            }
            for (const auto& cc : st.coordinatedCards) {
                std::string lbl;
                if (table_->canActivateCardAbility(cc.uid, &lbl, nullptr)) {
                    const CardDef& cd = catalog_.card(cc.def);
                    readyAbils.push_back(
                        {cc.uid, (cd.title.empty() ? cd.name : cd.title) + " (Coord)", lbl, false});
                }
            }
            if (!readyAbils.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.98f, 1.f),
                                   "Optional Card Abilities Ready (%zu):", readyAbils.size());
                for (const auto& ra : readyAbils) {
                    ImGui::PushID(static_cast<int>(ra.uid));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.34f, 0.46f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.46f, 0.62f, 1.f));
                    const std::string btnText =
                        "Activate " + ra.cardName + ": " + ra.label;
                    if (ImGui::Button(btnText.c_str(), ImVec2(-1.f, 0.f))) {
                        table_->checkpoint();
                        showToast(table_->activateCardAbility(ra.uid, rng_()));
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
            }
        }

        int handCount = 0, playedCount = 0, deckCount = 0, discCount = 0;
        for (const auto& p : st.piles) {
            if (p.zone == handZid)
                handCount += static_cast<int>(p.cards.size());
            else if (p.zone == playedZid)
                playedCount += static_cast<int>(p.cards.size());
            else if (p.zone == deckZid)
                deckCount += static_cast<int>(p.cards.size());
            else if (p.zone == discZid)
                discCount += static_cast<int>(p.cards.size());
        }
        ImGui::Separator();
        ImGui::TextDisabled("P%d Hand: %d  |  Played: %d  |  Deck: %d  |  Discard: %d", actP + 1,
                            handCount, playedCount, deckCount, discCount);

        // Quick Active Player Deck / Hand Actions
        if (ImGui::Button("Draw 1 Card")) {
            table_->checkpoint();
            if (Pile* deck = table_->findZonePile(deckZid); deck && !deck->cards.empty()) {
                const PileId drawn = table_->splitTop(deck->id, 1);
                if (Pile* dp = table_->find(drawn)) {
                    dp->cards.back().faceUp = true;
                    dp->zone = handZid;
                }
                table_->reflowZone(handZid);
                std::string delta;
                table_->syncStateFromZones(&delta);
                table_->recordMove("DRAW_CARD", "Top of Player Deck", 1, deckZid, handZid,
                                   handCount + 1, true, delta);
                showToast("P" + std::to_string(actP + 1) + " drew 1 card to Hand Area");
            } else {
                showToast("Player Deck is empty — drag Discard onto Deck or click Shuffle Discard -> Deck");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Shuffle Discard -> Deck")) {
            if (Pile* disc = table_->findZonePile(discZid); disc && !disc->cards.empty()) {
                table_->checkpoint();
                disc->zone = deckZid;
                table_->syncStateFromZones();
                if (Pile* deck = table_->findZonePile(deckZid)) {
                    table_->shuffle(deck->id, rng_());
                    table_->recordMove("RECYCLE_DISCARD", "Player Discard",
                                       static_cast<int>(deck->cards.size()), discZid, deckZid,
                                       static_cast<int>(deck->cards.size()), false,
                                       "Recycled & shuffled Discard into Deck");
                }
                showToast("Recycled & shuffled P" + std::to_string(actP + 1) +
                          "'s Discard Pile into Deck!");
            }
        }

        ImGui::Separator();

        // Step-by-Step Phase Action Buttons
        if (st.phase == TurnPhase::EnemyPhase) {
            if (ImGui::Button("Step 1: Draw Enemy to Wilds [Enter]", ImVec2(-1.f, 0.f))) {
                showToast(table_->resolveEnemyPhase(rng_()));
            }
        } else if (st.phase == TurnPhase::ActionPhase) {
            if (ImGui::Button("Play All Hand Cards [A]", ImVec2(-1.f, 0.f))) {
                showToast(table_->playAllHandCards(rng_()));
            }
            if (st.currentObjective == 2) {
                ImGui::BeginDisabled(!table_->canCompleteObjective2());
                if (ImGui::Button("Complete Objective 2 (Spend 6 *)", ImVec2(-1.f, 0.f))) {
                    showToast(table_->completeObjective2());
                }
                ImGui::EndDisabled();
            }
            if (ImGui::Button("End Action Phase -> Strikes [Enter]", ImVec2(-1.f, 0.f))) {
                showToast(table_->stepPhase(rng_()));
            }
        } else if (st.phase == TurnPhase::StrikePhase) {
            if (ImGui::Button("Step 3: Resolve Combat Strikes [Enter]", ImVec2(-1.f, 0.f))) {
                showToast(table_->resolveStrikePhase(rng_()));
            }
        } else if (st.phase == TurnPhase::CleanupPhase) {
            const std::string cleanBtn =
                (st.playerCount > 1) ? "Step 4: Cleanup, Draw 6 & Next Player [Enter]"
                                     : "Step 4: Cleanup & Draw 6 [Enter]";
            if (ImGui::Button(cleanBtn.c_str(), ImVec2(-1.f, 0.f))) {
                showToast(table_->resolveCleanupPhase(rng_()));
            }
        } else {
            if (ImGui::Button("New Game Setup...", ImVec2(-1.f, 0.f))) {
                showSetupWizard_ = true;
            }
        }

        if (!st.lastActionLog.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", st.lastActionLog.c_str());
        }
    }
    ImGui::End();
}

void App::renderMoveLogWindow() {
    if (!showMoveLog_) return;

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(14.f, vp.y - 52.f), ImGuiCond_FirstUseEver, ImVec2(0.f, 1.f));
    ImGui::SetNextWindowSize(ImVec2(680.f, 230.f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Card Movement & Telemetry Log [L]###MoveLogWin", &showMoveLog_,
                     ImGuiWindowFlags_NoSavedSettings)) {
        const auto& logVec = table_->state().moveLog;
        ImGui::TextDisabled("Disk Log: %s (%zu events)",
                            table_->logFilePath().filename().string().c_str(), logVec.size());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Full path: %s", table_->logFilePath().string().c_str());
        }
        ImGui::SameLine(0.f, 16.f);
        ImGui::SetNextItemWidth(220.f);
        ImGui::InputTextWithHint("##MoveFilter", "Filter by zone, card, or action...",
                                 moveLogFilter_, sizeof moveLogFilter_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear View")) {
            table_->mutableState().moveLog.clear();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.22f, 0.14f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.30f, 0.18f, 1.f));
        if (ImGui::SmallButton("Report Bug [B]")) {
            openBugReportModal(hoveredPile_, inspectCardDef_);
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click here or click [Bug] on any row below to record a bug with that move & board state!");
        }

        ImGui::Separator();
        ImGui::BeginChild("MoveLogScroll");
        for (int i = static_cast<int>(logVec.size()) - 1; i >= 0; --i) {
            const MoveLogEntry& e = logVec[static_cast<size_t>(i)];
            if (!containsCaseInsensitive(e.cardSummary, moveLogFilter_) &&
                !containsCaseInsensitive(e.fromZone, moveLogFilter_) &&
                !containsCaseInsensitive(e.toZone, moveLogFilter_) &&
                !containsCaseInsensitive(e.actionType, moveLogFilter_)) {
                continue;
            }

            ImGui::PushID(static_cast<int>(e.seq));
            if (ImGui::SmallButton("Bug")) {
                char noteBuf[512];
                std::snprintf(noteBuf, sizeof noteBuf,
                              "Flagged Move #%03u [T%d %s] %s: %dx %s [%s -> %s] (%s)\nWhat went wrong: ",
                              e.seq, e.turn, e.phase.c_str(), e.actionType.c_str(), e.cardCount,
                              e.cardSummary.c_str(), e.fromZone.c_str(), e.toZone.c_str(),
                              e.stateDelta.c_str());
                openBugReportModal(kNoPile, -1,
                                   e.cardSummary + " (" + e.actionType + " #" +
                                       std::to_string(e.seq) + ")",
                                   e.fromZone + " -> " + e.toZone, noteBuf);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Report a bug starting from Move #%03u (%s)", e.seq,
                                  e.cardSummary.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.98f, 1.f), "#%03u [T%d %s]", e.seq, e.turn,
                               e.phase.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.25f, 1.f), "%-13s", e.actionType.c_str());
            ImGui::SameLine();
            ImGui::Text("%dx %s", e.cardCount, e.cardSummary.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f), "[%s -> %s]", e.fromZone.c_str(),
                               e.toZone.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(stack=%d, %s) %s", e.destStackCount,
                                e.faceUp ? "FaceUp" : "FaceDown", e.stateDelta.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void App::renderSetupWizardModal() {
    if (!showSetupWizard_) return;

    // Ensure 5 valid unique Avatar slots exist for editing 1..5 players
    if (setupConfig_.playerAvatars.size() < 5) {
        GameSetupConfig tmp = setupConfig_;
        tmp.playerCount = 5;
        setupConfig_.playerAvatars = resolveUniquePlayerAvatars(tmp);
        setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
    }
    wizardEditingPlayer_ = std::clamp(wizardEditingPlayer_, 0, std::max(0, playerCount_ - 1));

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(820.f, 620.f), ImGuiCond_Appearing);

    if (ImGui::Begin("NEW MISSION SETUP — LEGENDARY ENCOUNTERS: PREDATOR###SetupWizard",
                     &showSetupWizard_,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        // Step 1: Movie Selection
        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.22f, 1.f),
                           "STEP 1: Which movie scenario are we playing?");
        if (ImGui::RadioButton("Predator 1 (1987) — Val Verdean Jungle (Dutch, Dillon, Mac, Blain)",
                               setupConfig_.movie == 1)) {
            setupConfig_.movie = 1;
            setupConfig_.playerAvatars = defaultAvatarsForScenario(1, setupConfig_.playAsPredators);
            setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
        }
        if (ImGui::RadioButton("Predator 2 (1990) — Streets of Los Angeles (Harrigan, Keyes, Danny, Lambert)",
                               setupConfig_.movie == 2)) {
            setupConfig_.movie = 2;
            setupConfig_.playerAvatars = defaultAvatarsForScenario(2, setupConfig_.playAsPredators);
            setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Step 2: Faction / Mode Selection
        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.22f, 1.f),
                           "STEP 2: Are we playing as Humans or Predators?");
        if (ImGui::RadioButton("Play as Humans (Cooperative — Recruit Heroes, Scan Wilds, Complete 3 Objectives)",
                               !setupConfig_.playAsPredators)) {
            setupConfig_.playAsPredators = false;
            setupConfig_.playerAvatars = defaultAvatarsForScenario(setupConfig_.movie, false);
            setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
        }
        if (ImGui::RadioButton("Play as Predators (Hunter Mode — Acquire Gear/Traps/Tests, Stalk & Hunt Prey)",
                               setupConfig_.playAsPredators)) {
            setupConfig_.playAsPredators = true;
            setupConfig_.playerAvatars = defaultAvatarsForScenario(setupConfig_.movie, true);
            setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Step 3: Player Count & Per-Player Unique Character Selection
        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.22f, 1.f),
                           "STEP 3: Number of Players & Unique Character Roster");
        ImGui::Text("Number of Players:");
        ImGui::SameLine();
        for (int p = 1; p <= 5; ++p) {
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "%d Player%s", p, p == 1 ? "" : "s");
            if (ImGui::RadioButton(lbl, playerCount_ == p)) {
                playerCount_ = p;
                setupConfig_.playerCount = p;
                GameSetupConfig tmp = setupConfig_;
                tmp.playerCount = 5;
                setupConfig_.playerAvatars = resolveUniquePlayerAvatars(tmp);
                setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
                wizardEditingPlayer_ = std::clamp(wizardEditingPlayer_, 0, playerCount_ - 1);
            }
            if (p < 5) ImGui::SameLine();
        }

        // Per-player character slot buttons (Player 1 .. Player N)
        ImGui::Spacing();
        for (int i = 0; i < playerCount_; ++i) {
            const bool editingThis = (i == wizardEditingPlayer_);
            if (editingThis) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.30f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.56f, 0.35f, 1.f));
            }
            const int pAv = setupConfig_.playerAvatars[static_cast<size_t>(i)];
            char tabLbl[64];
            std::snprintf(tabLbl, sizeof tabLbl, "P%d: %s", i + 1,
                          shortAvatarTitle(catalog_, pAv));
            if (ImGui::Button(tabLbl)) {
                wizardEditingPlayer_ = i;
            }
            if (editingThis) {
                ImGui::PopStyleColor(2);
            }
            if (i + 1 < playerCount_) ImGui::SameLine();
        }

        ImGui::Spacing();
        ImGui::BeginChild("AvatarPickerPane", ImVec2(415.f, 232.f), true);
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.68f, 1.f),
                           "Choose Unique Character for Player %d:", wizardEditingPlayer_ + 1);

        auto renderAvatarOptionRow = [&](const AvatarOption& opt) {
            int ownerIdx = -1;
            for (int p = 0; p < playerCount_; ++p) {
                if (setupConfig_.playerAvatars[static_cast<size_t>(p)] == opt.defId) {
                    ownerIdx = p;
                    break;
                }
            }
            int hp = 9, def = 5;
            avatarStatsForDef(opt.defId, hp, def);

            char rowLbl[128];
            if (ownerIdx >= 0) {
                std::snprintf(rowLbl, sizeof rowLbl, "[P%d] %s  (HP %d, DEF %d)", ownerIdx + 1,
                              opt.name, hp, def);
            } else {
                std::snprintf(rowLbl, sizeof rowLbl, "     %s  (HP %d, DEF %d)", opt.name, hp, def);
            }

            const bool isMovieMatch = (opt.defaultMovie == setupConfig_.movie);
            const bool selectedForEditing = (ownerIdx == wizardEditingPlayer_);
            if (selectedForEditing) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.98f, 0.68f, 1.f));
            } else if (ownerIdx >= 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.84f, 0.42f, 1.f));
            } else if (isMovieMatch) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.98f, 0.95f, 1.f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.75f, 0.68f, 1.f));
            }

            if (ImGui::RadioButton(rowLbl, selectedForEditing)) {
                if (ownerIdx >= 0 && ownerIdx != wizardEditingPlayer_) {
                    // Swap characters so every player retains a 100% unique Avatar + Role card!
                    std::swap(setupConfig_.playerAvatars[static_cast<size_t>(ownerIdx)],
                              setupConfig_.playerAvatars[static_cast<size_t>(wizardEditingPlayer_)]);
                } else {
                    setupConfig_.playerAvatars[static_cast<size_t>(wizardEditingPlayer_)] =
                        opt.defId;
                }
                setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
            }
            ImGui::PopStyleColor();
        };

        if (!setupConfig_.playAsPredators) {
            for (const auto& opt : kHumanAvatars) {
                renderAvatarOptionRow(opt);
            }
        } else {
            for (const auto& opt : kPredatorAvatars) {
                renderAvatarOptionRow(opt);
            }
        }
        ImGui::EndChild();

        // Live Scanned Card Preview of Selected Player's Avatar + Unique Role Card
        ImGui::SameLine();
        ImGui::BeginChild("AvatarPreviewPane", ImVec2(0.f, 232.f), true);
        const int avDefId = setupConfig_.playerAvatars[static_cast<size_t>(wizardEditingPlayer_)];
        const int roleDefId = avDefId + 1;
        if (avDefId >= 0 && avDefId < static_cast<int>(catalog_.cards.size())) {
            const CardDef& avDef = catalog_.card(avDefId);
            const CardDef& roleDef = catalog_.card(roleDefId);
            const Texture avTex = textures_->request(avDef.texture);
            const Texture roleTex = textures_->request(roleDef.texture);

            ImGui::Image(static_cast<ImTextureID>(avTex ? avTex.id : backTex_.id),
                         ImVec2(130.f, 184.f));
            if (ImGui::IsItemClicked()) inspectCardDef_ = avDefId;
            ImGui::SameLine();
            ImGui::Image(static_cast<ImTextureID>(roleTex ? roleTex.id : backTex_.id),
                         ImVec2(130.f, 184.f));
            if (ImGui::IsItemClicked()) inspectCardDef_ = roleDefId;

            ImGui::TextDisabled("P%d: %s  |  Role: %s (Click to Inspect)", wizardEditingPlayer_ + 1,
                                avDef.title.empty() ? avDef.name.c_str() : avDef.title.c_str(),
                                roleDef.title.empty() ? roleDef.name.c_str()
                                                      : roleDef.title.c_str());
        }
        ImGui::EndChild();

        ImGui::Spacing();
        const std::string launchLabel =
            "LAUNCH " + std::to_string(playerCount_) + "-PLAYER MISSION (Initialize " +
            std::to_string(playerCount_) + " Separate Player Board" +
            (playerCount_ == 1 ? "" : "s") + ")";
        if (ImGui::Button(launchLabel.c_str(), ImVec2(-1.f, 38.f))) {
            resetSetup();
            fitPlayerArea();
            showSetupWizard_ = false;
        }
    }
    ImGui::End();
}

void App::renderSacrificeModal() {
    const TableState& st = table_->state();
    if (!st.pendingSacrificeChoice) return;

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("STRIKE: SACRIFICE (5 DAMAGE) — CHOOSE TARGET PLAYER###SacrificeModal",
                     nullptr, flags)) {
        const CardDef& sacDef = catalog_.card(st.pendingSacrificeCard.def);
        const Texture sacTex = textures_->request(sacDef.texture);

        ImGui::Columns(2, "SacrificeModalCols", false);
        ImGui::SetColumnWidth(0, 220.f);

        ImGui::Image(static_cast<ImTextureID>(sacTex ? sacTex.id : backTex_.id),
                     ImVec2(195.f, 275.f));

        ImGui::NextColumn();

        const int srcP = st.pendingSacrificeSourcePlayer;
        const int srcAv = (srcP >= 0 && srcP < static_cast<int>(st.players.size()))
                              ? st.players[static_cast<size_t>(srcP)].avatarDefId
                              : 72;

        ImGui::TextColored(ImVec4(0.98f, 0.42f, 0.42f, 1.f),
                           "Player %d (%s) received SACRIFICE (5 Damage)!", srcP + 1,
                           shortAvatarTitle(catalog_, srcAv));
        ImGui::Spacing();
        ImGui::TextWrapped("Card Text: \"5 Damage. Another player may take this Strike instead of you.\"");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.f),
                           "Choose which player will receive this 5-Damage Strike:");
        ImGui::TextDisabled("(If the chosen player's HP drops to 0 or below, that player will die!)");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        for (int pIdx = 0; pIdx < st.playerCount; ++pIdx) {
            if (pIdx >= static_cast<int>(st.players.size())) break;
            const PlayerState& ps = st.players[static_cast<size_t>(pIdx)];
            if (!ps.alive) continue;

            const int curHp = ps.health - ps.strikeDamage;
            const int afterHp = ps.health - (ps.strikeDamage + 5);
            const bool willDie = (afterHp <= 0 && ps.cannotDieTurnsRemaining <= 0);
            const bool isOriginal = (pIdx == srcP);

            if (willDie) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.14f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.16f, 0.18f, 1.f));
            } else if (isOriginal) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.34f, 0.22f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.44f, 0.28f, 1.f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.38f, 0.26f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.50f, 0.34f, 1.f));
            }

            char btnLbl[200];
            std::snprintf(btnLbl, sizeof btnLbl,
                          "%s Player %d (%s)  |  HP: %d/%d -> %d/%d%s###SacBtn%d",
                          isOriginal ? "[Keep on Target]" : "[Redirect to Teammate]",
                          pIdx + 1, shortAvatarTitle(catalog_, ps.avatarDefId),
                          curHp, ps.health, afterHp, ps.health,
                          willDie ? "   [FATAL — WILL DIE!]" : "", pIdx);

            if (ImGui::Button(btnLbl, ImVec2(-1.f, 36.f))) {
                table_->checkpoint();
                showToast(table_->resolveSacrificeChoice(pIdx));
            }

            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        ImGui::Columns(1);
    }
    ImGui::End();
}

void App::renderAbilityModals() {
    TableState& st = table_->mutableState();
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoCollapse;

    // 1. COVER ME! (def 191, 4 Damage): Teammates choose Attack cards totaling >= 4 /// to discard and cancel
    if (st.pendingCoverMeChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(820.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("STRIKE: COVER ME! (4 DAMAGE) — TEAMMATE DISCARD PROMPT###CoverMeModal",
                         nullptr, flags)) {
            const CardDef& cmDef = catalog_.card(st.pendingCoverMeCard.def);
            const Texture cmTex = textures_->request(cmDef.texture);
            const int targetP = st.pendingCoverMeTargetPlayer;
            const int targetAv = (targetP >= 0 && targetP < static_cast<int>(st.players.size()))
                                     ? st.players[static_cast<size_t>(targetP)].avatarDefId
                                     : 72;

            ImGui::Columns(2, "CoverMeCols", false);
            ImGui::SetColumnWidth(0, 210.f);
            ImGui::Image(static_cast<ImTextureID>(cmTex ? cmTex.id : backTex_.id),
                         ImVec2(185.f, 262.f));
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.98f, 0.42f, 0.42f, 1.f),
                               "Player %d (%s) drew COVER ME! (4 Damage)!", targetP + 1,
                               shortAvatarTitle(catalog_, targetAv));
            ImGui::TextWrapped("Card Text: \"4 Damage. The other players may discard cards with a "
                               "printed total of at least 4 [Attack] to cancel this Strike.\"");
            ImGui::Spacing();

            const int selAtk = table_->selectedCoverMeAttackTotal();
            const ImVec4 atkCol = (selAtk >= 4) ? ImVec4(0.45f, 0.95f, 0.62f, 1.f)
                                                : ImVec4(0.98f, 0.78f, 0.25f, 1.f);
            ImGui::TextColored(atkCol, "Selected Teammate Attack to Discard: %d / 4 ///", selAtk);
            ImGui::Separator();

            ImGui::BeginChild("CoverMeCardsScroll", ImVec2(0.f, 195.f), true);
            for (int p = 0; p < st.playerCount; ++p) {
                if (p == targetP || p >= static_cast<int>(st.players.size())) continue;
                const PlayerState& ps = st.players[static_cast<size_t>(p)];
                if (!ps.alive || ps.escapedInChoppa) continue;

                ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.68f, 1.f), "Player %d (%s) Hand Cards:",
                                   p + 1, shortAvatarTitle(catalog_, ps.avatarDefId));
                const std::string hZid = table_->playerZone(p, "hand");
                bool anyAtkInHand = false;
                for (const auto& hp : st.piles) {
                    if (hp.zone != hZid || hp.cards.empty()) continue;
                    const CardInst& c = hp.top();
                    const CardDef& cd = catalog_.card(c.def);
                    if (cd.attack <= 0) continue;
                    anyAtkInHand = true;
                    const bool isSel =
                        std::find(st.coverMeSelectedUids.begin(), st.coverMeSelectedUids.end(),
                                  c.uid) != st.coverMeSelectedUids.end();
                    ImGui::PushID(static_cast<int>(c.uid));
                    if (isSel) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.32f, 1.f));
                    }
                    char cBtn[160];
                    std::snprintf(cBtn, sizeof cBtn, "%s %s  (+%d /// Attack)",
                                  isSel ? "[SELECTED]" : "[Select]",
                                  (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                                  cd.attack);
                    if (ImGui::Button(cBtn, ImVec2(-1.f, 0.f))) {
                        table_->toggleCoverMeCard(c.uid);
                    }
                    if (isSel) {
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
                if (!anyAtkInHand) {
                    ImGui::TextDisabled("  (No Attack cards in hand)");
                }
                ImGui::Spacing();
            }
            ImGui::EndChild();

            ImGui::BeginDisabled(selAtk < 4);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.30f, 1.f));
            const std::string coverBtn = "Discard Selected Cards (" + std::to_string(selAtk) +
                                         " ///) & Cancel COVER ME!";
            if (ImGui::Button(coverBtn.c_str(), ImVec2(-1.f, 34.f))) {
                table_->checkpoint();
                showToast(table_->resolveCoverMeChoice(true, rng_()));
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.16f, 0.18f, 1.f));
            char takeBtn[128];
            std::snprintf(takeBtn, sizeof takeBtn,
                          "Decline Cover — Player %d Takes 4 Damage", targetP + 1);
            if (ImGui::Button(takeBtn, ImVec2(-1.f, 32.f))) {
                table_->checkpoint();
                showToast(table_->resolveCoverMeChoice(false, rng_()));
            }
            ImGui::PopStyleColor();

            ImGui::Columns(1);
        }
        ImGui::End();
    }

    // 2. SNEAK PEEK MODAL (Pushing Pencils id 5): Look at a hidden card in The Wilds without revealing it; optional free scan
    if (st.pendingSneakPeekChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("PUSHING PENCILS [TECH]: SNEAK PEEK HIDDEN WILDS CARD###SneakPeekModal",
                         nullptr, flags)) {
            if (!st.sneakPeekRevealedCardSelected) {
                ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.98f, 1.f),
                                   "Step 1: Choose a hidden (face-down) card in The Wilds to privately inspect:");
                ImGui::TextDisabled("(Looking at the card will NOT trigger its Reveal effect unless you choose to open it face-up.)");
                ImGui::Spacing();
                for (int s = 0; s <= 4; ++s) {
                    const Pile* wp = table_->findZonePile("wilds_" + std::to_string(s));
                    const bool isHidden = (wp && !wp->cards.empty() && !wp->top().faceUp);
                    ImGui::BeginDisabled(!isHidden);
                    char sBtn[128];
                    std::snprintf(sBtn, sizeof sBtn, "Slot %d: %s  (%s)", s + 1, wildsName(s),
                                  isHidden ? "Face-Down — Click to Sneak Peek" : "Not Face-Down");
                    if (ImGui::Button(sBtn, ImVec2(-1.f, 34.f))) {
                        table_->checkpoint();
                        showToast(table_->selectSneakPeekWildsSlot(s));
                    }
                    ImGui::EndDisabled();
                }
                ImGui::Spacing();
                if (ImGui::Button("Cancel Sneak Peek", ImVec2(-1.f, 0.f))) {
                    showToast(table_->cancelOptionalModal());
                }
            } else {
                const int slotIdx = st.sneakPeekWildsSlot;
                const Pile* wp = table_->findZonePile("wilds_" + std::to_string(slotIdx));
                if (wp && !wp->cards.empty()) {
                    const CardDef& peekDef = catalog_.card(wp->top().def);
                    const Texture peekTex = textures_->request(peekDef.texture);

                    ImGui::Columns(2, "SneakPeekCols", false);
                    ImGui::SetColumnWidth(0, 230.f);
                    ImGui::Image(static_cast<ImTextureID>(peekTex ? peekTex.id : backTex_.id),
                                 ImVec2(205.f, 290.f));
                    ImGui::NextColumn();

                    ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.25f, 1.f),
                                       "PRIVATE SNEAK PEEK — %s:", wildsName(slotIdx));
                    ImGui::TextColored(ImVec4(0.50f, 0.96f, 0.68f, 1.f), "%s",
                                       peekDef.title.empty() ? peekDef.name.c_str()
                                                             : peekDef.title.c_str());
                    if (!peekDef.subtitle.empty()) {
                        ImGui::TextDisabled("%s", peekDef.subtitle.c_str());
                    }
                    if (peekDef.attack >= 0) {
                        ImGui::TextColored(ImVec4(0.98f, 0.42f, 0.42f, 1.f), "Attack: %d ///",
                                           peekDef.attack);
                    }
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", peekDef.rulesText.c_str());
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.48f, 0.30f, 1.f));
                    if (ImGui::Button("Open Face-Up & Scan For Free (Triggers Reveal Effect)",
                                      ImVec2(-1.f, 38.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveSneakPeekDecision(true, rng_()));
                    }
                    ImGui::PopStyleColor();

                    ImGui::Spacing();
                    if (ImGui::Button("Keep Hidden (Leave Face-Down Without Revealing)",
                                      ImVec2(-1.f, 36.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveSneakPeekDecision(false, rng_()));
                    }
                    ImGui::Columns(1);
                }
            }
        }
        ImGui::End();
    }

    // 3. CHOOSE CARD TO KILL MODAL (Stick Around id 8, Running the Streets id 69, Nothing On Earth id 117, A Fitting Demise id 210)
    if (st.pendingKillChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("CHOOSE A CARD TO KILL###KillCardModal", nullptr, flags)) {
            if (st.pendingKillStayUntilCleanup) {
                ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.25f, 1.f),
                                   "Choose a card from your Hand or Played Area to kill:");
                ImGui::TextDisabled("The killed card stays in your Played Area with a [KILLED] badge until Cleanup, "
                                    "providing its Recruit, Attack, Class, and Team for the rest of this turn!");
            } else {
                ImGui::TextColored(ImVec4(0.98f, 0.42f, 0.42f, 1.f),
                                   "Mandatory Enemy Death Effect: Choose %d card(s) with Cost >= %d to kill:",
                                   st.pendingKillCount, st.pendingKillMinCost);
            }
            ImGui::Separator();

            const std::string handZid = table_->activePlayerZone("hand");
            const std::string playedZid = table_->activePlayerZone("played");

            ImGui::BeginChild("KillCandList", ImVec2(0.f, 260.f), true);
            for (const auto& p : st.piles) {
                if ((p.zone != handZid && p.zone != playedZid) || p.cards.empty()) continue;
                const CardInst& c = p.top();
                if (table_->isCardKilledInPlayed(c.uid)) continue;
                if (!st.pendingKillMandatory && c.uid == st.pendingAbilitySourceUid) continue;
                const CardDef& cd = catalog_.card(c.def);
                if (cd.cost < st.pendingKillMinCost) continue;

                ImGui::PushID(static_cast<int>(c.uid));
                int effR = 0, effA = 0;
                table_->effectiveCardStats(c, &effR, &effA);
                char kBtn[200];
                std::snprintf(kBtn, sizeof kBtn,
                              "Kill [%s] %s  (Cost %d | %d * / %d /// | %s)",
                              (p.zone == handZid ? "Hand" : "Played"),
                              (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                              std::max(0, cd.cost), effR, effA,
                              cd.classType.empty() ? "Starter" : cd.classType.c_str());
                if (ImGui::Button(kBtn, ImVec2(-1.f, 32.f))) {
                    table_->checkpoint();
                    showToast(table_->resolveKillCardChoice(c.uid));
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (!st.pendingKillMandatory) {
                if (ImGui::Button("Cancel (Do Not Kill a Card)", ImVec2(-1.f, 0.f))) {
                    showToast(table_->cancelOptionalModal());
                }
            }
        }
        ImGui::End();
    }

    // 4. HEAL STRIKE MODAL (Sexual Tyrannosaurus id 0, I See You id 14, Sources and Contacts id 79, Crafty Veteran id 87)
    if (st.pendingHealChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("CHOOSE A STRIKE TO HEAL###HealStrikeModal", nullptr, flags)) {
            if (st.pendingHealLeastOnly) {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                                   "Sexual Tyrannosaurus [Survival]: Choose a lowest-damage Strike to heal:");
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                                   "Choose a Strike to heal (Open Wound must be healed before other Strikes on that player):");
            }
            ImGui::Separator();

            int minGlobalDmg = 999;
            if (st.pendingHealLeastOnly) {
                for (int q = 0; q < st.playerCount; ++q) {
                    const Pile* qw = table_->findZonePile(table_->playerZone(q, "strikes"));
                    if (!qw || qw->cards.empty()) continue;
                    bool qOpen = false;
                    for (const auto& qc : qw->cards) {
                        if (qc.def == 194) qOpen = true;
                    }
                    for (const auto& qc : qw->cards) {
                        if (qOpen && qc.def != 194) continue;
                        minGlobalDmg =
                            std::min(minGlobalDmg, std::max(0, catalog_.card(qc.def).attack));
                    }
                }
            }

            ImGui::BeginChild("HealStrikeList", ImVec2(0.f, 240.f), true);
            for (int q = 0; q < st.playerCount; ++q) {
                if (q >= static_cast<int>(st.players.size()) ||
                    !st.players[static_cast<size_t>(q)].alive) {
                    continue;
                }
                const Pile* qw = table_->findZonePile(table_->playerZone(q, "strikes"));
                if (!qw || qw->cards.empty()) continue;

                bool hasOpenWound = false;
                for (const auto& qc : qw->cards) {
                    if (qc.def == 194) hasOpenWound = true;
                }

                ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.f), "Player %d (%s) Strikes:",
                                   q + 1,
                                   shortAvatarTitle(catalog_,
                                                    st.players[static_cast<size_t>(q)].avatarDefId));
                for (const auto& sc : qw->cards) {
                    const CardDef& sd = catalog_.card(sc.def);
                    const int dmg = std::max(0, sd.attack);
                    const bool blockedByOpenWound = (hasOpenWound && sc.def != 194);
                    const bool blockedByLeastRule =
                        (st.pendingHealLeastOnly && dmg > minGlobalDmg);
                    ImGui::PushID(static_cast<int>(sc.uid));
                    ImGui::BeginDisabled(blockedByOpenWound || blockedByLeastRule);
                    char hBtn[200];
                    std::snprintf(hBtn, sizeof hBtn, "Heal P%d: %s (%d DMG)%s%s", q + 1,
                                  sd.name.c_str(), dmg,
                                  (sc.def == 190 ? " [Permanent Damage: Draws 1 Strike]" : ""),
                                  (blockedByOpenWound ? " [Must heal Open Wound first]" : ""));
                    if (ImGui::Button(hBtn, ImVec2(-1.f, 32.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveHealStrikeChoice(q, sc.uid, rng_()));
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::Spacing();
            }
            ImGui::EndChild();

            if (ImGui::Button("Cancel Heal", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    // 5. PLAYER PICKER & DISCARD 2 MODAL (Ol' Painless Is Waiting id 1, I Can Get Even id 7)
    if (st.pendingPlayerChoiceMode != 0) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("PLAYER ABILITY PROMPT###PlayerPickerModal", nullptr, flags)) {
            if (st.pendingPlayerChoiceMode == 1 || st.pendingPlayerChoiceMode == 3 ||
                st.pendingPlayerChoiceMode == 4) {
                ImGui::TextColored(
                    ImVec4(0.45f, 0.95f, 0.62f, 1.f), "%s",
                    st.pendingPlayerChoiceMode == 1
                        ? "Ol' Painless Is Waiting [Leadership]: Choose a player to draw 2 cards, then discard 2 cards:"
                        : (st.pendingPlayerChoiceMode == 3
                               ? "I Can Get Even: Choose a player whose hand cards all gain Coordinate this turn:"
                               : "Cowboy S#!%: Choose another player to receive the next character you gain this turn:"));
                ImGui::Separator();
                for (int q = 0; q < st.playerCount; ++q) {
                    if (q >= static_cast<int>(st.players.size())) break;
                    if (st.pendingPlayerChoiceMode == 4 && q == st.activePlayer) continue;
                    const PlayerState& ps = st.players[static_cast<size_t>(q)];
                    if (!ps.alive || ps.escapedInChoppa) continue;
                    char pBtn[128];
                    std::snprintf(pBtn, sizeof pBtn, "Choose Player %d (%s)", q + 1,
                                  shortAvatarTitle(catalog_, ps.avatarDefId));
                    if (ImGui::Button(pBtn, ImVec2(-1.f, 34.f))) {
                        table_->checkpoint();
                        showToast(table_->resolvePlayerPickerChoice(q, rng_()));
                    }
                }
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                    showToast(table_->cancelOptionalModal());
                }
            } else if (st.pendingPlayerChoiceMode == 2) {
                const int tp = st.pendingTargetPlayer;
                ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.25f, 1.f),
                                   "Ol' Painless Is Waiting: Choose %d more card(s) from Player %d's Hand to discard:",
                                   st.pendingDiscardRemaining, tp + 1);
                ImGui::Separator();
                const std::string hZid = table_->playerZone(tp, "hand");
                ImGui::BeginChild("Discard2HandList", ImVec2(0.f, 230.f), true);
                for (const auto& p : st.piles) {
                    if (p.zone != hZid || p.cards.empty()) continue;
                    const CardInst& c = p.top();
                    const CardDef& cd = catalog_.card(c.def);
                    ImGui::PushID(static_cast<int>(c.uid));
                    char dBtn[180];
                    std::snprintf(dBtn, sizeof dBtn, "Discard %s  (%d * / %d ///)",
                                  (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                                  std::max(0, cd.recruit), std::max(0, cd.attack));
                    if (ImGui::Button(dBtn, ImVec2(-1.f, 32.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveHandDiscardChoice(c.uid));
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    // 6. SWITCH TWO WILDS CARDS MODAL (Same Kind of Jungle id 13 — Wilds 0..4 only)
    if (st.pendingWildsSwapChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("SAME KIND OF JUNGLE [STRENGTH]: SWITCH TWO WILDS CARDS###WildsSwapModal",
                         nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Select two spaces in The Wilds to switch their cards (Wilds only; Combat Zone & attached Goals cannot be switched):");
            ImGui::Separator();
            for (int s = 0; s <= 4; ++s) {
                const bool swappable = table_->isWildsSlotSwappable(s);
                const Pile* wp = table_->findZonePile("wilds_" + std::to_string(s));
                std::string desc = "Empty";
                if (wp && !wp->cards.empty()) {
                    const CardInst& top = wp->top();
                    desc = top.faceUp ? catalog_.card(top.def).name : "Hidden (Face-Down Card)";
                    if (top.attached || top.def == 102) desc += " [Attached Goal — Cannot Switch]";
                }
                const bool isFirst = (st.wildsSwapFirstSlot == s);
                ImGui::BeginDisabled(!swappable);
                if (isFirst) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.30f, 1.f));
                }
                char wBtn[180];
                std::snprintf(wBtn, sizeof wBtn, "%sSlot %d (%s): %s",
                              isFirst ? "[FIRST SELECTED] " : "", s + 1, wildsName(s),
                              desc.c_str());
                if (ImGui::Button(wBtn, ImVec2(-1.f, 34.f))) {
                    table_->checkpoint();
                    showToast(table_->resolveWildsSwapSlotClick(s));
                }
                if (isFirst) {
                    ImGui::PopStyleColor();
                }
                ImGui::EndDisabled();
            }
            ImGui::Spacing();
            if (ImGui::Button("Cancel Switch", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    // 7. ROLE CARD MODALS (id 75 The Highest Tech, id 77 Position and Situation, id 81 Partners in Crime)
    if (st.pendingStatSwapChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("THE HIGHEST TECH: SWAP CARD RECRUIT & ATTACK###StatSwapModal", nullptr,
                         flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Choose a card in your Hand or Played Area to swap its printed Recruit * and Attack /// this turn:");
            ImGui::Separator();
            const std::string handZid = table_->activePlayerZone("hand");
            const std::string playedZid = table_->activePlayerZone("played");
            ImGui::BeginChild("StatSwapList", ImVec2(0.f, 230.f), true);
            for (const auto& p : st.piles) {
                if ((p.zone != handZid && p.zone != playedZid) || p.cards.empty()) continue;
                const CardInst& c = p.top();
                const CardDef& cd = catalog_.card(c.def);
                int curR = 0, curA = 0;
                table_->effectiveCardStats(c, &curR, &curA);
                ImGui::PushID(static_cast<int>(c.uid));
                char sBtn[180];
                std::snprintf(sBtn, sizeof sBtn, "Swap %s  (%d * / %d /// -> %d * / %d ///)",
                              (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()), curR, curA,
                              curA, curR);
                if (ImGui::Button(sBtn, ImVec2(-1.f, 32.f))) {
                    table_->checkpoint();
                    showToast(table_->resolveStatSwapChoice(c.uid));
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    if (st.pendingDiscardToDeckChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("POSITION AND SITUATION: PLACE DISCARD CARD ON DECK TOP###DiscToDeckModal",
                         nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Choose a card from your Discard pile to place face-down on top of your Deck:");
            ImGui::Separator();
            const Pile* disc = table_->findZonePile(table_->activePlayerZone("discard"));
            ImGui::BeginChild("DiscToDeckList", ImVec2(0.f, 230.f), true);
            if (disc) {
                for (int i = static_cast<int>(disc->cards.size()) - 1; i >= 0; --i) {
                    const CardInst& c = disc->cards[static_cast<size_t>(i)];
                    const CardDef& cd = catalog_.card(c.def);
                    ImGui::PushID(static_cast<int>(c.uid));
                    char dBtn[180];
                    std::snprintf(dBtn, sizeof dBtn, "Place on Deck Top: %s  (%d * / %d ///)",
                                  (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                                  std::max(0, cd.recruit), std::max(0, cd.attack));
                    if (ImGui::Button(dBtn, ImVec2(-1.f, 32.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveDiscardToDeckChoice(c.uid));
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    if (st.pendingGainCardChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("PARTNERS IN CRIME: GAIN COMMANDER OR HQ CARD (COST <= 3)###GainCardModal",
                         nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Choose a Commander or an HQ card costing 3 * or less to gain to your Discard pile:");
            ImGui::Separator();
            if (const Pile* cmd = table_->findZonePile("commanders");
                cmd && !cmd->cards.empty()) {
                const CardDef& cd = catalog_.card(cmd->top().def);
                const std::string cBtn = "Gain Commander: " + cd.name + " (Cost 3 *)";
                if (ImGui::Button(cBtn.c_str(), ImVec2(-1.f, 34.f))) {
                    table_->checkpoint();
                    showToast(table_->resolveGainCardChoice("commanders"));
                }
            }
            for (int h = 0; h < 5; ++h) {
                const std::string hZid = "hq_" + std::to_string(h);
                const Pile* hp = table_->findZonePile(hZid);
                if (!hp || hp->cards.empty()) continue;
                const CardDef& hd = catalog_.card(hp->top().def);
                const int cost = std::max(0, hd.cost);
                ImGui::BeginDisabled(cost > 3);
                char hBtn[180];
                std::snprintf(hBtn, sizeof hBtn, "HQ %d: %s  (Cost %d *)", h + 1, hd.name.c_str(),
                              cost);
                if (ImGui::Button(hBtn, ImVec2(-1.f, 34.f))) {
                    table_->checkpoint();
                    showToast(table_->resolveGainCardChoice(hZid));
                }
                ImGui::EndDisabled();
            }
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    // 8. LIKE A HUNTER (id 109): Informational Top-3 Deck Reveal Modal
    if (st.pendingLikeAHunterModal) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(680.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("LIKE A HUNTER — TOP 3 PLAYER DECK REVEAL###LikeAHunterModal", nullptr,
                         flags)) {
            ImGui::TextColored(ImVec4(0.98f, 0.45f, 0.45f, 1.f),
                               "Like A Hunter Revealed: Each player revealed the top 3 cards of their deck!");
            ImGui::TextDisabled("(Cards with Cost >= 1 were discarded; Cost 0 Starters were returned to the top of the deck.)");
            ImGui::Separator();

            ImGui::BeginChild("LikeAHunterList", ImVec2(0.f, 220.f), true);
            for (const auto& entry : st.likeAHunterRevealed) {
                const CardDef& cd = catalog_.card(entry.card.def);
                const ImVec4 col = entry.discarded ? ImVec4(0.98f, 0.48f, 0.48f, 1.f)
                                                   : ImVec4(0.55f, 0.95f, 0.68f, 1.f);
                ImGui::TextColored(col, "P%d: %-30s (Cost %d) -> %s", entry.playerIdx + 1,
                                   (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                                   entry.cost,
                                   entry.discarded ? "DISCARDED" : "KEPT ON TOP OF DECK");
            }
            ImGui::EndChild();

            if (ImGui::Button("Acknowledge & Continue", ImVec2(-1.f, 34.f))) {
                table_->dismissLikeAHunterModal();
            }
        }
        ImGui::End();
    }

    // 9. OUT FRONT HAVING LUNCH (id 18): Each player may kill a card in their discard pile
    if (st.pendingDiscardKillChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("OUT FRONT HAVING LUNCH [SURVIVAL]: KILL DISCARD PILE CARDS###DiscardKillModal",
                         nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Each player may kill up to 1 card in their Discard pile:");
            ImGui::Separator();
            ImGui::BeginChild("DiscardKillList", ImVec2(0.f, 240.f), true);
            for (int q = 0; q < st.playerCount; ++q) {
                if (q >= static_cast<int>(st.players.size())) break;
                const PlayerState& ps = st.players[static_cast<size_t>(q)];
                if (!ps.alive || ps.escapedInChoppa) continue;
                const bool done = std::find(st.discardKillDonePlayers.begin(),
                                            st.discardKillDonePlayers.end(),
                                            q) != st.discardKillDonePlayers.end();
                if (done) {
                    ImGui::TextDisabled("Player %d: Already killed a card from Discard.", q + 1);
                    continue;
                }
                const Pile* disc = table_->findZonePile(table_->playerZone(q, "discard"));
                if (!disc || disc->cards.empty()) {
                    ImGui::TextDisabled("Player %d: Discard pile is empty.", q + 1);
                    continue;
                }
                ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.35f, 1.f), "Player %d (%s) Discard:",
                                   q + 1, shortAvatarTitle(catalog_, ps.avatarDefId));
                for (int i = static_cast<int>(disc->cards.size()) - 1; i >= 0; --i) {
                    const CardInst& c = disc->cards[static_cast<size_t>(i)];
                    const CardDef& cd = catalog_.card(c.def);
                    ImGui::PushID(static_cast<int>(c.uid));
                    char kBtn[180];
                    std::snprintf(kBtn, sizeof kBtn, "P%d Kill: %s  (%d * / %d ///)", q + 1,
                                  (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                                  std::max(0, cd.recruit), std::max(0, cd.attack));
                    if (ImGui::Button(kBtn, ImVec2(-1.f, 30.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveDiscardKillChoice(q, c.uid));
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            if (ImGui::Button("Done / Finish", ImVec2(-1.f, 34.f))) {
                table_->checkpoint();
                showToast(table_->finishDiscardKillChoice());
            }
        }
        ImGui::End();
    }

    // 10. THE DOOR SWINGS BOTH WAYS (id 22): Discard any number of cards from hand, then draw that many
    if (st.pendingDoorSwingsChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("THE DOOR SWINGS BOTH WAYS [INTEL]: DISCARD & REDRAW###DoorSwingsModal",
                         nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Select any number of cards in your Hand to discard, then draw that many cards:");
            ImGui::Separator();
            const std::string hZid = table_->activePlayerZone("hand");
            ImGui::BeginChild("DoorSwingsHandList", ImVec2(0.f, 230.f), true);
            for (const auto& p : st.piles) {
                if (p.zone != hZid || p.cards.empty()) continue;
                const CardInst& c = p.top();
                const CardDef& cd = catalog_.card(c.def);
                const bool sel = std::find(st.doorSwingsSelectedUids.begin(),
                                           st.doorSwingsSelectedUids.end(),
                                           c.uid) != st.doorSwingsSelectedUids.end();
                ImGui::PushID(static_cast<int>(c.uid));
                if (sel) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.30f, 1.f));
                }
                char dBtn[180];
                std::snprintf(dBtn, sizeof dBtn, "%s%s  (%d * / %d ///)",
                              sel ? "[SELECTED] " : "",
                              (cd.title.empty() ? cd.name.c_str() : cd.title.c_str()),
                              std::max(0, cd.recruit), std::max(0, cd.attack));
                if (ImGui::Button(dBtn, ImVec2(-1.f, 32.f))) {
                    table_->toggleDoorSwingsCard(c.uid);
                }
                if (sel) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            char confirmLbl[128];
            std::snprintf(confirmLbl, sizeof confirmLbl,
                          "Discard %zu Selected Card(s) & Draw %zu",
                          st.doorSwingsSelectedUids.size(), st.doorSwingsSelectedUids.size());
            if (ImGui::Button(confirmLbl, ImVec2(-1.f, 34.f))) {
                table_->checkpoint();
                showToast(table_->resolveDoorSwingsChoice(rng_()));
            }
            if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }

    // 11. BAG OF TRICKS (id 31): Move a Mark from one player to another
    if (st.pendingMoveMarkChoice) {
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.f, 0.f), ImGuiCond_Always);
        if (ImGui::Begin("BAG OF TRICKS: MOVE A MARK BETWEEN PLAYERS###MoveMarkModal", nullptr,
                         flags)) {
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.62f, 1.f),
                               "Choose which Mark transfer to perform:");
            ImGui::Separator();
            for (int srcP = 0; srcP < st.playerCount; ++srcP) {
                if (srcP >= static_cast<int>(st.players.size())) break;
                const PlayerState& sps = st.players[static_cast<size_t>(srcP)];
                if (!sps.alive || sps.escapedInChoppa || sps.marksCount <= 0) continue;
                for (int dstP = 0; dstP < st.playerCount; ++dstP) {
                    if (dstP == srcP || dstP >= static_cast<int>(st.players.size())) continue;
                    const PlayerState& dps = st.players[static_cast<size_t>(dstP)];
                    if (!dps.alive || dps.escapedInChoppa) continue;
                    char mBtn[180];
                    std::snprintf(mBtn, sizeof mBtn,
                                  "Move 1 Mark from P%d (%d Marks) -> P%d (%d Marks)", srcP + 1,
                                  sps.marksCount, dstP + 1, dps.marksCount);
                    if (ImGui::Button(mBtn, ImVec2(-1.f, 34.f))) {
                        table_->checkpoint();
                        showToast(table_->resolveMoveMarkChoice(srcP, dstP));
                    }
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(-1.f, 0.f))) {
                showToast(table_->cancelOptionalModal());
            }
        }
        ImGui::End();
    }
}

void App::renderContextMenu() {
    if (openContextMenu_) {
        ImGui::OpenPopup("PileContextMenu");
        openContextMenu_ = false;
    }

    if (!ImGui::BeginPopup("PileContextMenu")) return;

    Pile* p = table_->find(contextPile_);
    if (!p || p->cards.empty()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const TableState& st = table_->state();
    const CardInst& top = p->top();
    const CardDef& def = catalog_.card(top.def);
    const std::string title = top.faceUp ? def.name : "Face-Down Card";
    ImGui::TextColored(ImVec4(0.50f, 0.94f, 0.65f, 1.f), "%s", title.c_str());
    ImGui::TextDisabled("%zu card%s in pile  |  Zone: %s", p->cards.size(),
                        p->cards.size() == 1 ? "" : "s",
                        p->zone.empty() ? "Loose Table" : p->zone.c_str());
    if (p->cards.size() >= 2 && p->cards.front().attached) {
        const CardInst& bot = p->cards.front();
        const std::string botName =
            bot.faceUp ? catalog_.card(bot.def).name : std::string("Hidden Attached Card");
        ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.18f, 1.f), "Attached Underneath: %s",
                           botName.c_str());
    }
    ImGui::Separator();

    // Multi-Selection & Stack Actions
    if (ImGui::MenuItem(isSelected(p->id) ? "Deselect Pile" : "Select Pile for Multi-Move",
                        "Ctrl+Click")) {
        toggleSelect(p->id);
    }
    if (selectedPiles_.size() >= 2 &&
        ImGui::MenuItem("Stack All Selected Piles Here", "G")) {
        table_->checkpoint();
        const Vec2 pos = p->pos;
        const PileId merged = table_->gatherPiles(selectedPiles_, pos);
        selectedPiles_.clear();
        if (merged != kNoPile) selectedPiles_.push_back(merged);
    }
    ImGui::Separator();

    // Quick Send-to-Player-Zone Actions (supports active player and all 1..5 player boards)
    if (ImGui::BeginMenu("Move Pile / Stack To Player Zone...")) {
        auto sendToZone = [&](const std::string& dstZoneId) {
            table_->checkpoint();
            const std::string srcZone = p->zone;
            DropTarget dt;
            dt.zone = layout_.zone(dstZoneId);
            if (dt.zone) {
                dt.kind = (dt.zone->kind == ZoneKind::Slot) ? DropTarget::Kind::Slot
                                                            : DropTarget::Kind::Area;
                dt.pos = dt.zone->rect.center();
                table_->applyDropWithLog(p->id, dt, srcZone);
            }
        };
        auto renderZoneItems = [&](int pIdx) {
            if (ImGui::MenuItem("Hand Area (Fanned)")) sendToZone(table_->playerZone(pIdx, "hand"));
            if (ImGui::MenuItem("Played / Active Area"))
                sendToZone(table_->playerZone(pIdx, "played"));
            if (ImGui::MenuItem("Discard Area (Stack)"))
                sendToZone(table_->playerZone(pIdx, "discard"));
            if (ImGui::MenuItem("Deck Area (Face-Down Stack)"))
                sendToZone(table_->playerZone(pIdx, "deck"));
            if (ImGui::MenuItem("Strike Area (Damage)"))
                sendToZone(table_->playerZone(pIdx, "strikes"));
            if (ImGui::MenuItem("Mark Area")) sendToZone(table_->playerZone(pIdx, "marks"));
        };

        if (st.playerCount <= 1) {
            renderZoneItems(0);
        } else {
            for (int i = 0; i < st.playerCount; ++i) {
                const int avId = (i < static_cast<int>(st.players.size()))
                                     ? st.players[static_cast<size_t>(i)].avatarDefId
                                     : 72;
                char subLbl[80];
                std::snprintf(subLbl, sizeof subLbl, "Player %d (%s)%s", i + 1,
                              shortAvatarTitle(catalog_, avId),
                              (i == st.activePlayer) ? " [Active]" : "");
                if (ImGui::BeginMenu(subLbl)) {
                    renderZoneItems(i);
                    ImGui::EndMenu();
                }
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();

    const TurnPhase ph = st.phase;
    int zoneOwnerP = -1;
    std::string_view zoneRole;
    const bool isPZone = parsePlayerZone(p->zone, zoneOwnerP, zoneRole);

    if (ph == TurnPhase::EnemyPhase && p->zone == "prey") {
        if (ImGui::MenuItem("Draw Enemy to Wilds (Enemy Phase)", "Double-Click / Enter")) {
            showToast(table_->resolveEnemyPhase(rng_()));
        }
        ImGui::Separator();
    } else if (ph == TurnPhase::ActionPhase) {
        std::string abilLbl;
        if (table_->canActivateCardAbility(top.uid, &abilLbl, nullptr)) {
            const std::string menuLbl = "Activate Ability: " + abilLbl;
            if (ImGui::MenuItem(menuLbl.c_str())) {
                table_->checkpoint();
                showToast(table_->activateCardAbility(top.uid, rng_()));
            }
            ImGui::Separator();
        }
        if (isPZone && zoneRole == "hand") {
            if (zoneOwnerP == st.activePlayer) {
                if (ImGui::MenuItem("Play Card to Played Area", "Double-Click")) {
                    showToast(table_->playCard(p->id, rng_()));
                }
            }
            bool hasCoord = (zoneOwnerP >= 0 && zoneOwnerP < static_cast<int>(st.players.size()) &&
                             st.players[static_cast<size_t>(zoneOwnerP)].allHandCoordinateThisTurn);
            for (const auto& kw : def.keywords) {
                if (kw == "Coordinate") hasCoord = true;
            }
            if (hasCoord && (st.playerCount == 1 || zoneOwnerP != st.activePlayer)) {
                const char* coordLbl =
                    (st.playerCount == 1)
                        ? "Coordinate Card (Solo 1P: Discard & Draw 1 for Call For Backup)"
                        : "Coordinate Card (Lend Virtual Copy & Draw 1)";
                if (ImGui::MenuItem(coordLbl,
                                    zoneOwnerP != st.activePlayer ? "Double-Click" : nullptr)) {
                    showToast(table_->coordinateCard(p->id, rng_()));
                }
            }
            ImGui::Separator();
        } else if (p->zone == "commanders" || p->zone.rfind("hq_", 0) == 0) {
            const int cost = (p->zone == "commanders") ? 3 : std::max(0, def.cost);
            const std::string recLabel = "Recruit Card (" + std::to_string(cost) + " *)";
            if (ImGui::MenuItem(recLabel.c_str(), "Double-Click")) {
                showToast(table_->recruitCard(p->id, rng_()));
            }
            ImGui::Separator();
        } else if (p->zone.rfind("wilds_", 0) == 0 && !top.faceUp) {
            const int scanCost = table_->scanCostForZone(p->zone);
            const std::string scanLabel = "Scan Space (" + std::to_string(scanCost) + " ///)";
            if (ImGui::MenuItem(scanLabel.c_str(), "Double-Click")) {
                showToast(table_->scanWilds(p->id, rng_()));
            }
            ImGui::Separator();
        } else if ((p->zone.rfind("wilds_", 0) == 0 || p->zone == "combat") && top.faceUp) {
            const char* fightLbl =
                (top.def == 102)
                    ? "Wipe Out Guerrilla Camp (1 ///)"
                    : (top.def == 114 ? "Board The Choppa! (Spend 3 *)" : "Fight / Resolve Card");
            if (ImGui::MenuItem(fightLbl, "Double-Click")) {
                showToast(table_->fightEnemy(p->id, rng_()));
            }
            ImGui::Separator();
        }
    }

    if (top.faceUp && ImGui::MenuItem("Inspect Card (3D)", "I / Double-Click")) {
        inspectCardDef_ = top.def;
        inspectPile_ = p->id;
    }
    if (p->cards.size() >= 2 && p->cards.front().attached && p->cards.front().faceUp) {
        const std::string inspAtt =
            "Inspect Attached Bottom Card (" + catalog_.card(p->cards.front().def).name + ")";
        if (ImGui::MenuItem(inspAtt.c_str())) {
            inspectCardDef_ = p->cards.front().def;
            inspectPile_ = p->id;
        }
    }
    if (p->cards.size() > 1 && ImGui::MenuItem("Browse / Search Stack...")) {
        browsePile_ = p->id;
    }
    if (ImGui::MenuItem(top.faceUp ? "Flip Top Card Face-Down" : "Flip Top Card Face-Up", "F")) {
        table_->checkpoint();
        table_->flipTop(p->id);
        if (const Pile* after = table_->find(contextPile_); after && !after->cards.empty()) {
            table_->recordMove("FLIP_CARD", def.name, 1, after->zone, after->zone,
                               static_cast<int>(after->cards.size()), after->top().faceUp,
                               "Flipped top card");
        }
    }
    if (p->cards.size() > 1 && ImGui::MenuItem("Flip Entire Stack", "Shift+F")) {
        table_->checkpoint();
        table_->flipPile(p->id);
        table_->recordMove("FLIP_PILE", def.name, static_cast<int>(p->cards.size()), p->zone,
                           p->zone, static_cast<int>(p->cards.size()), p->top().faceUp,
                           "Flipped entire stack");
    }
    if (p->cards.size() > 1 && ImGui::MenuItem("Shuffle Stack", "S")) {
        table_->checkpoint();
        table_->shuffle(p->id, rng_());
        table_->recordMove("SHUFFLE_PILE", p->label.empty() ? p->zone : p->label,
                           static_cast<int>(p->cards.size()), p->zone, p->zone,
                           static_cast<int>(p->cards.size()), p->top().faceUp, "Shuffled stack");
        showToast("Shuffled pile (" + std::to_string(p->cards.size()) + " cards)");
    }
    if (ImGui::MenuItem("Rotate 90 Degrees", "R")) {
        table_->checkpoint();
        table_->rotate(p->id);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Report Bug with This Card / Pile...", "B")) {
        openBugReportModal(p->id, top.def);
    }

    ImGui::EndPopup();
}

void App::renderInspectModal() {
    if (inspectCardDef_ < 0 || inspectCardDef_ >= static_cast<int>(catalog_.cards.size())) return;

    const CardDef& def = catalog_.card(inspectCardDef_);
    const Texture tex = textures_->request(def.texture);

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoCollapse;

    bool open = true;
    const std::string displayTitle = !def.title.empty()
                                         ? (def.subtitle.empty() ? def.title : (def.title + " — " + def.subtitle))
                                         : def.name;
    const std::string winTitle = displayTitle + "  ###InspectModal";
    if (ImGui::Begin(winTitle.c_str(), &open, flags)) {
        const float maxH = vp.y * 0.76f;
        const float aspect = def.landscape ? (1024.f / 726.f) : (712.f / 1024.f);
        const ImVec2 imgSize = def.landscape ? ImVec2(maxH * aspect * 0.8f, maxH * 0.8f)
                                             : ImVec2(maxH * aspect, maxH);
        const GLuint tid = tex ? tex.id : backTex_.id;

        ImGui::BeginGroup();
        const ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##Card3DCanvas", imgSize);
        if (ImGui::IsItemClicked()) open = false;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 pMax = ImVec2(pMin.x + imgSize.x, pMin.y + imgSize.y);
        const ImVec2 center = ImVec2((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);

        // Interactive 3D perspective quad tilt using the exact scanned card texture
        float tiltX = 0.f, tiltY = 0.f;
        if (hovered) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            tiltX = std::clamp((mp.x - center.x) / (imgSize.x * 0.5f), -1.f, 1.f);
            tiltY = std::clamp((mp.y - center.y) / (imgSize.y * 0.5f), -1.f, 1.f);
        }
        const float perspX = tiltY * 14.f;
        const float perspY = -tiltX * 14.f;
        const float scaleLift = hovered ? 1.018f : 1.0f;
        const float hw = imgSize.x * 0.5f * scaleLift;
        const float hh = imgSize.y * 0.5f * scaleLift;

        const ImVec2 cTL(center.x - hw - perspX, center.y - hh + perspY);
        const ImVec2 cTR(center.x + hw + perspX, center.y - hh - perspY);
        const ImVec2 cBR(center.x + hw - perspX, center.y + hh - perspY);
        const ImVec2 cBL(center.x - hw + perspX, center.y + hh + perspY);

        ImDrawList* wdl = ImGui::GetWindowDrawList();
        const ImVec2 sOff(8.f - tiltX * 12.f, 12.f - tiltY * 12.f);
        wdl->AddQuadFilled(ImVec2(cTL.x + sOff.x, cTL.y + sOff.y),
                           ImVec2(cTR.x + sOff.x, cTR.y + sOff.y),
                           ImVec2(cBR.x + sOff.x, cBR.y + sOff.y),
                           ImVec2(cBL.x + sOff.x, cBL.y + sOff.y),
                           IM_COL32(0, 0, 0, 165));

        wdl->AddImageQuad(static_cast<ImTextureID>(tid), cTL, cTR, cBR, cBL,
                          ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                          IM_COL32(255, 255, 255, 255));

        const ImU32 rimCol = (def.rarity == "Rare")
                                 ? IM_COL32(245, 180, 35, 235)
                                 : (!def.rarity.empty() ? IM_COL32(90, 210, 130, 210)
                                                        : IM_COL32(60, 110, 80, 180));
        wdl->AddQuad(cTL, cTR, cBR, cBL, rimCol, 2.2f);

        ImGui::TextDisabled("Group: %s  |  Copies in box: %d  (Hover card for 3D tilt, click to close)",
                            def.group.c_str(), def.qty);
        ImGui::EndGroup();

        // If rich card metadata is available, render Tactical Breakdown HUD & Direct Actions
        if (!def.title.empty() || !def.rulesText.empty()) {
            ImGui::SameLine(0.f, 20.f);
            ImGui::BeginChild("TacticalInspectHUD", ImVec2(390.f, imgSize.y), true);

            ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.20f, 1.f), "%s",
                               def.title.empty() ? def.name.c_str() : def.title.c_str());
            if (!def.subtitle.empty()) {
                ImGui::TextColored(ImVec4(0.60f, 0.90f, 0.70f, 1.f), "%s  |  %s",
                                   def.subtitle.c_str(),
                                   def.rarity.empty() ? def.group.c_str() : def.rarity.c_str());
            }
            ImGui::Separator();

            // Direct On-Table Rule Action Buttons if inspected from an active board pile
            const TableState& st = table_->state();
            if (Pile* ip = table_->find(inspectPile_);
                ip && !ip->cards.empty() && ip->top().def == inspectCardDef_ &&
                st.phase == TurnPhase::ActionPhase) {
                std::string abilLbl;
                if (table_->canActivateCardAbility(ip->top().uid, &abilLbl, nullptr)) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.38f, 0.52f, 1.f));
                    const std::string actBtn = "Activate Ability: " + abilLbl;
                    if (ImGui::Button(actBtn.c_str(), ImVec2(-1.f, 0.f))) {
                        table_->checkpoint();
                        showToast(table_->activateCardAbility(ip->top().uid, rng_()));
                        open = false;
                    }
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                }
                int ownerP = -1;
                std::string_view role;
                const bool isPZone = parsePlayerZone(ip->zone, ownerP, role);
                if (isPZone && role == "hand") {
                    if (ownerP == st.activePlayer) {
                        if (ImGui::Button("Play Card to Played Area", ImVec2(-1.f, 0.f))) {
                            showToast(table_->playCard(ip->id, rng_()));
                            open = false;
                        }
                    }
                    bool hasCoord =
                        (ownerP >= 0 && ownerP < static_cast<int>(st.players.size()) &&
                         st.players[static_cast<size_t>(ownerP)].allHandCoordinateThisTurn);
                    for (const auto& kw : def.keywords) {
                        if (kw == "Coordinate") hasCoord = true;
                    }
                    if (hasCoord && (st.playerCount == 1 || ownerP != st.activePlayer) &&
                        ImGui::Button("Coordinate Card (Draw 1 & Virtual Copy)", ImVec2(-1.f, 0.f))) {
                        showToast(table_->coordinateCard(ip->id, rng_()));
                        open = false;
                    }
                    ImGui::Separator();
                } else if (ip->zone == "commanders" || ip->zone.rfind("hq_", 0) == 0) {
                    const int cost = (ip->zone == "commanders") ? 3 : std::max(0, def.cost);
                    ImGui::BeginDisabled(st.recruitPool < cost);
                    const std::string btn = "Recruit Card (" + std::to_string(cost) + " *)";
                    if (ImGui::Button(btn.c_str(), ImVec2(-1.f, 0.f))) {
                        showToast(table_->recruitCard(ip->id, rng_()));
                        open = false;
                    }
                    ImGui::EndDisabled();
                    ImGui::Separator();
                } else if (ip->zone.rfind("wilds_", 0) == 0 || ip->zone == "combat") {
                    const char* btnLbl =
                        (def.id == 102)
                            ? "Wipe Out Guerrilla Camp (1 ///)"
                            : (def.id == 114 ? "Board The Choppa! (Spend 3 *)"
                                             : "Fight / Resolve Card");
                    if (ImGui::Button(btnLbl, ImVec2(-1.f, 0.f))) {
                        const std::uint32_t prevUid = ip->top().uid;
                        showToast(table_->fightEnemy(ip->id, rng_()));
                        const Pile* after = table_->find(inspectPile_);
                        if (!after || after->cards.empty() || after->top().uid != prevUid) {
                            open = false;
                        }
                    }
                    ImGui::Separator();
                }
            }

            // Authentic Stat Identifiers Row
            if (def.attack > 0 || (def.attack == 0 && def.recruit == 0 && def.cost > 0)) {
                ImGui::TextColored(ImVec4(0.95f, 0.32f, 0.32f, 1.f), "Attack (Claw Slash): %d", def.attack);
            }
            if (def.recruit > 0) {
                ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.25f, 1.f), "Recruit (Star): +%d", def.recruit);
            }
            if (def.cost >= 0) {
                ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.18f, 1.f), "Cost (Gold Circle): %d", def.cost);
            }
            if (!def.classType.empty()) {
                ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.50f, 1.f), "Class Badge: %s", def.classType.c_str());
            }
            if (!def.team.empty()) {
                ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.60f, 1.f), "Team Badge: %s", def.team.c_str());
            }

            if (!def.keywords.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                for (const auto& kw : def.keywords) {
                    ImGui::TextColored(ImVec4(0.96f, 0.35f, 0.35f, 1.f), "[Keyword: %s]", kw.c_str());
                    if (kw == "Coordinate") {
                        ImGui::TextWrapped("During another player's turn, set this card aside from your hand "
                                           "face-up and immediately draw 1 replacement card. The active turn "
                                           "player gains a virtual copy of it (providing its stats, class, team, "
                                           "character, and ability until end of turn).");
                    } else if (kw == "Brothers in Arms") {
                        ImGui::TextWrapped("Triggers once for each OTHER distinct Barracks character "
                                           "(0 to 3 times across Blain, Dillon, Dutch, Mac) present in your "
                                           "current turn cards (Hand + Played Area + Coordinated cards)!");
                    } else if (kw == "Attach") {
                        ImGui::TextWrapped("Moves normally through The Wilds while face-down. When revealed, "
                                           "attaches to its target Location (sitting at the bottom of a 2-card "
                                           "stack if another card occupies that space) and becomes immune to "
                                           "regular Phase 1 movement. Clear its Location and spend 1 /// Attack "
                                           "(or move Guerrilla Camp left into the Combat Zone) to discard it.");
                    }
                }
            }

            if (!def.rulesText.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.f), "Card Text:");
                ImGui::TextWrapped("%s", def.rulesText.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.22f, 0.14f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.30f, 0.18f, 1.f));
            if (ImGui::Button("Report Bug with This Card [B]", ImVec2(-1.f, 0.f))) {
                openBugReportModal(inspectPile_, inspectCardDef_);
            }
            ImGui::PopStyleColor(2);

            ImGui::EndChild();
        }
    }
    ImGui::End();
    if (!open) {
        inspectCardDef_ = -1;
        inspectPile_ = kNoPile;
    }
}

void App::renderPileBrowserModal() {
    if (browsePile_ == kNoPile) return;
    Pile* p = table_->find(browsePile_);
    if (!p || p->cards.empty()) {
        browsePile_ = kNoPile;
        return;
    }

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowSize(ImVec2(760.f, 560.f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    const std::string title =
        (p->label.empty() ? (p->zone.empty() ? "Pile Contents" : p->zone) : p->label) + " (" +
        std::to_string(p->cards.size()) + " cards)###PileBrowser";
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::Button("Shuffle Pile")) {
            table_->checkpoint();
            table_->shuffle(p->id, rng_());
            table_->recordMove("SHUFFLE_PILE", p->zone, static_cast<int>(p->cards.size()), p->zone,
                               p->zone, static_cast<int>(p->cards.size()), p->top().faceUp,
                               "Shuffled via Pile Browser");
            showToast("Shuffled pile");
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip All Face-Up")) {
            table_->checkpoint();
            for (auto& c : p->cards) c.faceUp = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip All Face-Down")) {
            table_->checkpoint();
            for (auto& c : p->cards) c.faceUp = false;
        }
        ImGui::Separator();

        const std::string handZid = table_->activePlayerZone("hand");
        const Vec2 handPos =
            layout_.zone(handZid) ? layout_.zone(handZid)->rect.center() : Vec2{1185.f, 1502.f};

        ImGui::BeginChild("PileCardList");
        for (int i = static_cast<int>(p->cards.size()) - 1; i >= 0; --i) {
            const CardInst& ci = p->cards[static_cast<size_t>(i)];
            const CardDef& def = catalog_.card(ci.def);
            ImGui::PushID(static_cast<int>(ci.uid));

            const int posFromTop = static_cast<int>(p->cards.size()) - i;
            if (ci.attached) {
                ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.18f, 1.f), "#%-3d  %s [ATTACHED]",
                                   posFromTop, def.name.c_str());
            } else {
                ImGui::Text("#%-3d  %s", posFromTop, def.name.c_str());
            }
            ImGui::SameLine(390.f);
            if (ImGui::SmallButton("Inspect")) {
                inspectCardDef_ = ci.def;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Pull to Hand")) {
                table_->checkpoint();
                const std::string srcZone = p->zone;
                const PileId pulled = table_->pullCardAt(p->id, static_cast<size_t>(i), handPos);
                if (Pile* pp = table_->find(pulled)) {
                    pp->cards.back().attached = false;
                    pp->zone = handZid;
                }
                table_->reflowZone(handZid);
                std::string delta;
                table_->syncStateFromZones(&delta);
                table_->recordMove("PULL_TO_HAND", def.name, 1, srcZone, handZid, 1, true, delta);
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Pull to Table")) {
                table_->checkpoint();
                const std::string srcZone = p->zone;
                const Vec2 spawnPos = p->pos + Vec2{220.f, 0.f};
                const PileId pulled = table_->pullCardAt(p->id, static_cast<size_t>(i), spawnPos);
                if (Pile* pp = table_->find(pulled)) {
                    pp->cards.back().attached = false;
                }
                table_->recordMove("PULL_CARD", def.name, 1, srcZone, "loose", 1, true,
                                   "Pulled from stack");
                ImGui::PopID();
                break;
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) browsePile_ = kNoPile;
}

void App::renderCatalogModal() {
    if (!showCatalogModal_) return;

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowSize(ImVec2(840.f, 600.f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Box Reserve & Card Catalog (All 331 Printings)", &showCatalogModal_,
                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::InputTextWithHint("##Filter", "Search card name or group...", catalogFilter_,
                                 sizeof catalogFilter_);
        ImGui::Separator();

        const std::string handZid = table_->activePlayerZone("hand");
        const std::string discZid = table_->activePlayerZone("discard");
        const Vec2 handPos =
            layout_.zone(handZid) ? layout_.zone(handZid)->rect.center() : Vec2{1185.f, 1502.f};
        const Vec2 discPos =
            layout_.zone(discZid) ? layout_.zone(discZid)->rect.center() : Vec2{2207.f, 1367.f};

        ImGui::BeginChild("CatalogScroll");
        for (const auto& def : catalog_.cards) {
            if (!containsCaseInsensitive(def.name, catalogFilter_) &&
                !containsCaseInsensitive(def.group, catalogFilter_)) {
                continue;
            }
            ImGui::PushID(def.id);
            ImGui::Text("%-36s", def.name.c_str());
            ImGui::SameLine(330.f);
            ImGui::TextDisabled("%-26s (x%d)", def.group.c_str(), def.qty);
            ImGui::SameLine(570.f);
            if (ImGui::SmallButton("Inspect")) {
                inspectCardDef_ = def.id;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+Hand")) {
                table_->checkpoint();
                const PileId pid = table_->spawnCard(def.id, handPos, true);
                if (Pile* p = table_->find(pid)) p->zone = handZid;
                table_->reflowZone(handZid);
                std::string delta;
                table_->syncStateFromZones(&delta);
                table_->recordMove("SPAWN_CARD", def.name, 1, "box_reserve", handZid, 1, true,
                                   delta);
                showToast("Added " + def.name + " to P" +
                          std::to_string(table_->state().activePlayer + 1) + " Hand");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+Discard")) {
                table_->checkpoint();
                const PileId pid = table_->spawnCard(def.id, discPos, true);
                if (Pile* p = table_->find(pid)) p->zone = discZid;
                std::string delta;
                table_->syncStateFromZones(&delta);
                table_->recordMove("SPAWN_CARD", def.name, 1, "box_reserve", discZid, 1, true,
                                   delta);
                showToast("Added " + def.name + " to P" +
                          std::to_string(table_->state().activePlayer + 1) + " Discard");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+Table")) {
                table_->checkpoint();
                table_->spawnCard(def.id, camera_.screenToWorld(camera_.viewport() * 0.5f), true);
                table_->recordMove("SPAWN_CARD", def.name, 1, "box_reserve", "loose", 1, true,
                                   "Spawned onto table");
                showToast("Spawned " + def.name);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void App::openBugReportModal(PileId focusPile, int focusCardDef, const std::string& focusSubject,
                             const std::string& focusZone, const std::string& prefillNotes) {
    showBugReportModal_ = true;
    bugReportTab_ = 0; // Open on "+ Record New Bug Report" tab

    std::string subjCard = focusSubject;
    std::string subjZone = focusZone;

    if (subjCard.empty() && focusPile != kNoPile) {
        if (const Pile* p = table_->find(focusPile); p && !p->cards.empty()) {
            const CardInst& top = p->top();
            const CardDef& d = catalog_.card(top.def);
            subjCard = "#" + std::to_string(d.id) + " " + (d.title.empty() ? d.name : d.title);
            if (!d.subtitle.empty()) subjCard += " (" + d.subtitle + ")";
            if (!top.faceUp) subjCard += " [Face-Down]";
            if (subjZone.empty()) subjZone = p->zone.empty() ? "loose" : p->zone;
        }
    }
    if (subjCard.empty() && focusCardDef >= 0 &&
        focusCardDef < static_cast<int>(catalog_.cards.size())) {
        const CardDef& d = catalog_.card(focusCardDef);
        subjCard = "#" + std::to_string(d.id) + " " + (d.title.empty() ? d.name : d.title);
        if (!d.subtitle.empty()) subjCard += " (" + d.subtitle + ")";
    }
    if (subjCard.empty() && !table_->state().moveLog.empty()) {
        for (int i = static_cast<int>(table_->state().moveLog.size()) - 1; i >= 0; --i) {
            const MoveLogEntry& mv = table_->state().moveLog[static_cast<size_t>(i)];
            if (mv.actionType == "BUG_REPORT") continue;
            subjCard = mv.cardSummary + " (" + mv.actionType + " #" + std::to_string(mv.seq) + ")";
            if (subjZone.empty()) subjZone = mv.fromZone + " -> " + mv.toZone;
            break;
        }
    }

    std::snprintf(bugSubjectCardBuf_, sizeof bugSubjectCardBuf_, "%s", subjCard.c_str());
    std::snprintf(bugSubjectZoneBuf_, sizeof bugSubjectZoneBuf_, "%s", subjZone.c_str());
    if (!prefillNotes.empty()) {
        std::snprintf(bugDescBuf_, sizeof bugDescBuf_, "%s", prefillNotes.c_str());
    }
}

void App::renderBugReportModal() {
    if (!showBugReportModal_) return;

    static constexpr const char* kCategories[] = {
        "Card Ability / Rules",
        "Turn / Phase Flow",
        "UI / Modal Interaction",
        "Card Movement / Zones",
        "Multiplayer / Coordinate",
        "Other",
    };
    static constexpr const char* kSeverities[] = {
        "Minor",
        "Normal",
        "High",
        "Critical",
    };

    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowSize(ImVec2(920.f, 680.f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("BUG REPORTER & SAVED ISSUE LOG [B]###BugReportModal", &showBugReportModal_,
                     ImGuiWindowFlags_NoSavedSettings)) {
        const auto& reports = table_->bugReports();
        const int openCount = table_->openBugReportCount();

        // Top Mode Switcher Tabs
        {
            const bool isRecordTab = (bugReportTab_ == 0);
            if (isRecordTab) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.24f, 0.15f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.30f, 0.18f, 1.f));
            }
            if (ImGui::Button("+ Record New Bug Report", ImVec2(240.f, 32.f))) {
                bugReportTab_ = 0;
            }
            if (isRecordTab) ImGui::PopStyleColor(2);

            ImGui::SameLine();

            const bool isHistoryTab = (bugReportTab_ == 1);
            if (isHistoryTab) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.46f, 0.30f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.56f, 0.36f, 1.f));
            }
            char histTabLbl[80];
            std::snprintf(histTabLbl, sizeof histTabLbl,
                          "Saved Bug Reports (%zu total, %d open)", reports.size(), openCount);
            if (ImGui::Button(histTabLbl, ImVec2(290.f, 32.f))) {
                bugReportTab_ = 1;
                if (selectedBugReportId_ < 0 && !reports.empty()) {
                    selectedBugReportId_ = reports.back().id;
                }
            }
            if (isHistoryTab) ImGui::PopStyleColor(2);

            ImGui::SameLine(0.f, 16.f);
            ImGui::TextDisabled("Saved to: %s",
                                table_->bugReportMdPath().filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Markdown: %s\nJSONL: %s",
                                  table_->bugReportMdPath().string().c_str(),
                                  table_->bugReportJsonlPath().string().c_str());
            }
        }

        ImGui::Separator();

        if (bugReportTab_ == 0) {
            // =================================================================
            // TAB 0: RECORD NEW BUG REPORT
            // =================================================================
            ImGui::TextColored(
                ImVec4(0.98f, 0.80f, 0.32f, 1.f),
                "Record a self-contained bug snapshot (automatically attaches board state & recent moves):");

            // Row 1: Category, Severity, Recent Moves Count
            ImGui::SetNextItemWidth(220.f);
            ImGui::Combo("Category", &bugCategoryIdx_, kCategories, IM_ARRAYSIZE(kCategories));
            ImGui::SameLine(0.f, 18.f);
            ImGui::SetNextItemWidth(130.f);
            ImGui::Combo("Severity", &bugSeverityIdx_, kSeverities, IM_ARRAYSIZE(kSeverities));
            ImGui::SameLine(0.f, 18.f);
            ImGui::SetNextItemWidth(150.f);
            ImGui::SliderInt("Recent Moves", &bugRecentMoveCount_, 5, 50, "%d moves");

            // Row 2: Subject Card / Action & Zone
            ImGui::SetNextItemWidth(430.f);
            ImGui::InputTextWithHint("Subject Card / Move",
                                     "e.g. #8 STICK AROUND or PLAY_CARD...", bugSubjectCardBuf_,
                                     sizeof bugSubjectCardBuf_);
            ImGui::SameLine(0.f, 16.f);
            ImGui::SetNextItemWidth(220.f);
            ImGui::InputTextWithHint("Zone", "e.g. p0_hand, wilds_2, combat...", bugSubjectZoneBuf_,
                                     sizeof bugSubjectZoneBuf_);

            // Row 3: Short Bug Summary / Title
            ImGui::SetNextItemWidth(-120.f);
            ImGui::InputTextWithHint(
                "Summary / Title",
                "Short title (optional — auto-filled from Subject Card/Turn if left blank)...",
                bugTitleBuf_, sizeof bugTitleBuf_);

            // Row 4: Multiline Description (What Happened / Expected vs Actual)
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.68f, 1.f),
                               "What Happened / Expected vs Actual Behavior:");
            ImGui::InputTextMultiline(
                "##BugDescInput", bugDescBuf_, sizeof bugDescBuf_, ImVec2(-1.f, 105.f));

            // Row 5: Live Auto-Captured Context Preview (Side-by-Side Panes)
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Auto-Captured Snapshot Preview (included automatically in logs/bug_reports.md):");

            const float previewH = ImGui::GetContentRegionAvail().y - 46.f;
            const float halfW = (ImGui::GetContentRegionAvail().x - 10.f) * 0.5f;

            ImGui::BeginChild("BugStatePreview", ImVec2(halfW, std::max(120.f, previewH)), true);
            ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.65f, 1.f),
                               "Tactical & Board Zones Snapshot:");
            ImGui::Separator();
            const std::string tacSnap = table_->buildTacticalSnapshot();
            const std::string brdSnap = table_->buildBoardSnapshot();
            ImGui::TextUnformatted(tacSnap.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(brdSnap.c_str());
            ImGui::EndChild();

            ImGui::SameLine(0.f, 10.f);

            ImGui::BeginChild("BugMovesPreview", ImVec2(0.f, std::max(120.f, previewH)), true);
            const auto recentLines = table_->formatRecentMoves(bugRecentMoveCount_);
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.98f, 1.f),
                               "Attached Recent Moves (%zu events):", recentLines.size());
            ImGui::Separator();
            if (recentLines.empty()) {
                ImGui::TextDisabled("(No moves in current view)");
            } else {
                for (int i = static_cast<int>(recentLines.size()) - 1; i >= 0; --i) {
                    ImGui::TextWrapped("%s", recentLines[static_cast<size_t>(i)].c_str());
                }
            }
            ImGui::EndChild();

            // Row 6: Submit & Clear Buttons
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.54f, 0.32f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.66f, 0.40f, 1.f));
            if (ImGui::Button("Save Bug Report to logs/bug_reports.md", ImVec2(340.f, 34.f))) {
                const BugReportEntry& saved = table_->submitBugReport(
                    bugTitleBuf_, kCategories[bugCategoryIdx_], kSeverities[bugSeverityIdx_],
                    bugDescBuf_, bugSubjectCardBuf_, bugSubjectZoneBuf_, bugRecentMoveCount_);
                selectedBugReportId_ = saved.id;
                bugTitleBuf_[0] = '\0';
                bugDescBuf_[0] = '\0';
                bugReportTab_ = 1; // Switch to Saved Bug Reports view so user sees their saved report
                showToast("Recorded " + saved.reportCode + " -> " +
                          table_->bugReportMdPath().filename().string());
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ImGui::Button("Clear Fields", ImVec2(130.f, 34.f))) {
                bugTitleBuf_[0] = '\0';
                bugSubjectCardBuf_[0] = '\0';
                bugSubjectZoneBuf_[0] = '\0';
                bugDescBuf_[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Close [Esc]", ImVec2(120.f, 34.f))) {
                showBugReportModal_ = false;
            }
        } else {
            // =================================================================
            // TAB 1: SAVED BUG REPORTS BROWSER (LOOK BACK WITHOUT FULL LOGS)
            // =================================================================
            ImGui::SetNextItemWidth(260.f);
            ImGui::InputTextWithHint("##BugHistFilter",
                                     "Search bug ID, card, title, or notes...", bugHistoryFilter_,
                                     sizeof bugHistoryFilter_);
            ImGui::SameLine(0.f, 14.f);
            if (ImGui::RadioButton("All", bugHistoryStatusFilter_ == 0))
                bugHistoryStatusFilter_ = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("Open Only", bugHistoryStatusFilter_ == 1))
                bugHistoryStatusFilter_ = 1;
            ImGui::SameLine();
            if (ImGui::RadioButton("Resolved Only", bugHistoryStatusFilter_ == 2))
                bugHistoryStatusFilter_ = 2;

            ImGui::Separator();

            const float availH = ImGui::GetContentRegionAvail().y - 8.f;
            // Left Pane: Bug Report List
            ImGui::BeginChild("BugReportListPane", ImVec2(320.f, availH), true);
            if (reports.empty()) {
                ImGui::TextDisabled("No bug reports recorded yet.\nSwitch to '+ Record New Bug Report' to log an issue.");
            } else {
                for (int i = static_cast<int>(reports.size()) - 1; i >= 0; --i) {
                    const BugReportEntry& r = reports[static_cast<size_t>(i)];
                    if (bugHistoryStatusFilter_ == 1 && r.status == "RESOLVED") continue;
                    if (bugHistoryStatusFilter_ == 2 && r.status != "RESOLVED") continue;
                    if (!containsCaseInsensitive(r.reportCode, bugHistoryFilter_) &&
                        !containsCaseInsensitive(r.title, bugHistoryFilter_) &&
                        !containsCaseInsensitive(r.description, bugHistoryFilter_) &&
                        !containsCaseInsensitive(r.subjectCard, bugHistoryFilter_) &&
                        !containsCaseInsensitive(r.category, bugHistoryFilter_)) {
                        continue;
                    }

                    ImGui::PushID(r.id);
                    const bool isSel = (r.id == selectedBugReportId_);
                    if (isSel) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.32f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              ImVec4(0.24f, 0.54f, 0.38f, 1.f));
                    } else if (r.status == "RESOLVED") {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.22f, 0.19f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              ImVec4(0.22f, 0.30f, 0.26f, 1.f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.36f, 0.18f, 0.14f, 0.92f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              ImVec4(0.48f, 0.24f, 0.18f, 1.f));
                    }

                    char itemLbl[240];
                    std::snprintf(itemLbl, sizeof itemLbl, "[%s] [%s] %s\nT%d %s | %s",
                                  r.reportCode.c_str(), r.status.c_str(), r.title.c_str(), r.turn,
                                  r.phase.c_str(), r.timestamp.c_str());
                    if (ImGui::Button(itemLbl, ImVec2(-1.f, 46.f))) {
                        selectedBugReportId_ = r.id;
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            ImGui::SameLine(0.f, 10.f);

            // Right Pane: Selected Bug Report Details
            ImGui::BeginChild("BugReportDetailPane", ImVec2(0.f, availH), true);
            const BugReportEntry* sel = nullptr;
            for (const auto& r : reports) {
                if (r.id == selectedBugReportId_) {
                    sel = &r;
                    break;
                }
            }
            if (!sel && !reports.empty()) {
                sel = &reports.back();
                selectedBugReportId_ = sel->id;
            }

            if (!sel) {
                ImGui::TextDisabled("Select a bug report on the left to inspect its details.");
            } else {
                const bool isResolved = (sel->status == "RESOLVED");
                const ImVec4 statusCol = isResolved ? ImVec4(0.45f, 0.95f, 0.62f, 1.f)
                                                    : ImVec4(0.98f, 0.55f, 0.40f, 1.f);
                ImGui::TextColored(statusCol, "[%s] [%s] %s", sel->reportCode.c_str(),
                                   sel->status.c_str(), sel->title.c_str());

                // Action Bar for Selected Report
                if (ImGui::SmallButton(isResolved ? "Reopen Issue" : "Mark as Resolved")) {
                    const std::string nextStatus = isResolved ? "OPEN" : "RESOLVED";
                    const std::string code = sel->reportCode;
                    table_->updateBugReportStatus(sel->id, nextStatus);
                    showToast(code + " marked as " + nextStatus);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy Markdown to Clipboard")) {
                    const std::string md = table_->formatBugReportMarkdown(*sel);
                    ImGui::SetClipboardText(md.c_str());
                    showToast("Copied " + sel->reportCode + " markdown report to clipboard!");
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.16f, 0.16f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.22f, 0.22f, 1.f));
                if (ImGui::SmallButton("Delete Report")) {
                    const int delId = sel->id;
                    const std::string delCode = sel->reportCode;
                    table_->deleteBugReport(delId);
                    selectedBugReportId_ =
                        table_->bugReports().empty() ? -1 : table_->bugReports().back().id;
                    showToast("Deleted " + delCode);
                    ImGui::PopStyleColor(2);
                    ImGui::EndChild();
                    ImGui::End();
                    return;
                }
                ImGui::PopStyleColor(2);

                ImGui::Separator();
                ImGui::TextDisabled("Timestamp: %s  |  Category: %s  |  Severity: %s",
                                    sel->timestamp.c_str(), sel->category.c_str(),
                                    sel->severity.c_str());
                ImGui::TextDisabled("Scenario: %s  |  Turn %d (%s)", sel->scenarioSummary.c_str(),
                                    sel->turn, sel->phase.c_str());
                if (!sel->subjectCard.empty() || !sel->subjectZone.empty()) {
                    ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.35f, 1.f),
                                       "Subject Card / Move: %s   |   Zone: %s",
                                       sel->subjectCard.empty() ? "N/A" : sel->subjectCard.c_str(),
                                       sel->subjectZone.empty() ? "N/A" : sel->subjectZone.c_str());
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.68f, 1.f),
                                   "User Notes / Expected vs Actual:");
                ImGui::TextWrapped("%s", sel->description.empty()
                                             ? "(No additional user notes provided)"
                                             : sel->description.c_str());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.98f, 1.f),
                                   "Captured Recent Moves (%zu events leading up to bug):",
                                   sel->recentMoves.size());
                ImGui::BeginChild("SavedBugMovesBox", ImVec2(0.f, 145.f), true);
                if (sel->recentMoves.empty()) {
                    ImGui::TextDisabled("(No moves recorded)");
                } else {
                    for (int i = static_cast<int>(sel->recentMoves.size()) - 1; i >= 0; --i) {
                        ImGui::TextWrapped("%s", sel->recentMoves[static_cast<size_t>(i)].c_str());
                    }
                }
                ImGui::EndChild();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.65f, 1.f),
                                   "Captured Tactical & Board Zones Snapshot:");
                ImGui::BeginChild("SavedBugSnapshotBox", ImVec2(0.f, 0.f), true);
                ImGui::TextUnformatted(sel->tacticalSnapshot.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(sel->boardSnapshot.c_str());
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void App::renderStatusBar() {
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(14.f, vp.y - 44.f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        const PileId activeId = (draggedPile_ != kNoPile) ? draggedPile_ : hoveredPile_;
        if (const Pile* p = table_->find(activeId); p && !p->cards.empty()) {
            const CardInst& top = p->top();
            const std::string name = top.faceUp ? catalog_.card(top.def).name : "Face-Down Card";
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.68f, 1.f), "%s", name.c_str());
            if (p->cards.size() >= 2 && p->cards.front().attached) {
                ImGui::SameLine();
                const CardInst& bot = p->cards.front();
                ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.18f, 1.f), "[+%s attached below]",
                                   bot.faceUp ? catalog_.card(bot.def).name.c_str() : "Attached");
            } else if (top.attached) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.18f, 1.f), "[ATTACHED]");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu in pile, zone=%s)  |  Ctrl+Click: Multi-Select  [G] Stack  "
                                "[M] Drag Stack  [B] Report Bug  R-Click: Menu",
                                p->cards.size(), p->zone.empty() ? "loose" : p->zone.c_str());
        } else {
            ImGui::TextDisabled("Ctrl+Click: Multi-Select  |  [G] Stack Selected  |  [M] Toggle Drag Whole Stack  "
                                "|  [L] Move Log  |  [B] Report Bug");
        }

        if (toastTimer_ > 0.f && !toastText_.empty()) {
            ImGui::SameLine(0.f, 24.f);
            ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.35f, 1.f), ">> %s", toastText_.c_str());
        }
    }
    ImGui::End();
}

} // namespace lc
