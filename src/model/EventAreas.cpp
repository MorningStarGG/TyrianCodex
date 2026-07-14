#include "model/EventAreas.h"

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

    bool JBool(const json& o, const char* key, bool def = false)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }

    float JFloat(const json& o, const char* key, float def = -1.f)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number()) ? it->get<float>() : def;
    }

    std::vector<std::string> JStringArray(const json& o, const char* key)
    {
        std::vector<std::string> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array()) return out;
        for (const auto& v : *it)
            if (v.is_string()) out.push_back(v.get<std::string>());
        return out;
    }

    bool ReadPoint2(const json& o, const char* key, float& x, float& y)
    {
        auto it = o.find(key);
        if (it == o.end() || !it->is_array() || it->size() < 2) return false;
        if (!(*it)[0].is_number() || !(*it)[1].is_number()) return false;
        x = (*it)[0].get<float>();
        y = (*it)[1].get<float>();
        return true;
    }

    EventArea ParseArea(const json& e)
    {
        EventArea out;
        out.EventId        = JStr(e, "eventId");
        out.Name           = JStr(e, "name");
        out.MapId          = (uint32_t)JInt(e, "mapId", 0);
        out.Category       = JStr(e, "category");
        out.Flags          = JStringArray(e, "flags");
        out.Level          = JInt(e, "level", 0);
        out.ViewerEligible = JBool(e, "viewerEligible", false);
        out.LocationType   = JStr(e, "locationType");
        out.RadiusMeters   = JFloat(e, "radiusMeters", -1.f);
        out.WikiTitle      = JStr(e, "wikiTitle");
        out.WikiUrl        = JStr(e, "wikiUrl");
        out.Description    = JStr(e, "description");
        ReadPoint2(e, "world", out.WorldX, out.WorldZ);
        ReadPoint2(e, "continent", out.ContinentX, out.ContinentY);
        return out;
    }
}

bool EventAreaData::Load(const std::string& path)
{
    loaded_ = false;
    areas_.clear();
    areasByMap_.clear();

    std::ifstream f(path);
    if (!f) return false;

    json j;
    try { f >> j; }
    catch (...) { return false; }
    if (!j.is_object()) return false;

    try
    {
        auto it = j.find("events");
        if (it != j.end() && it->is_array())
        {
            areas_.reserve(it->size());
            for (const auto& e : *it)
            {
                if (!e.is_object()) continue;
                EventArea area = ParseArea(e);
                if (area.EventId.empty() || area.Name.empty() || area.MapId == 0) continue;
                const size_t index = areas_.size();
                areas_.push_back(std::move(area));
                areasByMap_[areas_.back().MapId].push_back(index);
            }
        }
    }
    catch (...)
    {
        areas_.clear();
        areasByMap_.clear();
        return false;
    }

    loaded_ = !areas_.empty();
    return loaded_;
}

std::vector<const EventArea*> EventAreaData::AreasForMap(uint32_t mapId) const
{
    std::vector<const EventArea*> out;
    auto it = areasByMap_.find(mapId);
    if (it == areasByMap_.end()) return out;
    out.reserve(it->second.size());
    for (size_t index : it->second)
        if (index < areas_.size()) out.push_back(&areas_[index]);
    return out;
}
