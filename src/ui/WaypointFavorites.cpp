#include "ui/WaypointFavorites.h"
#include "Shared.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <utility>

namespace
{
    bool g_loaded = false;
    uint64_t g_version = 0;
    std::vector<WaypointFavorites::Favorite> g_list;   // ordered: this IS the manual order

    std::string FavoritesPath()
    {
        const char* d = AddonDir();
        return d ? std::string(d) + "\\waypoint-favorites.json" : std::string();
    }

    WaypointFavorites::Favorite ParseFavorite(const nlohmann::json& e)
    {
        WaypointFavorites::Favorite f;
        if (!e.is_object()) return f;
        f.stepId = e.value("stepId", std::string());
        f.name = e.value("name", std::string());
        f.zoneName = e.value("zoneName", std::string());
        f.chatLink = e.value("chatLink", std::string());
        f.mapId = e.value("mapId", 0u);
        f.continentId = e.value("continentId", 1);
        f.cx = e.value("cx", 0.f);
        f.cy = e.value("cy", 0.f);
        return f;
    }

    // Append unless this stepId is already present. Used by both load paths, so a duplicate can never enter
    // the list regardless of which shape the file was in.
    void AppendUnique(std::vector<WaypointFavorites::Favorite>& out, WaypointFavorites::Favorite fav)
    {
        if (fav.stepId.empty() || fav.chatLink.empty()) return;
        if (std::any_of(out.begin(), out.end(),
                        [&](const WaypointFavorites::Favorite& x) { return x.stepId == fav.stepId; }))
            return;
        out.push_back(std::move(fav));
    }

    void Save()
    {
        const std::string path = FavoritesPath();
        if (path.empty()) return;
        nlohmann::json arr = nlohmann::json::array();
        for (const WaypointFavorites::Favorite& f : g_list)
        {
            arr.push_back({
                { "stepId", f.stepId },
                { "name", f.name },
                { "zoneName", f.zoneName },
                { "chatLink", f.chatLink },
                { "mapId", f.mapId },
                { "continentId", f.continentId },
                { "cx", f.cx },
                { "cy", f.cy }
            });
        }
        nlohmann::json root = {
            { "version", 2 },          // 2 = one account-wide ordered list (1 was keyed by character)
            { "favorites", std::move(arr) }
        };
        Json::WriteAtomic(path, root.dump(2));
    }

    void Load()
    {
        if (g_loaded) return;
        g_loaded = true;
        g_list.clear();

        std::ifstream f(FavoritesPath());
        if (!f) return;
        bool migrated = false;
        try
        {
            nlohmann::json j; f >> j;
            if (!j.is_object()) return;

            if (j.contains("favorites") && j["favorites"].is_array())          // v2: already account-wide
            {
                for (const auto& e : j["favorites"])
                    AppendUnique(g_list, ParseFavorite(e));
            }
            else if (j.contains("characters") && j["characters"].is_object())  // v1: fold the alts together
            {
                // First-seen wins, walking characters in file order: a waypoint starred on four alts ends up
                // in the list once, and the resulting order is at least deterministic.
                const nlohmann::json& chars = j["characters"];
                for (auto it = chars.begin(); it != chars.end(); ++it)
                {
                    if (!it.value().is_array()) continue;
                    for (const auto& e : it.value())
                        AppendUnique(g_list, ParseFavorite(e));
                }
                migrated = true;
            }
        }
        catch (...)
        {
            g_list.clear();   // a corrupt file must not take the addon down with it
            return;
        }
        if (migrated) Save();   // rewrite in the new shape so the fold only ever happens once
        ++g_version;
    }
}

const std::vector<WaypointFavorites::Favorite>& WaypointFavorites::List()
{
    Load();
    return g_list;
}

bool WaypointFavorites::Contains(const std::string& stepId)
{
    if (stepId.empty()) return false;
    Load();
    return std::any_of(g_list.begin(), g_list.end(), [&](const Favorite& f) { return f.stepId == stepId; });
}

bool WaypointFavorites::Add(const Favorite& favorite)
{
    Load();
    if (favorite.stepId.empty() || favorite.chatLink.empty()) return false;
    const size_t before = g_list.size();
    AppendUnique(g_list, favorite);
    if (g_list.size() == before) return false;
    ++g_version;
    Save();
    return true;
}

bool WaypointFavorites::Remove(const std::string& stepId)
{
    Load();
    if (stepId.empty()) return false;
    const size_t before = g_list.size();
    g_list.erase(std::remove_if(g_list.begin(), g_list.end(),
                                [&](const Favorite& f) { return f.stepId == stepId; }),
                 g_list.end());
    if (g_list.size() == before) return false;
    ++g_version;
    Save();
    return true;
}

bool WaypointFavorites::Toggle(const Favorite& favorite)
{
    return Contains(favorite.stepId) ? Remove(favorite.stepId) : Add(favorite);
}

bool WaypointFavorites::Move(int from, int to)
{
    Load();
    const int n = (int)g_list.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    Favorite moved = std::move(g_list[from]);
    g_list.erase(g_list.begin() + from);
    g_list.insert(g_list.begin() + to, std::move(moved));   // `to` is the final index, not a pre-erase one
    ++g_version;
    Save();
    return true;
}

uint64_t WaypointFavorites::Version()
{
    Load();
    return g_version;
}
