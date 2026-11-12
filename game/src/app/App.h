#pragma once
#include "core/Math.h"
#include "game/Assets.h"
#include "game/Camera.h"
#include "game/Table.h"
#include "gfx/Renderer2D.h"
#include "gfx/TextureCache.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <random>
#include <string>

namespace lc {

class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    int run(int maxFrames = 0);

private:
    // Input.cpp
    void handleEvent(const SDL_Event& e);
    void updateHover();
    void beginDrag(PileId hitPile, bool wholePile);
    void updateDrag();
    void endDrag();
    bool isSelected(PileId id) const;
    void toggleSelect(PileId id);
    void stackSelectedPiles();
    Rect allPlayerTraysBounds() const;
    void fitBoard(bool instant = false);
    void fitPlayerArea();
    void fitBoxShelf();
    void fitPlayer(int playerIdx);
    void resetSetup();
    void showToast(std::string msg);

    // App.cpp
    void update(float dt);
    void renderWorld();
    void renderPile(const Pile& p, bool isHeld);

    // Overlay.cpp
    void renderOverlay();
    void renderWorldLabels();
    void renderTopBar();
    void renderTacticalHUD();
    void renderMoveLogWindow();
    void renderSetupWizardModal();
    void renderSacrificeModal();
    void renderAbilityModals();
    void renderContextMenu();
    void renderInspectModal();
    void renderPileBrowserModal();
    void renderCatalogModal();
    void openBugReportModal(PileId focusPile = kNoPile, int focusCardDef = -1,
                            const std::string& focusSubject = "",
                            const std::string& focusZone = "",
                            const std::string& prefillNotes = "");
    void renderBugReportModal();
    void renderStatusBar();

    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool running_ = true;

    std::filesystem::path assetDir_;
    Catalog catalog_;
    BoardLayout layout_;
    std::unique_ptr<TextureCache> textures_;
    Texture boardTex_;
    Texture backTex_;
    Renderer2D renderer_;
    std::unique_ptr<Table> table_;
    Camera camera_;
    std::mt19937 rng_{std::random_device{}()};

    // Input & interaction state
    Vec2 mouseScreen_{0, 0};
    Vec2 mouseWorld_{0, 0};
    bool panning_ = false;
    bool spaceDown_ = false;

    PileId hoveredPile_ = kNoPile;
    PileId pressedPile_ = kNoPile;
    Vec2 pressScreen_{0, 0};
    Vec2 grabOffsetWorld_{0, 0};
    bool pressWholePile_ = false;

    PileId draggedPile_ = kNoPile;
    std::vector<PileId> selectedPiles_;
    std::vector<PileId> draggedMultiPiles_;
    std::vector<Vec2> draggedMultiOffsets_;
    std::vector<std::string> draggedSrcZones_;
    DropTarget currentDrop_{};

    // UI / Overlay state
    GameSetupConfig setupConfig_{};
    int playerCount_ = 1;
    int wizardEditingPlayer_ = 0;
    bool snapToSlots_ = true;
    bool moveWholeStackMode_ = false;
    bool showZones_ = false;
    bool showMoveLog_ = true;
    bool showSetupWizard_ = true;
    bool openContextMenu_ = false;
    PileId contextPile_ = kNoPile;
    int inspectCardDef_ = -1;
    PileId inspectPile_ = kNoPile;
    PileId browsePile_ = kNoPile;
    bool showCatalogModal_ = false;
    char catalogFilter_[128] = {};
    char moveLogFilter_[128] = {};

    // Bug Report & Issue Tracker Modal state
    bool showBugReportModal_ = false;
    int bugReportTab_ = 0;      // 0 = Record New Bug, 1 = Saved Bug Reports
    int bugCategoryIdx_ = 0;    // Index into bug categories
    int bugSeverityIdx_ = 1;    // 0 = Minor, 1 = Normal, 2 = High, 3 = Critical
    int bugRecentMoveCount_ = 15;
    char bugTitleBuf_[160] = {};
    char bugSubjectCardBuf_[160] = {};
    char bugSubjectZoneBuf_[96] = {};
    char bugDescBuf_[2048] = {};
    char bugHistoryFilter_[128] = {};
    int bugHistoryStatusFilter_ = 0; // 0 = All, 1 = Open Only, 2 = Resolved Only
    int selectedBugReportId_ = -1;

    std::string toastText_;
    float toastTimer_ = 0.f;
};

} // namespace lc
