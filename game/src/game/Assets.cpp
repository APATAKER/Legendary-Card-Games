#include "game/Assets.h"

#include "core/Log.h"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace lc {

using nlohmann::json;

namespace {

std::optional<json> readJson(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f) {
        LC_ERROR("cannot open %s", file.string().c_str());
        return std::nullopt;
    }
    try {
        return json::parse(f);
    } catch (const std::exception& e) {
        LC_ERROR("bad JSON in %s: %s", file.string().c_str(), e.what());
        return std::nullopt;
    }
}

ZoneTone parseTone(const std::string& s) {
    if (s == "wilds") return ZoneTone::Wilds;
    if (s == "danger") return ZoneTone::Danger;
    if (s == "hq") return ZoneTone::HQ;
    return ZoneTone::Neutral;
}

bool isKnownPlayerRole(std::string_view role) {
    return role == "avatar" || role == "strikes" || role == "marks" || role == "played" ||
           role == "hand" || role == "deck" || role == "discard";
}

} // namespace

std::string playerZoneId(int playerIdx, std::string_view role) {
    return "p" + std::to_string(std::clamp(playerIdx, 0, 4)) + "_" + std::string(role);
}

bool parsePlayerZone(std::string_view zoneId, int& outPlayerIdx, std::string_view& outRole) {
    if (zoneId.size() > 3 && zoneId[0] == 'p' && zoneId[1] >= '0' && zoneId[1] <= '4' &&
        zoneId[2] == '_') {
        const std::string_view role = zoneId.substr(3);
        if (isKnownPlayerRole(role)) {
            outPlayerIdx = static_cast<int>(zoneId[1] - '0');
            outRole = role;
            return true;
        }
    }
    if (zoneId.rfind("player_", 0) == 0) {
        const std::string_view role = zoneId.substr(7);
        if (isKnownPlayerRole(role)) {
            outPlayerIdx = 0;
            outRole = role;
            return true;
        }
    }
    return false;
}

bool isPlayerZoneId(std::string_view zoneId) {
    int p = 0;
    std::string_view role;
    return parsePlayerZone(zoneId, p, role);
}

void configurePlayerZones(BoardLayout& layout, int playerCount) {
    const int nPlayers = std::clamp(playerCount, 1, 5);

    // Strip any previously generated player zones while preserving the 22 printed mat zones.
    layout.zones.erase(
        std::remove_if(layout.zones.begin(), layout.zones.end(),
                       [](const Zone& z) { return isPlayerZoneId(z.id); }),
        layout.zones.end());
    layout.playerTrays.clear();

    constexpr float kTrayW = 2310.f;
    constexpr float kTrayH = 585.f;
    constexpr float kGapX = 80.f;
    constexpr float kGapY = 85.f;

    const float boardCenterX = layout.boardSize.x * 0.5f;
    const float topY = layout.boardSize.y + (nPlayers == 1 ? 35.f : 60.f);

    for (int p = 0; p < nPlayers; ++p) {
        float tx = 0.f;
        float ty = topY;

        if (nPlayers == 1) {
            tx = boardCenterX - kTrayW * 0.5f;
        } else {
            const int row = p / 2;
            const int col = p % 2;
            const bool isOddLastRow = (col == 0 && p == nPlayers - 1);
            ty = topY + static_cast<float>(row) * (kTrayH + kGapY);
            if (isOddLastRow) {
                tx = boardCenterX - kTrayW * 0.5f;
            } else if (col == 0) {
                tx = boardCenterX - kGapX * 0.5f - kTrayW;
            } else {
                tx = boardCenterX + kGapX * 0.5f;
            }
        }

        layout.playerTrays.push_back({p, {tx, ty, kTrayW, kTrayH}});

        const std::string prefix =
            (nPlayers > 1) ? ("P" + std::to_string(p + 1) + " ") : std::string{};

        auto addZone = [&](std::string_view role, const std::string& baseLabel, ZoneKind kind,
                           ZoneTone tone, Rect relRect) {
            Zone z;
            z.id = playerZoneId(p, role);
            z.label = prefix + baseLabel;
            z.kind = kind;
            z.tone = tone;
            z.rect = {tx + relRect.x, ty + relRect.y, relRect.w, relRect.h};
            z.landscape = false;
            layout.zones.push_back(std::move(z));
        };

        addZone("avatar", "Avatar", ZoneKind::Slot, ZoneTone::HQ, {30.f, 30.f, 195.f, 255.f});
        addZone("strikes", "Strike Area", ZoneKind::Slot, ZoneTone::Danger,
                {230.f, 30.f, 195.f, 255.f});
        addZone("marks", "Mark Area", ZoneKind::Slot, ZoneTone::Danger,
                {127.f, 300.f, 200.f, 255.f});
        addZone("played", "Played / Active Area", ZoneKind::Area, ZoneTone::Wilds,
                {445.f, 30.f, 1400.f, 255.f});
        addZone("hand", "Hand Area", ZoneKind::Area, ZoneTone::HQ, {445.f, 300.f, 1400.f, 255.f});
        addZone("deck", "Deck Area", ZoneKind::Slot, ZoneTone::Neutral,
                {1865.f, 165.f, 195.f, 255.f});
        addZone("discard", "Discard Area", ZoneKind::Slot, ZoneTone::Neutral,
                {2070.f, 165.f, 195.f, 255.f});
    }
}

