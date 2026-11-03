#include "app/App.h"

#include "core/Log.h"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>

namespace lc {

namespace {

Color toneColor(ZoneTone t, float alpha = 1.f) {
    switch (t) {
    case ZoneTone::Wilds:
        return Color::rgb(0x4ade80, alpha);
    case ZoneTone::Danger:
        return Color::rgb(0xf87171, alpha);
    case ZoneTone::HQ:
        return Color::rgb(0x60a5fa, alpha);
    case ZoneTone::Neutral:
    default:
        return Color::rgb(0xfacc15, alpha);
    }
}

void applyTacticalTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.f;
    s.FrameRounding = 5.f;
    s.PopupRounding = 7.f;
    s.GrabRounding = 4.f;
    s.WindowBorderSize = 1.f;
    s.FramePadding = ImVec2(10.f, 6.f);
    s.ItemSpacing = ImVec2(8.f, 6.f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.09f, 0.08f, 0.94f);
    c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.11f, 0.09f, 0.97f);
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.38f, 0.28f, 0.55f);
    c[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.17f, 0.14f, 0.90f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.26f, 0.20f, 0.95f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.34f, 0.25f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.08f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.16f, 0.12f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.13f, 0.24f, 0.18f, 0.92f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.36f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.46f, 0.33f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.15f, 0.29f, 0.21f, 0.85f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.40f, 0.29f, 0.95f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.48f, 0.34f, 1.00f);
}

} // namespace

App::~App() {
    textures_.reset();
    if (glContext_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(glContext_);
    }
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LC_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    window_ = SDL_CreateWindow("Legendary Encounters: Predator — Tabletop", 1600, 960,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        LC_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        // Fallback to GL 3.3 Core if a driver doesn't advertise 4.6
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        glContext_ = SDL_GL_CreateContext(window_);
    }
    if (!glContext_) {
        LC_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        LC_ERROR("Failed to initialize GLAD");
        return false;
    }

    LC_LOG("OpenGL %s | %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    applyTacticalTheme();
    ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    assetDir_ = findAssetDir();
    LC_LOG("Assets directory: %s", assetDir_.string().c_str());

    auto cat = loadCatalog(assetDir_ / "catalog.json");
    auto lay = loadBoardLayout(assetDir_ / "board_layout.json");
    if (!cat || !lay) return false;
    catalog_ = std::move(*cat);
    layout_ = std::move(*lay);

    textures_ = std::make_unique<TextureCache>(assetDir_);
    boardTex_ = textures_->loadNow("board.png");
    backTex_ = textures_->loadNow("card_back.png");
    if (!boardTex_ || !backTex_) return false;

    if (!renderer_.init()) return false;

    setupConfig_.playerAvatars = defaultAvatarsForScenario(1, false);
    setupConfig_.avatarDefId = setupConfig_.playerAvatars[0];

    table_ = std::make_unique<Table>(catalog_, layout_);
    resetSetup();

    int winW = 1600, winH = 960;
    SDL_GetWindowSize(window_, &winW, &winH);
    camera_.setViewport({static_cast<float>(winW), static_cast<float>(winH)});
    camera_.setLimits({-1800.f, -800.f, layout_.boardSize.x + 3600.f, layout_.boardSize.y + 3600.f});
    fitPlayerArea();

    return true;
}

int App::run(int maxFrames) {
    if (maxFrames > 0) showSetupWizard_ = false;
    std::uint64_t prevTicks = SDL_GetTicksNS();
    int frames = 0;

    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            handleEvent(e);
        }

        const std::uint64_t nowTicks = SDL_GetTicksNS();
        const float dt = std::min(0.05f, static_cast<float>(nowTicks - prevTicks) * 1e-9f);
        prevTicks = nowTicks;

        update(dt);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        renderOverlay();
        ImGui::Render();

        int fbW = 0, fbH = 0;
        SDL_GetWindowSizeInPixels(window_, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.045f, 0.068f, 0.055f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        renderWorld();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);

        if (maxFrames > 0) {
            if (frames == 2) {
                LC_LOG("[Smoke] %s", table_->resolveEnemyPhase(rng_()).c_str());
            } else if (frames == 4) {
                // Test dragging a Strike card into active player's Strike Area (two-way zone sync)
                if (Pile* strk = table_->findZonePile("strikes")) {
                    const PileId topStrike = table_->splitTop(strk->id, 1);
                    const std::string strikeZid = table_->activePlayerZone("strikes");
                    DropTarget dropTarget;
                    dropTarget.zone = layout_.zone(strikeZid);
                    dropTarget.kind = DropTarget::Kind::Slot;
                    dropTarget.pos =
                        dropTarget.zone ? dropTarget.zone->rect.center() : Vec2{367.f, 1232.f};
                    table_->applyDropWithLog(topStrike, dropTarget, "strikes");
                    LC_LOG("[Smoke] Dragged Strike into %s -> Strikes=%d, HP=%d/%d",
                           strikeZid.c_str(), table_->state().strikeDamage,
                           table_->state().avatarHealth - table_->state().strikeDamage,
                           table_->state().avatarHealth);
                }
            } else if (frames == 6) {
                LC_LOG("[Smoke] %s", table_->playAllHandCards(rng_()).c_str());
            } else if (frames == 8) {
                if (const Pile* cmd = table_->findZonePile("commanders")) {
                    LC_LOG("[Smoke] %s", table_->recruitCard(cmd->id, rng_()).c_str());
                }
            } else if (frames == 10) {
                LC_LOG("[Smoke] %s", table_->resolveStrikePhase(rng_()).c_str());
            } else if (frames == 12) {
                LC_LOG("[Smoke] %s", table_->resolveCleanupPhase(rng_()).c_str());
            }
            if (++frames >= maxFrames) {
                LC_LOG("Smoke test completed (%d frames rendered, %zu move log entries)", frames,
                       table_->state().moveLog.size());
                break;
            }
        }
    }

    return 0;
}

