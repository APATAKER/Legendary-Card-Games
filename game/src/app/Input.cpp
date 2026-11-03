#include "app/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace lc {

void App::showToast(std::string msg) {
    toastText_ = std::move(msg);
    toastTimer_ = 2.8f;
}

bool App::isSelected(PileId id) const {
    if (id == kNoPile) return false;
    return std::find(selectedPiles_.begin(), selectedPiles_.end(), id) != selectedPiles_.end();
}

void App::toggleSelect(PileId id) {
    if (id == kNoPile) return;
    auto it = std::find(selectedPiles_.begin(), selectedPiles_.end(), id);
    if (it != selectedPiles_.end()) {
        selectedPiles_.erase(it);
    } else {
        selectedPiles_.push_back(id);
    }
    showToast("Selected " + std::to_string(selectedPiles_.size()) +
              " pile(s) — Drag any selected card to move together, or press [G] to stack");
}

void App::stackSelectedPiles() {
    if (selectedPiles_.size() < 2) {
        showToast("Ctrl+Click at least 2 cards/piles to stack them together with [G].");
        return;
    }
    table_->checkpoint();
    Vec2 targetPos = mouseWorld_;
    if (const Pile* last = table_->find(selectedPiles_.back())) {
        targetPos = last->pos;
    }
    const PileId merged = table_->gatherPiles(selectedPiles_, targetPos);
    selectedPiles_.clear();
    if (merged != kNoPile) {
        selectedPiles_.push_back(merged);
        if (const Pile* mp = table_->find(merged)) {
            showToast("Stacked into 1 pile (" + std::to_string(mp->cards.size()) + " cards)");
        }
    }
}

