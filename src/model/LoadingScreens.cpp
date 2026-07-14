#include "model/LoadingScreens.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    std::string JStr(const json& o, const char* key)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    int JInt(const json& o, const char* key, int def = 0)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }

    LoadingScreen ParseScreen(const json& e)
    {
        LoadingScreen out;
        out.MapId     = (uint32_t)JInt(e, "mapId", 0);
        auto ids = e.find("mapIds");
        if (ids != e.end() && ids->is_array())
        {
            for (const auto& id : *ids)
            {
                if (!id.is_number_integer()) continue;
                const int v = id.get<int>();
                if (v > 0) out.MapIds.push_back((uint32_t)v);
            }
        }
        if (out.MapIds.empty() && out.MapId > 0)
            out.MapIds.push_back(out.MapId);
        out.Kind      = JStr(e, "kind");
        out.Name      = JStr(e, "name");
        out.Mode      = JStr(e, "mode");
        out.RelPath   = JStr(e, "relPath");
        out.WikiPage  = JStr(e, "wikiPage");
        out.WikiUrl   = JStr(e, "wikiUrl");
        out.FileTitle = JStr(e, "fileTitle");
        return out;
    }
}

bool LoadingScreenData::Load(const std::string& path)
{
    loaded_ = false;
    screens_.clear();
    byMap_.clear();

    std::ifstream f(path);
    if (!f) return false;

    json j;
    try { f >> j; }
    catch (...) { return false; }
    if (!j.is_object()) return false;

    try
    {
        auto it = j.find("screens");
        if (it == j.end() || !it->is_array()) return false;
        screens_.reserve(it->size());
        for (const auto& e : *it)
        {
            if (!e.is_object()) continue;
            LoadingScreen screen = ParseScreen(e);
            if (screen.RelPath.empty()) continue;
            const size_t index = screens_.size();
            screens_.push_back(std::move(screen));
            for (uint32_t mapId : screens_.back().MapIds)
                if (mapId > 0 && byMap_.find(mapId) == byMap_.end())
                    byMap_[mapId] = index;
        }
    }
    catch (...)
    {
        screens_.clear();
        byMap_.clear();
        return false;
    }

    loaded_ = !screens_.empty();
    return loaded_;
}

const LoadingScreen* LoadingScreenData::ForMap(uint32_t mapId) const
{
    auto it = byMap_.find(mapId);
    if (it == byMap_.end()) return nullptr;
    return it->second < screens_.size() ? &screens_[it->second] : nullptr;
}