void App::update(float dt) {
    textures_->pump(8);

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window_, &winW, &winH);
    camera_.setViewport({static_cast<float>(winW), static_cast<float>(winH)});
    camera_.update(dt);

    mouseWorld_ = camera_.screenToWorld(mouseScreen_);
    if (draggedPile_ != kNoPile) {
        updateDrag();
    } else {
        updateHover();
    }

    table_->updateVisuals(dt, draggedPile_);

    if (toastTimer_ > 0.f) {
        toastTimer_ = std::max(0.f, toastTimer_ - dt);
    }
}

void App::renderWorld() {
    const float unitsPerPx = 1.f / std::max(0.05f, camera_.zoom());
    renderer_.begin(camera_.viewProj(), unitsPerPx);

    const Vec2 bSize = layout_.boardSize;
    const Vec2 bCenter = bSize * 0.5f;
    const TableState& st = table_->state();

    // 1. Separate 7-Zone Player Board Areas for all 1..5 players
    for (const auto& tray : layout_.playerTrays) {
        const int pIdx = tray.playerIndex;
        const bool isActive = (pIdx == st.activePlayer);
        const bool isAlive = (pIdx < static_cast<int>(st.players.size()))
                                 ? st.players[static_cast<size_t>(pIdx)].alive
                                 : true;

        Color fillCol = Color::rgb(0x0f1c16, 0.85f);
        Color borderCol = Color::rgb(0x2c4c3b, 0.65f);
        float borderThick = 2.2f;

        if (!isAlive) {
            fillCol = Color::rgb(0x1f0d0f, 0.84f);
            borderCol = Color::rgb(0x991b1b, 0.72f);
        } else if (isActive) {
            fillCol = Color::rgb(0x11241b, 0.90f);
            borderCol = (st.playerCount > 1) ? Color::rgb(0x4ade80, 0.92f)
                                             : Color::rgb(0x3a6b50, 0.80f);
            borderThick = (st.playerCount > 1) ? 3.8f : 2.5f;
        }

        renderer_.shadow(tray.rect.center() + Vec2{0.f, 12.f}, tray.rect.size(), 0.f, 20.f, 24.f,
                         {0.f, 0.f, 0.f, 0.55f});
        renderer_.fill(tray.rect.center(), tray.rect.size(), fillCol, 18.f);
        renderer_.outline(tray.rect.center(), tray.rect.size(), 0.f, 18.f, borderThick, borderCol);
    }

    // Draw crisp tactical pads for every player's 7 zones
    for (const auto& z : layout_.zones) {
        if (isPlayerZoneId(z.id)) {
            const Color col = toneColor(z.tone, 0.55f);
            renderer_.fill(z.rect.center(), z.rect.size(), Color::rgb(0x08110d, 0.72f), 10.f);
            renderer_.outline(z.rect.center(), z.rect.size(), 0.f, 10.f, 1.8f, col);
        }
    }

    // 2. Full stitched Game Board Mat (Printed Pages 3 & 4)
    renderer_.shadow(bCenter + Vec2{0.f, 18.f}, bSize, 0.f, 26.f, 34.f, {0.f, 0.f, 0.f, 0.72f});
    renderer_.draw({bCenter, bSize, 0.f, 1.f, boardTex_.id, {1, 1, 1, 1}, 24.f});
    renderer_.outline(bCenter, bSize, 0.f, 24.f, 3.f, Color::rgb(0x3a5a40, 0.55f));

    // Cover baked-in rulebook setup diagram cards on board.png so empty HQ/deck slots look clean
    for (const auto& z : layout_.zones) {
        if (z.id == "objectives" || z.id == "location" || z.id == "commanders" ||
            z.id.rfind("hq_", 0) == 0 || z.id == "prey" || z.id == "strikes" ||
            z.id == "barracks") {
            renderer_.fill(z.rect.center(), z.rect.size(), Color::rgb(0x0d1812, 0.92f), 10.f);
            renderer_.outline(z.rect.center(), z.rect.size(), 0.f, 10.f, 1.8f,
                              Color::rgb(0x2c4c3b, 0.65f));
        }
    }

    // 3. Zone highlights (when toggled on, or subtly while dragging a card)
    if (showZones_ || draggedPile_ != kNoPile) {
        const float alpha = showZones_ ? 0.55f : 0.25f;
        for (const auto& z : layout_.zones) {
            const Color col = toneColor(z.tone, alpha);
            if (showZones_) {
                renderer_.fill(z.rect.center(), z.rect.size(), col.withAlpha(0.10f), 10.f);
            }
            renderer_.outline(z.rect.center(), z.rect.size(), 0.f, 10.f, 2.5f, col);
        }
    }

    // 4. Resting piles in z-order
    auto isHeldPile = [&](PileId id) {
        if (id == draggedPile_) return true;
        return std::find(draggedMultiPiles_.begin(), draggedMultiPiles_.end(), id) !=
               draggedMultiPiles_.end();
    };

    for (const auto& p : st.piles) {
        if (isHeldPile(p.id)) continue;
        renderPile(p, false);
    }

    // 5. Active drop target glow while dragging
    if (draggedPile_ != kNoPile && currentDrop_.kind != DropTarget::Kind::Loose) {
        const Rect hi = currentDrop_.highlight;
        const Color glow = (currentDrop_.kind == DropTarget::Kind::Pile)
                               ? Color::rgb(0x38bdf8, 0.90f)
                               : Color::rgb(0x4ade80, 0.90f);
        renderer_.fill(hi.center(), hi.size(), glow.withAlpha(0.16f), 12.f);
        renderer_.outline(hi.center(), hi.size(), 0.f, 12.f, 4.f, glow, 3.f);
    }

    // 6. Held / dragged pile(s) rendered on top with lift & deep shadow
    if (!draggedMultiPiles_.empty()) {
        for (PileId pid : draggedMultiPiles_) {
            if (const Pile* held = table_->find(pid)) {
                renderPile(*held, true);
            }
        }
    } else if (const Pile* held = table_->find(draggedPile_)) {
        renderPile(*held, true);
    }

    renderer_.end();
}

