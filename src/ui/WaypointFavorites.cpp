#include "ui/WaypointFavorites.h"
#include "Shared.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <utility>

namespace
{
    bool g_loaded = false;
    std::map<std::string, std::vector<WaypointFavorites::Favorite>> g_byCharacter;
    const std::vector<WaypointFavorites::Favorite> g_empty;

    std::string CharacterKey(const std::string& character)
    {
        return character.empty() ? std::string("default") : character;
    }

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

    void Load()
    {
        if (g_loaded) return;
        g_loaded = true;
        g_byCharacter.clear();

        std::ifstream f(FavoritesPath());
        if (!f) return;
        try
        {
            nlohmann::json j; f >> j;
            const nlohmann::json* chars = nullptr;
            if (j.is_object() && j.contains("characters") && j["characters"].is_object())
                chars = &j["characters"];
            if (!chars) return;

            for (auto it = chars->begin(); it != chars->end(); ++it)
            {
                if (!it.value().is_array()) continue;
                std::vector<WaypointFavorites::Favorite>& list = g_byCharacter[CharacterKey(it.key())];
                for (const auto& e : it.value())
                {
                    WaypointFavorites::Favorite fav = ParseFavorite(e);
                    if (fav.stepId.empty() || fav.chatLink.empty()) continue;
                    if (std::any_of(list.begin(), list.end(), [&](const WaypointFavorites::Favorite& x){ return x.stepId == fav.stepId; })) continue;
                    list.push_back(std::move(fav));
                }
            }
        }
        catch (...) { g_byCharacter.clear(); }
    }

    void Save()
    {
        nlohmann::json chars = nlohmann::json::object();
        for (const auto& kv : g_byCharacter)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const WaypointFavorites::Favorite& f : kv.second)
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
            chars[kv.first] = std::move(arr);
        }

        nlohmann::json root = {
            { "version", 1 },
            { "characters", std::move(chars) }
        };

        Json::WriteAtomic(FavoritesPath(), root.dump(2));
    }
}

const std::vector<WaypointFavorites::Favorite>& WaypointFavorites::List(const std::string& character)
{
    Load();
    const auto it = g_byCharacter.find(CharacterKey(character));
    return it == g_byCharacter.end() ? g_empty : it->second;
}

bool WaypointFavorites::Contains(const std::string& character, const std::string& stepId)
{
    if (stepId.empty()) return false;
    const std::vector<Favorite>& list = List(character);
    return std::any_of(list.begin(), list.end(), [&](const Favorite& f){ return f.stepId == stepId; });
}

bool WaypointFavorites::Add(const std::string& character, const Favorite& favorite)
{
    Load();
    if (favorite.stepId.empty() || favorite.chatLink.empty()) return false;
    std::vector<Favorite>& list = g_byCharacter[CharacterKey(character)];
    if (std::any_of(list.begin(), list.end(), [&](const Favorite& f){ return f.stepId == favorite.stepId; })) return false;
    list.push_back(favorite);
    Save();
    return true;
}

bool WaypointFavorites::Remove(const std::string& character, const std::string& stepId)
{
    Load();
    if (stepId.empty()) return false;
    std::vector<Favorite>& list = g_byCharacter[CharacterKey(character)];
    const auto oldSize = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Favorite& f){ return f.stepId == stepId; }), list.end());
    if (list.size() == oldSize) return false;
    Save();
    return true;
}

bool WaypointFavorites::Toggle(const std::string& character, const Favorite& favorite)
{
    return Contains(character, favorite.stepId) ? Remove(character, favorite.stepId) : Add(character, favorite);
}

void WaypointFavorites::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    Load();
    auto itF = g_byCharacter.find(CharacterKey(from));
    if (itF == g_byCharacter.end()) return;
    std::vector<Favorite>& dst = g_byCharacter[CharacterKey(to)];
    for (const Favorite& f : itF->second)
        if (std::none_of(dst.begin(), dst.end(), [&](const Favorite& d){ return d.stepId == f.stepId; }))
            dst.push_back(f);
    g_byCharacter.erase(itF);
    Save();
}

void WaypointFavorites::PurgeChar(const std::string& name)
{
    Load();
    if (g_byCharacter.erase(CharacterKey(name))) Save();
}

void WaypointFavorites::CollectCharNames(std::set<std::string>& out)
{
    Load();
    for (const auto& kv : g_byCharacter) out.insert(kv.first);
}