Rect App::allPlayerTraysBounds() const {
    if (layout_.playerTrays.empty()) {
        return {0.f, layout_.boardSize.y, layout_.boardSize.x, 650.f};
    }
    float minX = layout_.playerTrays[0].rect.x;
    float minY = layout_.playerTrays[0].rect.y - 45.f;
    float maxX = layout_.playerTrays[0].rect.right();
    float maxY = layout_.playerTrays[0].rect.bottom();
    for (const auto& t : layout_.playerTrays) {
        minX = std::min(minX, t.rect.x);
        minY = std::min(minY, t.rect.y - 45.f);
        maxX = std::max(maxX, t.rect.right());
        maxY = std::max(maxY, t.rect.bottom());
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

void App::fitBoard(bool instant) {
    camera_.fit({0.f, 0.f, layout_.boardSize.x, layout_.boardSize.y}, 36.f, instant);
}

void App::fitPlayerArea() {
    const Rect trays = allPlayerTraysBounds();
    const float minX = std::min(0.f, trays.x);
    const float minY = 0.f;
    const float maxX = std::max(layout_.boardSize.x, trays.right());
    const float maxY = std::max(layout_.boardSize.y + 620.f, trays.bottom());
    camera_.fit({minX, minY, maxX - minX, maxY - minY}, 24.f, false);
}

void App::fitBoxShelf() {
    camera_.fit(allPlayerTraysBounds(), 28.f, false);
}

void App::fitPlayer(int playerIdx) {
    for (const auto& t : layout_.playerTrays) {
        if (t.playerIndex == playerIdx) {
            Rect r = t.rect;
            r.y -= 45.f;
            r.h += 55.f;
            camera_.fit(r, 26.f, false);
            return;
        }
    }
    fitBoxShelf();
}

void App::resetSetup() {
    const std::uint32_t seed = rng_();
    playerCount_ = std::clamp(playerCount_, 1, 5);
    setupConfig_.playerCount = playerCount_;
    setupConfig_.playerAvatars = resolveUniquePlayerAvatars(setupConfig_);
    if (!setupConfig_.playerAvatars.empty()) {
        setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];
    }
    wizardEditingPlayer_ = std::clamp(wizardEditingPlayer_, 0, playerCount_ - 1);

    configurePlayerZones(layout_, setupConfig_.playerCount);
    camera_.setLimits({-1800.f, -800.f, layout_.boardSize.x + 3600.f, layout_.boardSize.y + 3600.f});

    table_->reset(buildGameSetup(catalog_, layout_, seed, setupConfig_));
    hoveredPile_ = kNoPile;
    draggedPile_ = kNoPile;
    pressedPile_ = kNoPile;
    browsePile_ = kNoPile;
    inspectCardDef_ = -1;
    inspectPile_ = kNoPile;
    selectedPiles_.clear();
    draggedMultiPiles_.clear();
    draggedMultiOffsets_.clear();
    draggedSrcZones_.clear();
    showToast(table_->state().lastActionLog);
}

void App::updateHover() {
    if (ImGui::GetIO().WantCaptureMouse) {
        hoveredPile_ = kNoPile;
        return;
    }
    hoveredPile_ = table_->pileAt(mouseWorld_);
}

void App::beginDrag(PileId hitPile, bool wholePile) {
    const Pile* src = table_->find(hitPile);
    if (!src || src->cards.empty()) return;

    table_->checkpoint();
    draggedMultiPiles_.clear();
    draggedMultiOffsets_.clear();
    draggedSrcZones_.clear();

    // Multi-selection drag: if hitPile is in selectedPiles_ and >1 pile is selected, move all together
    if (isSelected(hitPile) && selectedPiles_.size() > 1) {
        draggedPile_ = hitPile;
        grabOffsetWorld_ = src->pos - mouseWorld_;
        for (PileId pid : selectedPiles_) {
            if (Pile* sp = table_->find(pid); sp && !sp->cards.empty()) {
                draggedMultiPiles_.push_back(pid);
                draggedMultiOffsets_.push_back(sp->pos - mouseWorld_);
                draggedSrcZones_.push_back(sp->zone);
                sp->zone.clear();
                table_->bringToFront(pid);
            }
        }
        updateDrag();
        return;
    }

    const std::string originZone = src->zone;
    const Vec2 originPos = src->pos;
    grabOffsetWorld_ = originPos - mouseWorld_;

    const bool dragAll = wholePile || moveWholeStackMode_;
    const int count = dragAll ? static_cast<int>(src->cards.size()) : 1;
    draggedPile_ = table_->splitTop(hitPile, count);
    if (draggedPile_ != kNoPile) {
        draggedSrcZones_.push_back(originZone);
        table_->bringToFront(draggedPile_);
        updateDrag();
    }
}

void App::updateDrag() {
    const SDL_Keymod mods = SDL_GetModState();
    const bool bypassSnap = (mods & SDL_KMOD_ALT) != 0;
    const bool freePlace = !snapToSlots_ || bypassSnap;

    if (!draggedMultiPiles_.empty()) {
        for (size_t i = 0; i < draggedMultiPiles_.size(); ++i) {
            if (Pile* mp = table_->find(draggedMultiPiles_[i])) {
                mp->pos = mouseWorld_ + draggedMultiOffsets_[i];
            }
        }
        if (table_->find(draggedPile_)) {
            currentDrop_ = table_->resolveDrop(draggedPile_, freePlace);
        }
        return;
    }

    Pile* held = table_->find(draggedPile_);
    if (!held) {
        draggedPile_ = kNoPile;
        return;
    }
    held->pos = mouseWorld_ + grabOffsetWorld_;
    currentDrop_ = table_->resolveDrop(draggedPile_, freePlace);
}

void App::endDrag() {
    if (draggedPile_ == kNoPile) return;

    if (!draggedMultiPiles_.empty()) {
        table_->applyMultiDrop(draggedMultiPiles_, currentDrop_, draggedSrcZones_);
        selectedPiles_.clear();
        draggedMultiPiles_.clear();
        draggedMultiOffsets_.clear();
        draggedSrcZones_.clear();
    } else {
        const std::string srcZone = draggedSrcZones_.empty() ? "" : draggedSrcZones_[0];
        table_->applyDropWithLog(draggedPile_, currentDrop_, srcZone);
        draggedSrcZones_.clear();
    }

    hoveredPile_ = table_->pileAt(mouseWorld_);
    draggedPile_ = kNoPile;
    currentDrop_ = {};
}

void App::handleEvent(const SDL_Event& e) {
    const ImGuiIO& io = ImGui::GetIO();

    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        const Vec2 newPos{e.motion.x, e.motion.y};
        const Vec2 delta = newPos - mouseScreen_;
        mouseScreen_ = newPos;
        mouseWorld_ = camera_.screenToWorld(mouseScreen_);

        if (panning_) {
            camera_.panBy(delta);
        } else if (pressedPile_ != kNoPile && draggedPile_ == kNoPile) {
            if ((mouseScreen_ - pressScreen_).length() >= 5.f) {
                beginDrag(pressedPile_, pressWholePile_);
                pressedPile_ = kNoPile;
            }
        } else if (draggedPile_ != kNoPile) {
            updateDrag();
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        mouseScreen_ = {e.button.x, e.button.y};
        mouseWorld_ = camera_.screenToWorld(mouseScreen_);
        if (io.WantCaptureMouse) break;

        if (e.button.button == SDL_BUTTON_MIDDLE ||
            (e.button.button == SDL_BUTTON_LEFT && spaceDown_)) {
            panning_ = true;
            break;
        }

        if (e.button.button == SDL_BUTTON_LEFT) {
            const PileId hit = table_->pileAt(mouseWorld_);
            const SDL_Keymod mods = SDL_GetModState();
            const bool ctrlDown = (mods & SDL_KMOD_CTRL) != 0;

            if (hit != kNoPile) {
                if (ctrlDown) {
                    toggleSelect(hit);
                    pressedPile_ = kNoPile;
                } else if (e.button.clicks >= 2) {
                    pressedPile_ = kNoPile;
                    const Pile* p = table_->find(hit);
                    if (p && !p->cards.empty()) {
                        const TurnPhase ph = table_->state().phase;
                        const Vec2 pSz = table_->pileSize(*p);
                        int zoneOwnerP = -1;
                        std::string_view zoneRole;
                        const bool isPZone = parsePlayerZone(p->zone, zoneOwnerP, zoneRole);

                        // Double-clicking the exposed top strip of an attached bottom card (e.g. Guerrilla Camp) inspects it!
                        if (p->cards.size() >= 2 && p->cards.front().attached &&
                            p->cards.front().faceUp &&
                            mouseWorld_.y < (p->pos.y - pSz.y * 0.5f)) {
                            inspectCardDef_ = p->cards.front().def;
                            inspectPile_ = hit;
                        } else if (ph == TurnPhase::EnemyPhase && p->zone == "prey") {
                            showToast(table_->resolveEnemyPhase(rng_()));
                        } else if (ph == TurnPhase::ActionPhase && isPZone &&
                                   zoneRole == "hand") {
                            showToast(table_->playCard(hit, rng_()));
                        } else if (ph == TurnPhase::ActionPhase &&
                                   (p->zone == "commanders" || p->zone.rfind("hq_", 0) == 0)) {
                            const int defId = p->top().def;
                            const int prevRecruit = table_->state().recruitPool;
                            const std::string msg = table_->recruitCard(hit, rng_());
                            if (table_->state().recruitPool == prevRecruit) {
                                inspectCardDef_ = defId;
                                inspectPile_ = hit;
                            }
                            showToast(msg);
                        } else if (ph == TurnPhase::ActionPhase &&
                                   p->zone.rfind("wilds_", 0) == 0 && !p->top().faceUp) {
                            showToast(table_->scanWilds(hit, rng_()));
                        } else if (ph == TurnPhase::ActionPhase &&
                                   (p->zone.rfind("wilds_", 0) == 0 || p->zone == "combat") &&
                                   p->top().faceUp) {
                            const int defId = p->top().def;
                            const std::uint32_t prevTopUid = p->top().uid;
                            const std::string msg = table_->fightEnemy(hit, rng_());
                            const Pile* after = table_->find(hit);
                            if (after && !after->cards.empty() && after->top().uid == prevTopUid) {
                                inspectCardDef_ = defId;
                                inspectPile_ = hit;
                            }
                            showToast(msg);
                        } else if (p->top().faceUp) {
                            inspectCardDef_ = p->top().def;
                            inspectPile_ = hit;
                        } else {
                            table_->checkpoint();
                            table_->flipTop(hit);
                            if (const Pile* fp = table_->find(hit); fp && !fp->cards.empty()) {
                                table_->recordMove("FLIP_CARD", catalog_.card(fp->top().def).name, 1,
                                                   fp->zone, fp->zone,
                                                   static_cast<int>(fp->cards.size()),
                                                   fp->top().faceUp, table_->state().lastActionLog);
                            }
                            showToast(table_->state().lastActionLog);
                        }
                    }
                } else {
                    if (!isSelected(hit)) {
                        selectedPiles_.clear();
                    }
                    pressedPile_ = hit;
                    pressScreen_ = mouseScreen_;
                    pressWholePile_ = (mods & SDL_KMOD_SHIFT) != 0;
                }
            } else {
                if (!ctrlDown) selectedPiles_.clear();
                panning_ = true;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            const PileId hit = table_->pileAt(mouseWorld_);
            if (hit != kNoPile) {
                contextPile_ = hit;
                openContextMenu_ = true;
            } else {
                panning_ = true;
            }
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (draggedPile_ != kNoPile) {
                endDrag();
            } else if (pressedPile_ != kNoPile) {
                table_->bringToFront(pressedPile_);
                pressedPile_ = kNoPile;
            }
            panning_ = false;
        } else if (e.button.button == SDL_BUTTON_MIDDLE || e.button.button == SDL_BUTTON_RIGHT) {
            panning_ = false;
        }
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        if (io.WantCaptureMouse) break;
        if (e.wheel.y != 0.f) {
            const float factor = std::pow(1.16f, e.wheel.y);
            camera_.zoomAt(mouseScreen_, factor);
        }
        break;
    }

    case SDL_EVENT_KEY_DOWN: {
        if (e.key.key == SDLK_SPACE) spaceDown_ = true;
        if (io.WantCaptureKeyboard) break;

        const SDL_Keymod mods = e.key.mod;
        const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
        const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
        const bool alt = (mods & SDL_KMOD_ALT) != 0;
        const PileId target = (draggedPile_ != kNoPile) ? draggedPile_ : hoveredPile_;

        switch (e.key.key) {
        case SDLK_ESCAPE:
            if (inspectCardDef_ >= 0) {
                inspectCardDef_ = -1;
                inspectPile_ = kNoPile;
            } else if (browsePile_ != kNoPile) {
                browsePile_ = kNoPile;
            } else if (showCatalogModal_) {
                showCatalogModal_ = false;
            } else if (showSetupWizard_) {
                showSetupWizard_ = false;
            } else if (!selectedPiles_.empty()) {
                selectedPiles_.clear();
                showToast("Cleared selection");
            }
            break;

        case SDLK_G:
            stackSelectedPiles();
            break;

        case SDLK_M:
            moveWholeStackMode_ = !moveWholeStackMode_;
            showToast(std::string("Drag Whole Stack Mode: ") +
                      (moveWholeStackMode_ ? "ON (L-Drag moves entire stack)"
                                           : "OFF (L-Drag pulls top 1 card)"));
            break;

        case SDLK_L:
            showMoveLog_ = !showMoveLog_;
            break;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_N:
            table_->stepPhase(rng_());
            showToast(table_->state().lastActionLog);
            break;

        case SDLK_A:
            if (table_->state().phase == TurnPhase::ActionPhase) {
                table_->playAllHandCards(rng_());
                showToast(table_->state().lastActionLog);
            }
            break;

        case SDLK_HOME:
        case SDLK_0:
            fitBoard(false);
            break;
        case SDLK_1:
            if (alt)
                fitPlayer(0);
            else
                fitPlayerArea();
            break;
        case SDLK_2:
            if (alt && table_->state().playerCount >= 2)
                fitPlayer(1);
            else
                fitBoxShelf();
            break;
        case SDLK_3:
            if (table_->state().playerCount >= 3) fitPlayer(2);
            break;
        case SDLK_4:
            if (table_->state().playerCount >= 4) fitPlayer(3);
            break;
        case SDLK_5:
            if (table_->state().playerCount >= 5) fitPlayer(4);
            break;

        case SDLK_F2:
            showZones_ = !showZones_;
            break;

        case SDLK_F:
            if (target != kNoPile) {
                table_->checkpoint();
                if (shift) {
                    table_->flipPile(target);
                    if (const Pile* fp = table_->find(target); fp && !fp->cards.empty()) {
                        table_->recordMove("FLIP_PILE", catalog_.card(fp->top().def).name,
                                           static_cast<int>(fp->cards.size()), fp->zone, fp->zone,
                                           static_cast<int>(fp->cards.size()), fp->top().faceUp,
                                           "Flipped entire pile");
                    }
                    showToast("Flipped entire pile");
                } else {
                    table_->flipTop(target);
                    if (const Pile* fp = table_->find(target); fp && !fp->cards.empty()) {
                        table_->recordMove("FLIP_CARD", catalog_.card(fp->top().def).name, 1,
                                           fp->zone, fp->zone, static_cast<int>(fp->cards.size()),
                                           fp->top().faceUp, table_->state().lastActionLog);
                    }
                    showToast(table_->state().lastActionLog);
                }
            }
            break;

        case SDLK_R:
            if (target != kNoPile) {
                table_->checkpoint();
                table_->rotate(target);
            }
            break;

        case SDLK_S:
            if (target != kNoPile) {
                if (const Pile* p = table_->find(target); p && p->cards.size() > 1) {
                    table_->checkpoint();
                    table_->shuffle(target, rng_());
                    table_->recordMove("SHUFFLE_PILE", p->label.empty() ? p->zone : p->label,
                                       static_cast<int>(p->cards.size()), p->zone, p->zone,
                                       static_cast<int>(p->cards.size()), p->top().faceUp,
                                       "Shuffled pile");
                    showToast("Shuffled pile (" + std::to_string(p->cards.size()) + " cards)");
                }
            }
            break;

        case SDLK_I:
            if (const Pile* p = table_->find(target); p && !p->cards.empty()) {
                if (p->top().faceUp) {
                    inspectCardDef_ = p->top().def;
                    inspectPile_ = target;
                } else if (p->cards.size() > 1) {
                    browsePile_ = target;
                }
            }
            break;

        case SDLK_Z:
            if (ctrl) {
                if (shift ? table_->redo() : table_->undo()) {
                    showToast(shift ? "Redo" : "Undo");
                }
            }
            break;

        case SDLK_Y:
            if (ctrl && table_->redo()) {
                showToast("Redo");
            }
            break;

        default:
            break;
        }
        break;
    }

    case SDL_EVENT_KEY_UP:
        if (e.key.key == SDLK_SPACE) spaceDown_ = false;
        break;

    default:
        break;
    }
}

} // namespace lc
