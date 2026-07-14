#include "util/Dyes.h"
#include <nlohmann/json.hpp>
#include <fstream>

bool Dyes::Load(const std::string& addonDir, std::vector<Gw2Ui::Dye>& out, std::vector<std::string>& nameStore)
{
    std::ifstream f(addonDir + "\\data\\colors.json");
    if (!f) return false;

    nlohmann::json j;
    try { f >> j; } catch (...) { return false; }
    if (!j.is_array()) return false;

    out.clear();
    nameStore.clear();
    nameStore.reserve(j.size());   // reserve so c_str() pointers stay valid as we push
    out.reserve(j.size());

    for (const auto& c : j)
    {
        // Per-entry try/catch: a null "name"/"id"/"item" (value() throws on null) or a non-numeric rgb element
        // (get<int>() throws) skips just that dye instead of aborting the whole bundled-palette load.
        try
        {
            if (!c.is_object() || !c.contains("cloth")) continue;
            const auto& cloth = c["cloth"];
            if (!cloth.contains("rgb") || !cloth["rgb"].is_array() || cloth["rgb"].size() < 3) continue;

            nameStore.push_back(c.value("name", std::string("Dye")));

            Gw2Ui::Dye d;
            d.id   = c.value("id", 0);
            d.name = nameStore.back().c_str();
            d.r    = (unsigned char)cloth["rgb"][0].get<int>();
            d.g    = (unsigned char)cloth["rgb"][1].get<int>();
            d.b    = (unsigned char)cloth["rgb"][2].get<int>();
            d.item = c.value("item", 0);   // the dye-unlock item id (for a chat link via the ItemCatalog)
            out.push_back(d);
        }
        catch (...) { /* malformed dye entry -- skip it */ }
    }
    return !out.empty();
}