void App::renderPile(const Pile& p, bool isHeld) {
    if (p.cards.empty()) return;

    const PileVisual& pv = table_->visual(p.id);
    const CardInst& top = p.top();
    const Vec2 baseSize = table_->cardSize(top.def);
    const float liftScale = 1.f + pv.lift * 0.065f;
    const Vec2 size = baseSize * liftScale;
    const float radius = 10.f * liftScale;

    // Rulebook p.16: When a location has an attached card (e.g. Guerrilla Camp / Defensive Position)
    // at the bottom and another card on top of it, render the attached bottom card slid upward
    // underneath the top card so players can clearly see the 2-card location stack!
    const bool hasBottomAttached = (!isHeld && p.cards.size() >= 2 && p.cards.front().attached);
    if (hasBottomAttached) {
        const CardInst& bottom = p.cards.front();
        const Vec2 attSize = table_->cardSize(bottom.def);
        const Vec2 attPos = pv.pos + Vec2{0.f, -48.f};
        renderer_.shadow(attPos + Vec2{0.f, 5.f}, attSize, pv.angle, 10.f, 8.f,
                         {0.f, 0.f, 0.f, 0.50f});
        GLuint attTex = backTex_.id;
        Color attTint{1.f, 1.f, 1.f, 1.f};
        if (bottom.faceUp) {
            Texture front = textures_->request(catalog_.card(bottom.def).texture);
            if (front) {
                attTex = front.id;
            } else {
                attTint = {0.45f, 0.40f, 0.25f, 1.f};
            }
        }
        renderer_.draw({attPos, attSize, pv.angle, 1.f, attTex, attTint, 10.f});
        renderer_.outline(attPos, attSize, pv.angle, 10.f, 2.4f, Color::rgb(0xf59e0b, 0.92f));
    }

    // Shadow depth increases smoothly when picked up
    const Vec2 shadowOff = {pv.lift * 10.f, 5.f + pv.lift * 22.f};
    const float shadowBlur = 8.f + pv.lift * 18.f;
    renderer_.shadow(pv.pos + shadowOff, size, pv.angle, radius, shadowBlur,
                     {0.f, 0.f, 0.f, 0.52f + pv.lift * 0.18f});

    // Multi-card deck thickness layers beneath the top card (skip for 2-card attached Wilds stack)
    if (!hasBottomAttached) {
        const int layers = std::min(4, (static_cast<int>(p.cards.size()) - 1 + 2) / 3);
        for (int i = layers; i >= 1; --i) {
            const Vec2 layerOff{-static_cast<float>(i) * 1.8f, static_cast<float>(i) * 2.2f};
            renderer_.draw({pv.pos + layerOff, size, pv.angle, 1.f, backTex_.id,
                            {0.78f, 0.82f, 0.86f, 1.f}, radius});
        }
    }

    // Flip animation squashes X through 0 at midpoint
    const float flipCos = std::cos((1.f - pv.flipT) * kPi);
    const float scaleX = std::max(0.04f, std::abs(flipCos));

    GLuint texId = backTex_.id;
    Color tint{1, 1, 1, 1};
    if (top.faceUp) {
        Texture front = textures_->request(catalog_.card(top.def).texture);
        if (front) {
            texId = front.id;
        } else {
            tint = {0.35f, 0.45f, 0.40f, 1.f};
        }
    }

    const Vec2 topPos = pv.pos + pv.topOffset;
    renderer_.draw({topPos, size, pv.angle, scaleX, texId, tint, radius});

    // Subtle dark border on every card; amber outline on attached cards; bright highlight when selected/hovered/held
    if (isHeld) {
        renderer_.outline(topPos, size, pv.angle, radius, 3.0f, Color::rgb(0xfef08a, 0.95f));
    } else if (isSelected(p.id)) {
        renderer_.outline(topPos, size, pv.angle, radius, 3.4f, Color::rgb(0x38bdf8, 0.98f));
    } else if (p.id == hoveredPile_) {
        renderer_.outline(topPos, size, pv.angle, radius, 2.8f, Color::rgb(0x4ade80, 0.90f));
    } else if (top.attached) {
        renderer_.outline(topPos, size, pv.angle, radius, 2.5f, Color::rgb(0xf59e0b, 0.92f));
    } else {
        renderer_.outline(topPos, size, pv.angle, radius, 1.2f, {0.f, 0.f, 0.f, 0.55f});
    }
}

} // namespace lc
