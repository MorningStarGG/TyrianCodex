#pragma once
#include <string>
#include <unordered_map>

// The farming category facets shown as the Farming tab's Row-1 chips, plus the shared category<->node mapping
// used by the tab, the in-world/map overlay, and the active run. 0 = All matches everything; the rest fold the
// dataset's node categories (ore/wood/plant/seafood/special/misc/farmroute/event) into the handful of player-facing
// buckets. Material display names + chip tooltips also live here so the tab, overlay, and run agree.
namespace Farm
{
    inline const char* const kCategoryNames[] = { "All", "Ore", "Wood", "Plant", "Seafood", "Misc", "Event" };
    inline constexpr int      kCategoryCount   = 7;

    // Does node category `nodeCat` fall under the selected Row-1 chip `cat`?
    inline bool CategoryMatch(int cat, const std::string& nodeCat)
    {
        switch (cat)
        {
            case 1: return nodeCat == "ore" || nodeCat == "special";
            case 2: return nodeCat == "wood";
            case 3: return nodeCat == "plant";
            case 4: return nodeCat == "seafood";
            case 5: return nodeCat == "farmroute" || nodeCat == "misc";   // themed routes + mixed/combo nodes
            case 6: return nodeCat == "event";                            // holiday / festival gathering nodes
            default: return true;   // 0 = All
        }
    }

    // A short hover tooltip for each Row-1 category chip.
    inline const char* CategoryDesc(int cat)
    {
        switch (cat)
        {
            case 0:  return "Every gathering node on this map.";
            case 1:  return "Mining nodes - copper, iron, mithril, orichalcum and the rest.";
            case 2:  return "Logging nodes - soft, seasoned, elder and ancient wood.";
            case 3:  return "Harvesting nodes - vegetables, herbs and fruit (incl. mixed-herb routes).";
            case 4:  return "Underwater nodes - clams, mussels, seaweed and coral.";
            case 5:  return "Themed farm routes plus mixed / combo nodes that aren't a single material.";
            case 6:  return "Holiday / festival gathering nodes - Candy Corn and the like, live only during the event.";
            default: return "";
        }
    }

    // Canonical material key -> wiki-verified display name, populated at load from data/farming/index.json's
    // "taxonomy" map (build_farming.py MATERIALS is the single source of truth). MaterialPretty looks this up so
    // the tab/overlay/run show the real in-game item names instead of a title-cased key.
    inline std::unordered_map<std::string, std::string>& MaterialNameRegistry()
    {
        static std::unordered_map<std::string, std::string> reg;
        return reg;
    }
    inline void RegisterMaterialNames(const std::unordered_map<std::string, std::string>& names)
    {
        MaterialNameRegistry() = names;
    }

    // "elder_wood" -> "Elder Wood Log" (from the shipped taxonomy); title-cases the key as a fallback for any
    // material not present in the taxonomy (e.g. an older bundle).
    inline std::string MaterialPretty(std::string s)
    {
        const auto& reg = MaterialNameRegistry();
        auto it = reg.find(s);
        if (it != reg.end()) return it->second;
        if (s == "plant_mixed") return "Mixed";   // the only non-generic fallback case
        for (char& c : s) if (c == '_') c = ' ';
        bool start = true;
        for (char& c : s) { if (start && c >= 'a' && c <= 'z') c = (char)(c - 32); start = (c == ' '); }
        return s;
    }

    // The Row-2 material chip / list tooltip -- the pretty name, with a fuller explanation for the combination
    // node types that aren't a single material (so the player knows what "Mixed"/"Combo" actually are).
    inline std::string MaterialDesc(const std::string& mat)
    {
        if (mat == "combo")       return "Combo - combination mining/harvesting nodes that yield more than one type of material.";
        if (mat == "mixed")       return "Mixed - a route that gathers several materials together.";
        if (mat == "plant_mixed") return "Mixed - a mixed-herb gathering route.";
        return MaterialPretty(mat);
    }
}
