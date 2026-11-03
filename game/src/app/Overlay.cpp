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

} // namespace

void App::renderOverlay() {
    renderWorldLabels();
    renderTopBar();
    renderTacticalHUD();
    renderMoveLogWindow();
    renderSetupWizardModal();
    renderContextMenu();
    renderInspectModal();
    renderPileBrowserModal();
    renderCatalogModal();
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
                    if (atk > 0) {
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
                    std::snprintf(badge, sizeof badge, "Rescue: 8 ///");
                    if (st.phase == TurnPhase::ActionPhase && st.attackPool >= 8) {
                        borderCol = IM_COL32(74, 222, 128, 245);
                    }
                } else {
                    const int atk = table_->effectiveEnemyAttack(p);
                    if (atk > 0) {
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

        // Active Player Zone Card Counts Summary
        const std::string handZid = table_->activePlayerZone("hand");
        const std::string playedZid = table_->activePlayerZone("played");
        const std::string deckZid = table_->activePlayerZone("deck");
        const std::string discZid = table_->activePlayerZone("discard");

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
        if (isPZone && zoneRole == "hand") {
            if (zoneOwnerP == st.activePlayer) {
                if (ImGui::MenuItem("Play Card", "Double-Click")) {
                    showToast(table_->playCard(p->id, rng_()));
                }
            }
            bool hasCoord = false;
            for (const auto& kw : def.keywords) {
                if (kw == "Coordinate") hasCoord = true;
            }
            if (hasCoord && (st.playerCount == 1 || zoneOwnerP != st.activePlayer)) {
                if (ImGui::MenuItem("Coordinate Card (Lend Virtual Copy & Draw 1)",
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
                (top.def == 102) ? "Wipe Out Guerrilla Camp (1 ///)" : "Fight / Resolve Card";
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
                int ownerP = -1;
                std::string_view role;
                const bool isPZone = parsePlayerZone(ip->zone, ownerP, role);
                if (isPZone && role == "hand") {
                    if (ownerP == st.activePlayer) {
                        if (ImGui::Button("Play Card Now", ImVec2(-1.f, 0.f))) {
                            showToast(table_->playCard(ip->id, rng_()));
                            open = false;
                        }
                    }
                    bool hasCoord = false;
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
                        (def.id == 102) ? "Wipe Out Guerrilla Camp (1 ///)" : "Fight / Resolve Card";
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
                                           "player plays a virtual copy of it; at end of turn it goes to your "
                                           "discard pile.");
                    } else if (kw == "Brothers in Arms") {
                        ImGui::TextWrapped("Activates when you have played 4 or more different Barracks "
                                           "Characters this turn (Starters, Commanders, and Role cards do not "
                                           "count). Triggers retroactively if you reach 4 later in the turn!");
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
                                "[M] Drag Stack  R-Click: Menu",
                                p->cards.size(), p->zone.empty() ? "loose" : p->zone.c_str());
        } else {
            ImGui::TextDisabled("Ctrl+Click: Multi-Select  |  [G] Stack Selected  |  [M] Toggle Drag Whole Stack  "
                                "|  [L] Move Log  |  Drag cards into Player Area zones to update state!");
        }

        if (toastTimer_ > 0.f && !toastText_.empty()) {
            ImGui::SameLine(0.f, 24.f);
            ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.35f, 1.f), ">> %s", toastText_.c_str());
        }
    }
    ImGui::End();
}

} // namespace lc
