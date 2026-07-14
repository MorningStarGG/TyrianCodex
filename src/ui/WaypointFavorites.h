#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace WaypointFavorites
{
    struct Favorite
    {
        std::string stepId;
        std::string name;
        std::string zoneName;
        std::string chatLink;
        uint32_t    mapId = 0;
        int         continentId = 1;
        float       cx = 0.f;
        float       cy = 0.f;
    };

    const std::vector<Favorite>& List(const std::string& character);
    bool Contains(const std::string& character, const std::string& stepId);
    bool Add(const std::string& character, const Favorite& favorite);
    bool Remove(const std::string& character, const std::string& stepId);
    bool Toggle(const std::string& character, const Favorite& favorite);

    // -- per-character maintenance (rename detector + Diagnostics cleanup) --
    void RenameChar(const std::string& from, const std::string& to);   // union from's favorites into to + save
    void PurgeChar(const std::string& name);                           // drop a character's favorites + save
    void CollectCharNames(std::set<std::string>& out);                 // every character with favorites
}