void avatarStatsForDef(int avatarDefId, int& outHealth, int& outDefense) {
    switch (avatarDefId) {
    case 64: // CIA Agent
        outHealth = 12;
        outDefense = 4;
        break;
    case 66: // Detective
        outHealth = 9;
        outDefense = 5;
        break;
    case 68: // Gangster
        outHealth = 10;
        outDefense = 4;
        break;
    case 70: // Guerrilla
        outHealth = 10;
        outDefense = 4;
        break;
    case 72: // Lieutenant
        outHealth = 9;
        outDefense = 5;
        break;
    case 74: // OWLF Agent
        outHealth = 11;
        outDefense = 4;
        break;
    case 76: // Radioman
        outHealth = 11;
        outDefense = 4;
        break;
    case 78: // Reporter
        outHealth = 9;
        outDefense = 5;
        break;
    case 80: // SWAT Officer
        outHealth = 11;
        outDefense = 4;
        break;
    case 82: // Tracker
        outHealth = 10;
        outDefense = 4;
        break;
    case 84: // City Hunter
        outHealth = 12;
        outDefense = 3;
        break;
    case 86: // Elder Hunter
        outHealth = 13;
        outDefense = 3;
        break;
    case 88: // Gunslinger Hunter
        outHealth = 12;
        outDefense = 3;
        break;
    case 90: // Jungle Hunter
        outHealth = 11;
        outDefense = 3;
        break;
    case 92: // Primeval Hunter
        outHealth = 11;
        outDefense = 3;
        break;
    default:
        outHealth = 10;
        outDefense = 4;
        break;
    }
}

std::filesystem::path findAssetDir() {
    if (const char* base = SDL_GetBasePath()) {
        auto p = std::filesystem::path(base) / "assets";
        if (std::filesystem::exists(p / "board_layout.json")) return p;
    }
    return std::filesystem::path(LC_ASSET_DIR);
}

std::optional<Catalog> loadCatalog(const std::filesystem::path& file) {
    auto j = readJson(file);
    if (!j) return std::nullopt;
    try {
        Catalog cat;
        for (auto& c : (*j)["cards"]) {
            CardDef d;
            d.id = c["id"].get<int>();
            d.name = c["name"].get<std::string>();
            d.group = c["group"].get<std::string>();
            if (c.contains("tex") && c["tex"].is_string()) {
                d.texture = c["tex"].get<std::string>();
            } else if (c.contains("src") && c["src"].is_string()) {
                std::filesystem::path rel(c["src"].get<std::string>());
                rel.replace_extension(".jpg");
                d.texture = "cards/" + rel.generic_string();
            }
            d.landscape = c.value("land", false);
            d.qty = c.value("qty", 1);
            d.title = c.value("title", std::string{});
            d.subtitle = c.value("subtitle", std::string{});
            d.rarity = c.value("rarity", std::string{});
            d.cost = c.value("cost", -1);
            d.attack = c.value("attack", -1);
            d.recruit = c.value("recruit", -1);
            d.classType = c.value("classType", std::string{});
            d.team = c.value("team", std::string{});
            if (c.contains("keywords") && c["keywords"].is_array()) {
                d.keywords = c["keywords"].get<std::vector<std::string>>();
            }
            d.rulesText = c.value("rulesText", std::string{});
            if (d.id != static_cast<int>(cat.cards.size())) throw std::runtime_error("card ids must be dense");
            cat.cards.push_back(std::move(d));
        }
        for (auto& g : (*j)["groups"]) {
            CardGroup grp;
            grp.key = g["key"].get<std::string>();
            grp.label = g["label"].get<std::string>();
            grp.section = g["section"].get<std::string>();
            grp.faceUp = g.value("faceUp", false);
            grp.cards = g["cards"].get<std::vector<int>>();
            cat.groupIndex[grp.key] = cat.groups.size();
            cat.groups.push_back(std::move(grp));
        }
        LC_LOG("catalog: %zu cards, %zu groups", cat.cards.size(), cat.groups.size());
        return cat;
    } catch (const std::exception& e) {
        LC_ERROR("catalog parse error: %s", e.what());
        return std::nullopt;
    }
}

std::optional<BoardLayout> loadBoardLayout(const std::filesystem::path& file) {
    auto j = readJson(file);
    if (!j) return std::nullopt;
    try {
        BoardLayout b;
        b.boardSize = {(*j)["board"]["w"].get<float>(), (*j)["board"]["h"].get<float>()};
        b.cardSize = {(*j)["card"]["w"].get<float>(), (*j)["card"]["h"].get<float>()};
        for (auto& z : (*j)["zones"]) {
            Zone zone;
            zone.id = z["id"].get<std::string>();
            zone.label = z["label"].get<std::string>();
            zone.kind = z["kind"].get<std::string>() == "area" ? ZoneKind::Area : ZoneKind::Slot;
            zone.tone = parseTone(z.value("tone", "neutral"));
            auto r = z["rect"];
            zone.rect = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
            zone.landscape = z.value("land", false);
            b.zones.push_back(std::move(zone));
        }
        configurePlayerZones(b, 1);
        return b;
    } catch (const std::exception& e) {
        LC_ERROR("board layout parse error: %s", e.what());
        return std::nullopt;
    }
}

} // namespace lc
