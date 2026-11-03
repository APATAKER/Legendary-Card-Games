#pragma once
#include "core/Math.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc {

/// One distinct printed card. `qty` physical copies exist in the box.
struct CardDef {
    int id = 0;
    std::string name;
    std::string group;
    std::string texture; // asset-relative path
    bool landscape = false;
    int qty = 1;

    // Optional rich card metadata (populated per card as verified)
    std::string title;
    std::string subtitle;
    std::string rarity;
    int cost = -1;
    int attack = -1;
    int recruit = -1;
    std::string classType;
    std::string team;
    std::vector<std::string> keywords;
    std::string rulesText;
};

struct CardGroup {
    std::string key;
    std::string label;
    std::string section;
    bool faceUp = false;
    std::vector<int> cards; // CardDef ids
};

struct Catalog {
    std::vector<CardDef> cards;
    std::vector<CardGroup> groups;
    std::unordered_map<std::string, size_t> groupIndex;

    const CardGroup* group(const std::string& key) const {
        auto it = groupIndex.find(key);
        return it == groupIndex.end() ? nullptr : &groups[it->second];
    }
    const CardDef& card(int id) const { return cards.at(static_cast<size_t>(id)); }
};

enum class ZoneKind { Slot, Area };
enum class ZoneTone { Neutral, Wilds, Danger, HQ };

/// A named region printed on the mat or inside a player board area.
struct Zone {
    std::string id;
    std::string label;
    ZoneKind kind = ZoneKind::Slot;
    ZoneTone tone = ZoneTone::Neutral;
    Rect rect;
    bool landscape = false; // slot expects a sideways card (Location)
};

/// Bounding rectangle for one player's separate 7-zone board tray.
struct PlayerTray {
    int playerIndex = 0;
    Rect rect;
};

struct BoardLayout {
    Vec2 boardSize;
    Vec2 cardSize; // portrait card, world units
    std::vector<Zone> zones;
    std::vector<PlayerTray> playerTrays;

    const Zone* zone(const std::string& id) const {
        for (auto& z : zones)
            if (z.id == id) return &z;
        return nullptr;
    }
};

/// Build deterministic per-player zone id: `p{playerIdx}_{role}` (e.g. `p0_hand`, `p1_avatar`).
std::string playerZoneId(int playerIdx, std::string_view role);

/// Parse a player zone id (`p0_hand`..`p4_discard`, plus legacy `player_*` as player 0).
bool parsePlayerZone(std::string_view zoneId, int& outPlayerIdx, std::string_view& outRole);

/// True if `zoneId` belongs to any player board area.
bool isPlayerZoneId(std::string_view zoneId);

/// Dynamically generate `1..5` separate, non-overlapping 7-zone player board areas below the mat.
void configurePlayerZones(BoardLayout& layout, int playerCount);

/// Exact printed Health and Defense values for all 15 Human & Predator Avatars.
void avatarStatsForDef(int avatarDefId, int& outHealth, int& outDefense);

/// Locates the asset directory: `<exe dir>/assets` for packaged builds, else the source tree.
std::filesystem::path findAssetDir();

std::optional<Catalog> loadCatalog(const std::filesystem::path& file);
std::optional<BoardLayout> loadBoardLayout(const std::filesystem::path& file);

} // namespace lc
