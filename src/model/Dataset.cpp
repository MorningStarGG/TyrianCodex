#include "model/Dataset.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "guide/FarmCategories.h"

using nlohmann::json;

namespace
{
    // Null-safe field readers: nlohmann's value(key, default) THROWS type_error.302 when the key exists
    // but holds null (e.g. recommendedLevel is null on every non-heart step), which would propagate out
    // as an unhandled C++ exception and crash the game on load. These return the default for missing OR
    // null OR wrong-typed fields instead.
    std::string JStr(const json &o, const char *key)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }
    int JInt(const json &o, const char *key, int def)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }
    bool JBool(const json &o, const char *key, bool def)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }
    std::vector<std::string> JStringArray(const json &o, const char *key)
    {
        std::vector<std::string> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array())
            return out;
        for (const auto &v : *it)
            if (v.is_string())
                out.push_back(v.get<std::string>());
        return out;
    }
    std::vector<TrailSectionRange> JTrailSections(const json &o, const char *key, int trailSize)
    {
        std::vector<TrailSectionRange> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array() || trailSize <= 0)
            return out;
        for (const auto &v : *it)
        {
            if (!v.is_array() || v.size() < 2 || !v[0].is_number_integer() || !v[1].is_number_integer())
                continue;
            TrailSectionRange s;
            s.Start = v[0].get<int>();
            s.End = v[1].get<int>();
            if (s.Start < 0 || s.End < s.Start || s.End >= trailSize)
                continue;
            out.push_back(s);
        }
        return out;
    }
    float JNum(const json &j, const char *k)
    {
        return (float)(j.contains(k) && j[k].is_number() ? j[k].get<double>() : 0.0);
    }
    std::vector<RouteMarker> JRouteMarkers(const json &o, const char *key)
    {
        std::vector<RouteMarker> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array())
            return out;
        out.reserve(it->size());
        for (const auto &m : *it)
        {
            if (!m.is_object())
                continue;
            RouteMarker mk;
            mk.X = JNum(m, "x");
            mk.Y = JNum(m, "y");
            mk.Z = JNum(m, "z");
            mk.TrailIndex = JInt(m, "trailIndex", -1);
            mk.Mount = JStr(m, "mount");
            mk.Icon = JStr(m, "icon");
            if (!mk.Icon.empty())
                out.push_back(std::move(mk));
        }
        return out;
    }
    std::vector<RouteTransition> JRouteTransitions(const json &o, const char *key)
    {
        std::vector<RouteTransition> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array())
            return out;
        out.reserve(it->size());
        for (const auto &v : *it)
        {
            if (!v.is_object())
                continue;
            RouteTransition tr;
            tr.Id = JStr(v, "id");
            tr.SourceBranch = JStr(v, "sourceBranch");
            tr.SourceType = JStr(v, "sourceType");
            tr.SourceFile = JStr(v, "sourceFile");
            tr.Message = JStr(v, "message");
            tr.MapId = (uint32_t)JInt(v, "mapId", 0);
            tr.X = JNum(v, "x");
            tr.Y = JNum(v, "y");
            tr.Z = JNum(v, "z");
            tr.TrailIndex = JInt(v, "trailIndex", -1);
            tr.SectionBefore = JInt(v, "sectionBefore", -1);
            tr.SectionAfter = JInt(v, "sectionAfter", -1);
            if (v.contains("coord") && v["coord"].is_array() && v["coord"].size() >= 2)
            {
                tr.CX = v["coord"][0].get<float>();
                tr.CY = v["coord"][1].get<float>();
            }
            if (v.contains("links") && v["links"].is_array())
                for (const auto &l : v["links"])
                {
                    if (!l.is_object())
                        continue;
                    RouteTransitionLink link;
                    link.Label = JStr(l, "label");
                    link.ChatLink = JStr(l, "chatLink");
                    if (!link.ChatLink.empty())
                        tr.Links.push_back(std::move(link));
                }
            if (tr.Links.empty() && v.contains("chatLinks") && v["chatLinks"].is_array())
                for (const auto &l : v["chatLinks"])
                {
                    if (!l.is_string())
                        continue;
                    RouteTransitionLink link;
                    link.ChatLink = l.get<std::string>();
                    if (!link.ChatLink.empty())
                        tr.Links.push_back(std::move(link));
                }
            if (!tr.Id.empty() && !tr.Links.empty())
                out.push_back(std::move(tr));
        }
        return out;
    }
    void ReadStepsOverlay(const json &o, std::map<std::string, RouteStepInfo> &out)
    {
        if (!o.contains("steps") || !o["steps"].is_object())
            return;
        for (auto it = o["steps"].begin(); it != o["steps"].end(); ++it)
        {
            const auto &v = it.value();
            RouteStepInfo info;
            info.Order = JInt(v, "order", 0);
            info.TrailIndex = JInt(v, "trailIndex", -1);
            out[it.key()] = info;
        }
    }
    RouteOverlay ReadRouteOverlay(const json &r)
    {
        RouteOverlay route;
        route.Id = JStr(r, "id");
        route.Label = JStr(r, "label");
        route.Author = JStr(r, "author");
        route.Kind = JStr(r, "kind");
        route.Source = JStr(r, "source");
        route.SourceBranch = JStr(r, "sourceBranch");
        route.Complete = JBool(r, "complete", true);
        if (r.contains("trail") && r["trail"].is_array())
        {
            route.Trail.reserve(r["trail"].size());
            for (const auto &p : r["trail"])
                if (p.is_array() && p.size() >= 3)
                    route.Trail.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
        }
        route.Sections = JTrailSections(r, "sections", (int)route.Trail.size());
        ReadStepsOverlay(r, route.Steps);
        route.RouteMarkers = JRouteMarkers(r, "routeMarkers");
        route.RouteTransitions = JRouteTransitions(r, "routeTransitions");
        return route;
    }
    std::string ParentDir(const std::string &path)
    {
        const size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::map<std::string, std::vector<TaskChip>> LoadHeartTaskChips(const std::string &path)
    {
        std::map<std::string, std::vector<TaskChip>> out;
        std::ifstream f(path);
        if (!f)
            return out;

        json j;
        try
        {
            f >> j;
        }
        catch (...)
        {
            return out;
        }
        try
        {
            if (!j.contains("tasksByStepId") || !j["tasksByStepId"].is_object())
                return out;
            for (auto it = j["tasksByStepId"].begin(); it != j["tasksByStepId"].end(); ++it)
            {
                const auto &entry = it.value();
                if (!entry.is_object() || !entry.contains("chips") || !entry["chips"].is_array())
                    continue;
                std::vector<TaskChip> chips;
                for (const auto &c : entry["chips"])
                {
                    if (!c.is_object())
                        continue;
                    TaskChip chip;
                    chip.Label = JStr(c, "label");
                    chip.Category = JStr(c, "category");
                    chip.Count = JInt(c, "count", 0);
                    chip.Examples = JStringArray(c, "examples");
                    chip.Progress = JStringArray(c, "progress");
                    if (!chip.Label.empty())
                        chips.push_back(std::move(chip));
                }
                if (!chips.empty())
                    out[it.key()] = std::move(chips);
            }
        }
        catch (...)
        {
            out.clear();
        }
        return out;
    }

    // Clone the primary steps, overlay a route's per-stepId {order, trailIndex}, and re-sort by
    // order. Steps absent from the overlay keep their primary slot.
    std::vector<Step> BuildRouteSteps(const Zone &z, const RouteOverlay &route)
    {
        std::vector<Step> list = z._primarySteps;
        for (Step &s : list)
        {
            auto it = route.Steps.find(s.StepId);
            if (it != route.Steps.end())
            {
                s.Order = it->second.Order;
                s.TrailIndex = it->second.TrailIndex;
            }
        }
        std::stable_sort(list.begin(), list.end(), [](const Step &a, const Step &b)
                         { return a.Order < b.Order; });
        return list;
    }
    const char *RoutePreferenceId(int pref)
    {
        switch (pref)
        {
        case 1: return "tekkit-mount";
        case 2: return "lady-foot";
        case 3: return "lady-mount";
        default: return "tekkit-foot";
        }
    }
    std::vector<std::string> RouteFallbackOrder(int pref)
    {
        switch (pref)
        {
        case 1: return {"tekkit-mount", "lady-mount", "tekkit-foot", "lady-foot"};
        case 2: return {"lady-foot", "tekkit-foot", "lady-mount", "tekkit-mount"};
        case 3: return {"lady-mount", "tekkit-mount", "lady-foot", "tekkit-foot"};
        default: return {"tekkit-foot", "lady-foot", "tekkit-mount", "lady-mount"};
        }
    }
}

void Zone::Activate(const std::string &preferredKind)
{
    if (!_primaryCaptured)
    {
        _primarySteps = Steps;
        _primaryTrail = Trail;
        _primaryTrailSections = TrailSections;
        _primaryRouteMarkers = RouteMarkers;
        _primaryRouteTransitions = RouteTransitions;
        _primaryCaptured = true;
    }

    if (HasAlt && preferredKind == Alt.Kind)
    {
        if (!_altBuilt)
        {
            _altSteps = BuildRouteSteps(*this, Alt);
            _altBuilt = true;
        }
        Steps = _altSteps;
        Trail = Alt.Trail;
        TrailSections = Alt.Sections;
        RouteMarkers = Alt.RouteMarkers;
        RouteTransitions = Alt.RouteTransitions;
        ActiveKind = Alt.Kind;
        ActiveRouteId = Alt.Id.empty() ? Alt.Kind : Alt.Id;
        ActiveRouteLabel = Alt.Label.empty() ? Alt.Kind : Alt.Label;
        ActiveRouteAuthor = Alt.Author;
        ActiveRouteFallback = false;
        ActiveComplete = Alt.Complete;
    }
    else
    {
        Steps = _primarySteps;
        Trail = _primaryTrail;
        TrailSections = _primaryTrailSections;
        RouteMarkers = _primaryRouteMarkers;
        RouteTransitions = _primaryRouteTransitions;
        ActiveKind = PrimaryKind.empty() ? "foot" : PrimaryKind;
        ActiveRouteId = ActiveKind;
        ActiveRouteLabel = ActiveKind;
        ActiveRouteAuthor.clear();
        ActiveRouteFallback = false;
        ActiveComplete = PrimaryComplete;
    }
}

void Zone::Activate(int routePreference)
{
    if (!_primaryCaptured)
    {
        _primarySteps = Steps;
        _primaryTrail = Trail;
        _primaryTrailSections = TrailSections;
        _primaryRouteMarkers = RouteMarkers;
        _primaryRouteTransitions = RouteTransitions;
        _primaryCaptured = true;
    }

    if (!RouteVariants.empty())
    {
        const RouteOverlay *chosen = nullptr;
        const std::string requested = RoutePreferenceId(routePreference);
        std::vector<std::string> order = RouteFallbackOrder(routePreference);
        auto findRoute = [&](const std::string &id, bool requireComplete) -> const RouteOverlay *
        {
            auto it = std::find_if(RouteVariants.begin(), RouteVariants.end(), [&](const RouteOverlay &r)
                                   { return r.Id == id && r.Trail.size() >= 2 && (!requireComplete || r.Complete); });
            return it != RouteVariants.end() ? &*it : nullptr;
        };
        for (bool requireComplete : {true, false})
        {
            for (const std::string &id : order)
            {
                chosen = findRoute(id, requireComplete);
                if (chosen)
                    break;
            }
            if (chosen)
                break;
        }
        for (bool requireComplete : {true, false})
        {
            if (chosen)
                break;
            for (const RouteOverlay &r : RouteVariants)
                if (r.Trail.size() >= 2 && (!requireComplete || r.Complete))
                {
                    chosen = &r;
                    break;
                }
        }

        if (chosen)
        {
            Steps = BuildRouteSteps(*this, *chosen);
            Trail = chosen->Trail;
            TrailSections = chosen->Sections;
            RouteMarkers = chosen->RouteMarkers;
            RouteTransitions = chosen->RouteTransitions;
            ActiveKind = chosen->Kind.empty() ? "foot" : chosen->Kind;
            ActiveRouteId = chosen->Id;
            ActiveRouteLabel = chosen->Label.empty() ? chosen->Id : chosen->Label;
            ActiveRouteAuthor = chosen->Author;
            ActiveRouteFallback = chosen->Id != requested;
            ActiveComplete = chosen->Complete;
            return;
        }
    }

    Activate(routePreference == 1 || routePreference == 3 ? "mount" : "foot");
    ActiveRouteFallback = false;
}

// Read a zone's heavy geometry (trail + legacy altRoute + source-aware routeVariants) from `j` into `out`.
// Shared by LoadZone (back-compat: an un-split <slug>.json that still carries them) and
// LoadZoneTrail (the lazy <slug>.trail.json side-file). Leaves
// the zone meta + steps untouched, so it can layer the trail onto an already-metadata-loaded Zone.
static void ReadZoneGeom(const json &j, Zone &out)
{
    if (j.contains("trail") && j["trail"].is_array())
    {
        out.Trail.clear();
        out.Trail.reserve(j["trail"].size());
        for (const auto &p : j["trail"])
            if (p.is_array() && p.size() >= 3)
                out.Trail.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
    }
    out.TrailSections = JTrailSections(j, "sections", (int)out.Trail.size());
    out.RouteMarkers = JRouteMarkers(j, "routeMarkers");
    out.RouteTransitions = JRouteTransitions(j, "routeTransitions");
    if (j.contains("altRoute") && j["altRoute"].is_object())
    {
        const auto &ar = j["altRoute"];
        out.Alt = ReadRouteOverlay(ar);
        out.HasAlt = out.Alt.Trail.size() >= 2 && !out.Alt.Kind.empty();
    }
    if (j.contains("routeVariants") && j["routeVariants"].is_array())
    {
        out.RouteVariants.clear();
        for (const auto &r : j["routeVariants"])
        {
            if (!r.is_object())
                continue;
            RouteOverlay route = ReadRouteOverlay(r);
            if (!route.Id.empty() && route.Trail.size() >= 2)
                out.RouteVariants.push_back(std::move(route));
        }
    }
}

bool LoadZone(const std::string &path, Zone &out)
{
    out = Zone{};

    std::ifstream f(path);
    if (!f)
        return false;

    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return false;
    }

    try
    {
        if (j.contains("zone") && j["zone"].is_object())
        {
            const auto &z = j["zone"];
            out.MapId = (uint32_t)JInt(z, "mapId", 0);
            out.ContinentId = JInt(z, "continentId", 1);
            out.Floor = JInt(z, "floor", 1);
            out.RegionId = JInt(z, "regionId", 0);
            out.RegionName = JStr(z, "regionName");
            out.Release = JStr(z, "release");
            out.Name = JStr(z, "name");
            out.MinLevel = JInt(z, "minLevel", 0);
            out.MaxLevel = JInt(z, "maxLevel", 0);

            auto rect4 = [](const json &r, float out4[4]) -> bool
            {
                // Guard the INNER array sizes + element types too: a malformed rect like [[1],[2]] would make
                // r[0][1] an out-of-bounds json::operator[] (UB in release -- NOT caught by the loader try/catch).
                if (!r.is_array() || r.size() < 2 || !r[0].is_array() || !r[1].is_array() ||
                    r[0].size() < 2 || r[1].size() < 2 ||
                    !r[0][0].is_number() || !r[0][1].is_number() || !r[1][0].is_number() || !r[1][1].is_number())
                    return false;
                out4[0] = r[0][0].get<float>();
                out4[1] = r[0][1].get<float>();
                out4[2] = r[1][0].get<float>();
                out4[3] = r[1][1].get<float>();
                return true;
            };
            const bool mr = z.contains("mapRect") && rect4(z["mapRect"], out.MapRect);
            const bool cr = z.contains("continentRect") && rect4(z["continentRect"], out.ContRect);
            out.HasRects = mr && cr;
            out.HasSectorRect = z.contains("sectorRect") && rect4(z["sectorRect"], out.SectorRect); // null/absent -> false
        }

        // (trail + altRoute now live in the lazy <slug>.trail.json; ReadZoneGeom below still reads them if an older
        // un-split file carries them inline, for back-compat.)
        if (j.contains("steps") && j["steps"].is_array())
        {
            out.Steps.reserve(j["steps"].size());
            for (const auto &s : j["steps"])
            {
                Step st;
                st.Type = JStr(s, "type");
                st.Name = JStr(s, "name");
                st.StepId = JStr(s, "stepId");
                st.TrailIndex = JInt(s, "trailIndex", -1);
                st.Order = JInt(s, "order", (int)out.Steps.size());
                st.RecommendedLevel = JInt(s, "recommendedLevel", -1);
                st.WikiText = JStr(s, "wikiText");
                st.WhatToDo = JStr(s, "whatToDo");
                st.ChatLink = JStr(s, "chatLink");
                st.WaypointChatLink = JStr(s, "waypointChatLink");
                if (s.contains("coord") && s["coord"].is_array() && s["coord"].size() >= 2)
                {
                    st.CX = s["coord"][0].get<float>();
                    st.CY = s["coord"][1].get<float>();
                }
                if (s.contains("bounds") && s["bounds"].is_array()) // renown-heart area polygon (continent coords)
                    for (const auto &pt : s["bounds"])
                        if (pt.is_array() && pt.size() >= 2 && pt[0].is_number() && pt[1].is_number())
                            st.Bounds.push_back(Pt2{pt[0].get<float>(), pt[1].get<float>()});
                out.Steps.push_back(std::move(st));
            }
            // Guidance walks objectives in authored route order
            std::stable_sort(out.Steps.begin(), out.Steps.end(),
                             [](const Step &a, const Step &b)
                             { return a.Order < b.Order; });
        }

        // Legacy primary/altRoute metadata is still read for old JSON. Schema 5 routeVariants live in the
        // sidecar geometry and are selected by Zone::Activate(int) when present.
        out.PrimaryKind = JStr(j, "primaryKind").empty() ? "foot" : JStr(j, "primaryKind");
        out.PrimaryComplete = JBool(j, "primaryComplete", true);
        out.ActiveKind = out.PrimaryKind;
        out.ActiveRouteId = out.PrimaryKind;
        out.ActiveRouteLabel = out.PrimaryKind;
        out.ActiveRouteAuthor.clear();
        out.ActiveRouteFallback = false;
        out.ActiveComplete = out.PrimaryComplete;
        ReadZoneGeom(j, out); // trail + altRoute IF an un-split file still has them inline (else stays empty -> lazy)

        // `hasTrail` (builder flag) tells us a route trail exists WITHOUT reading the side-file: a zone is added to the
        // active-guide map only if it has a trail (a hub/lounge with objectives but no route stays out, as before the
        // split). GeomLoaded is true only when the trail is actually in hand (a legacy inline file); else it's lazy.
        const bool hasTrail = JBool(j, "hasTrail", !out.Trail.empty());
        out.GeomLoaded = !out.Trail.empty();
        out.Loaded = out.MapId != 0 && hasTrail;
    }
    catch (...)
    {
        out = Zone{};
        return false;
    } // any malformed field -> disable the guide, never crash

    return out.Loaded;
}

// Lazy-load a zone's trail + route overlays from <zonesDir>/<z.Slug>.trail.json onto an already-metadata-loaded Zone.
bool LoadZoneTrail(const std::string &zonesDir, Zone &z)
{
    if (z.GeomLoaded)
        return true; // already have it (or a legacy inline file provided it)
    if (z.Slug.empty())
    {
        z.GeomLoaded = true;
        return false;
    }
    std::ifstream f(zonesDir + "\\" + z.Slug + ".trail.json");
    if (f)
    {
        json j;
        try
        {
            f >> j;
            ReadZoneGeom(j, z);
        }
        catch (...)
        { /* malformed geometry -> no trail, guide degrades gracefully */
        }
    }
    z.GeomLoaded = true; // mark attempted so we never re-read (even if the file is absent)
    return !z.Trail.empty();
}

int LoadAllZones(const std::string &zonesDir, std::map<uint32_t, Zone> &out)
{
    std::ifstream f(zonesDir + "\\index.json");
    if (!f)
        return 0;

    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return 0;
    }
    if (!j.contains("zones") || !j["zones"].is_array())
        return 0;

    int n = 0;
    for (const auto &e : j["zones"])
    {
        const std::string slug = JStr(e, "slug");
        if (slug.empty())
            continue;
        Zone z;
        if (LoadZone(zonesDir + "\\" + slug + ".json", z) && z.MapId != 0)
        {
            z.Slug = slug; // key for the lazy trail side-file (LoadZoneTrail)
            out[z.MapId] = std::move(z);
            ++n;
        }
    }
    const std::string dataDir = ParentDir(zonesDir);
    const auto heartTasks = LoadHeartTaskChips(dataDir.empty() ? "heart_tasks.json" : dataDir + "\\heart_tasks.json");
    if (!heartTasks.empty())
        for (auto &kv : out)
            for (Step &s : kv.second.Steps)
            {
                auto hit = heartTasks.find(s.StepId);
                if (hit != heartTasks.end())
                    s.TaskChips = hit->second;
            }
    return n;
}

namespace
{
    // Parse a "#RRGGBB" dataset tint to 0xRRGGBBAA (alpha forced opaque). Returns white on anything odd.
    unsigned int ParseTint(const std::string &s)
    {
        if (s.size() >= 7 && s[0] == '#')
        {
            unsigned int v = 0;
            for (int i = 1; i <= 6; ++i)
            {
                char c = s[i];
                int d;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (c >= 'a' && c <= 'f')
                    d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    d = c - 'A' + 10;
                else
                    return 0xFFFFFFFFu;
                v = (v << 4) | (unsigned)d;
            }
            return (v << 8) | 0xFFu; // RRGGBB -> RRGGBBAA
        }
        return 0xFFFFFFFFu;
    }

    bool LoadInstance(const std::string &path, Instance &out)
    {
        out = Instance{};
        std::ifstream f(path);
        if (!f)
            return false;
        json j;
        try
        {
            f >> j;
        }
        catch (...)
        {
            return false;
        }

        try
        {
            out.MapId = (uint32_t)JInt(j, "mapId", 0);
            out.Name = JStr(j, "name");
            out.Mode = JStr(j, "mode");
            out.Family = JStr(j, "family");
            if (out.Family.empty())
                out.Family = "dungeon";             // back-compat: pre-Family datasets were all dungeons
            out.RegionName = JStr(j, "regionName"); // story/tutorial instances carry a region + release
            out.Release = JStr(j, "release");       // -> the Story-tab expansion pill (Content > Story)
            if (j.contains("routes") && j["routes"].is_array())
                for (const auto &r : j["routes"])
                {
                    InstanceRoute route;
                    route.Name = JStr(r, "name");
                    if (r.contains("trail") && r["trail"].is_array())
                    {
                        route.Trail.reserve(r["trail"].size());
                        for (const auto &p : r["trail"])
                            if (p.is_array() && p.size() >= 3)
                                route.Trail.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
                    }
                    route.Sections = JTrailSections(r, "sections", (int)route.Trail.size());
                    if (r.contains("markers") && r["markers"].is_array())
                        for (const auto &m : r["markers"])
                        {
                            InstanceMarker mk;
                            mk.X = (float)(m.contains("x") && m["x"].is_number() ? m["x"].get<double>() : 0.0);
                            mk.Y = (float)(m.contains("y") && m["y"].is_number() ? m["y"].get<double>() : 0.0);
                            mk.Z = (float)(m.contains("z") && m["z"].is_number() ? m["z"].get<double>() : 0.0);
                            mk.TrailIndex = JInt(m, "trailIndex", -1);
                            mk.Text = JStr(m, "text");
                            mk.Kind = JStr(m, "kind");
                            mk.Icon = JStr(m, "icon");
                            mk.Tint = ParseTint(JStr(m, "tint"));
                            route.Markers.push_back(std::move(mk));
                        }
                    out.Routes.push_back(std::move(route));
                }
        }
        catch (...)
        {
            out = Instance{};
            return false;
        }

        out.Loaded = out.MapId != 0 && !out.Routes.empty();
        return out.Loaded;
    }

    // Farming dataset: the same route schema as a dungeon (reusing Instance) PLUS per-route material/category,
    // per-marker material/chatLink/role/iconAssetId, a flat Nodes list (overlay + combined-nearest), and
    // slug/regionName. Distinct from LoadInstance so the live dungeon path stays byte-for-byte unchanged.
    bool LoadFarming(const std::string &path, Instance &out)
    {
        out = Instance{};
        std::ifstream f(path);
        if (!f)
            return false;
        json j;
        try
        {
            f >> j;
        }
        catch (...)
        {
            return false;
        }

        try
        {
            out.MapId = (uint32_t)JInt(j, "mapId", 0);
            out.Name = JStr(j, "name");
            out.Slug = JStr(j, "slug");
            out.Mode = JStr(j, "mode");
            out.RegionName = JStr(j, "regionName");
            if (j.contains("routes") && j["routes"].is_array())
                for (const auto &r : j["routes"])
                {
                    InstanceRoute route;
                    route.Name = JStr(r, "name");
                    route.Category = JStr(r, "category");
                    route.Material = JStr(r, "material");
                    route.Source = JStr(r, "source");
                    if (r.contains("trail") && r["trail"].is_array())
                    {
                        route.Trail.reserve(r["trail"].size());
                        for (const auto &p : r["trail"])
                            if (p.is_array() && p.size() >= 3)
                                route.Trail.push_back({p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
                    }
                    route.Sections = JTrailSections(r, "sections", (int)route.Trail.size());
                    if (r.contains("markers") && r["markers"].is_array())
                        for (const auto &m : r["markers"])
                        {
                            InstanceMarker mk;
                            mk.X = JNum(m, "x");
                            mk.Y = JNum(m, "y");
                            mk.Z = JNum(m, "z");
                            mk.TrailIndex = JInt(m, "trailIndex", -1);
                            mk.Text = JStr(m, "text");
                            mk.Icon = JStr(m, "icon");
                            mk.Material = JStr(m, "material");
                            mk.ChatLink = JStr(m, "chatLink");
                            mk.Role = JStr(m, "role");
                            mk.Mount = JStr(m, "mount");
                            mk.IconAssetId = (uint32_t)JInt(m, "iconAssetId", 0);
                            route.Markers.push_back(std::move(mk));
                        }
                    out.Routes.push_back(std::move(route));
                }
            if (j.contains("nodes") && j["nodes"].is_array())
                for (const auto &n : j["nodes"])
                {
                    FarmNode nd;
                    nd.X = JNum(n, "x");
                    nd.Y = JNum(n, "y");
                    nd.Z = JNum(n, "z");
                    nd.Material = JStr(n, "material");
                    nd.Category = JStr(n, "category");
                    nd.Icon = JStr(n, "icon");
                    nd.ChatLink = JStr(n, "chatLink");
                    nd.Mount = JStr(n, "mount");
                    nd.IconAssetId = (uint32_t)JInt(n, "iconAssetId", 0);
                    out.Nodes.push_back(std::move(nd));
                }
        }
        catch (...)
        {
            out = Instance{};
            return false;
        }

        out.Loaded = out.MapId != 0 && (!out.Routes.empty() || !out.Nodes.empty());
        return out.Loaded;
    }
}

int LoadAllInstances(const std::string &instancesDir, std::map<uint32_t, Instance> &out)
{
    std::ifstream f(instancesDir + "\\index.json");
    if (!f)
        return 0;
    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return 0;
    }
    if (!j.contains("instances") || !j["instances"].is_array())
        return 0;

    int n = 0;
    for (const auto &e : j["instances"])
    {
        const uint32_t mapId = (uint32_t)JInt(e, "mapId", 0);
        const std::string slug = JStr(e, "slug");
        if (mapId == 0 || slug.empty())
            continue;
        Instance inst; // METADATA STUB (Dungeons/Story tab lists); geometry lazy
        inst.MapId = mapId;
        inst.Slug = slug;
        inst.Name = JStr(e, "name");
        inst.Mode = JStr(e, "mode");
        inst.Family = JStr(e, "family").empty() ? "dungeon" : JStr(e, "family");
        inst.RegionName = JStr(e, "regionName");
        inst.Release = JStr(e, "release");
        if (e.contains("routes") && e["routes"].is_array())
            for (const auto &r : e["routes"]) // route NAMES only (card labels / path picker); no trails
            {
                InstanceRoute route;
                route.Name = JStr(r, "name");
                inst.Routes.push_back(std::move(route));
            }
        inst.GeomLoaded = false;
        inst.Loaded = true; // usable for the lists; SwitchInstance loads the real routes
        out[mapId] = std::move(inst);
        ++n;
    }
    return n;
}

// Lazy-load a stub instance's full per-slug file (route trails + markers) in place, preserving the stub's Slug.
bool LoadInstanceFull(const std::string &instancesDir, Instance &inst)
{
    if (inst.GeomLoaded)
        return true;
    const std::string slug = inst.Slug;
    if (slug.empty())
    {
        inst.GeomLoaded = true;
        return false;
    }
    Instance full;
    if (LoadInstance(instancesDir + "\\" + slug + ".json", full) && full.MapId != 0)
    {
        full.Slug = slug;
        full.GeomLoaded = true;
        inst = std::move(full);
        return true;
    }
    inst.GeomLoaded = true; // mark attempted (never re-read a missing/bad file per frame)
    return false;
}

int LoadAllFarming(const std::string &farmingDir, std::map<uint32_t, Instance> &out)
{
    std::ifstream f(farmingDir + "\\index.json");
    if (!f)
        return 0;
    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return 0;
    }
    // Register the canonical material key -> wiki-verified display name map (build_farming.py MATERIALS) so
    // Farm::MaterialPretty shows real item names instead of a title-cased key.
    if (j.contains("taxonomy") && j["taxonomy"].is_object())
    {
        std::unordered_map<std::string, std::string> names;
        for (auto it = j["taxonomy"].begin(); it != j["taxonomy"].end(); ++it)
            if (it.value().is_string())
                names[it.key()] = it.value().get<std::string>();
        Farm::RegisterMaterialNames(names);
    }
    if (!j.contains("farming") || !j["farming"].is_array())
        return 0;

    int n = 0;
    for (const auto &e : j["farming"])
    {
        const std::string slug = JStr(e, "slug");
        if (slug.empty())
            continue;
        Instance inst;
        if (LoadFarming(farmingDir + "\\" + slug + ".json", inst) && inst.MapId != 0)
        {
            out[inst.MapId] = std::move(inst);
            ++n;
        }
    }
    return n;
}

namespace
{
    // Read a [[x1,y1],[x2,y2]] continent/map rect into a flattened float[4]. False if absent/null/malformed.
    bool ReadRect4(const json &j, const char *key, float out4[4])
    {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2)
            return false;
        const auto &r = j[key];
        if (!r[0].is_array() || !r[1].is_array() || r[0].size() < 2 || r[1].size() < 2)
            return false;
        out4[0] = r[0][0].get<float>();
        out4[1] = r[0][1].get<float>();
        out4[2] = r[1][0].get<float>();
        out4[3] = r[1][1].get<float>();
        return true;
    }

    bool LoadFishMap(const std::string &path, FishMap &out)
    {
        out = FishMap{};
        std::ifstream f(path);
        if (!f)
            return false;
        json j;
        try
        {
            f >> j;
        }
        catch (...)
        {
            return false;
        }
        try
        {
            out.MapId = (uint32_t)JInt(j, "mapId", 0);
            out.Name = JStr(j, "name");
            out.Slug = JStr(j, "slug");
            out.RegionName = JStr(j, "regionName");
            const bool c = ReadRect4(j, "continentRect", out.ContRect);
            const bool m = ReadRect4(j, "mapRect", out.MapRect);
            out.HasRects = c && m;
            if (j.contains("holes") && j["holes"].is_array())
            {
                out.Holes.reserve(j["holes"].size());
                for (const auto &h : j["holes"])
                {
                    FishHole fh;
                    fh.X = JNum(h, "x");
                    fh.Y = JNum(h, "y");
                    fh.Z = JNum(h, "z");
                    fh.Type = JStr(h, "type");
                    out.Holes.push_back(std::move(fh));
                }
            }
        }
        catch (...)
        {
            out = FishMap{};
            return false;
        }
        return out.MapId != 0 && !out.Holes.empty();
    }
}

int LoadAllFishing(const std::string &fishingDir, std::map<uint32_t, FishMap> &out)
{
    std::ifstream f(fishingDir + "\\index.json");
    if (!f)
        return 0;
    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return 0;
    }
    if (!j.contains("fishing") || !j["fishing"].is_array())
        return 0;

    int n = 0;
    for (const auto &e : j["fishing"])
    {
        const uint32_t mapId = (uint32_t)JInt(e, "mapId", 0);
        const std::string slug = JStr(e, "slug");
        if (mapId == 0 || slug.empty())
            continue;
        FishMap fm; // STUB: presence + name; holes/rects load on entry
        fm.MapId = mapId;
        fm.Slug = slug;
        fm.Name = JStr(e, "name");
        fm.GeomLoaded = false;
        out[mapId] = std::move(fm);
        ++n;
    }
    return n;
}

// Lazy-load a stub fishing map's full per-slug file (holes + rects) in place, preserving the stub's Slug.
bool LoadFishMapFull(const std::string &fishingDir, FishMap &fm)
{
    if (fm.GeomLoaded)
        return true;
    const std::string slug = fm.Slug;
    if (slug.empty())
    {
        fm.GeomLoaded = true;
        return false;
    }
    FishMap full;
    if (LoadFishMap(fishingDir + "\\" + slug + ".json", full) && full.MapId != 0)
    {
        full.Slug = slug;
        full.GeomLoaded = true;
        fm = std::move(full);
        return true;
    }
    fm.GeomLoaded = true;
    return false;
}

int LoadMapIndex(const std::string &path, std::vector<MapInfo> &out)
{
    std::ifstream f(path);
    if (!f)
        return 0;
    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return 0;
    }
    if (!j.contains("maps") || !j["maps"].is_array())
        return 0;

    int n = 0;
    for (const auto &e : j["maps"])
    {
        if (!e.is_object())
            continue;
        MapInfo mi;
        mi.MapId = (uint32_t)JInt(e, "mapId", 0);
        mi.Lounge = JBool(e, "lounge", false);
        mi.Access = JStr(e, "access");
        mi.ChatLink = JStr(e, "chatLink");
        if (auto cx = e.find("cx"); cx != e.end() && cx->is_number())
            mi.Cx = (float)cx->get<double>();
        if (auto cy = e.find("cy"); cy != e.end() && cy->is_number())
            mi.Cy = (float)cy->get<double>();
        if (auto tg = e.find("tags"); tg != e.end() && tg->is_array())
            for (const auto &t : *tg)
                if (t.is_string())
                    mi.Tags.push_back(t.get<std::string>());
        if (auto t = e.find("tile"); t != e.end() && t->is_object())
        {
            mi.HasTile = true;
            mi.TileMaxZoom = JInt(*t, "maxZoom", 7);
        }
        mi.StaticMap = JStr(e, "staticMap");
        if (mi.MapId == 0 && !mi.Lounge && mi.ChatLink.empty())
            continue; // mapId 0 only valid for a curated lounge / fractal-raid row
        mi.Name = JStr(e, "name");
        mi.Kind = JStr(e, "kind");
        mi.ContinentId = JInt(e, "continentId", 0);
        mi.RegionId = JInt(e, "regionId", 0);
        mi.RegionName = JStr(e, "regionName");
        mi.MinLevel = JInt(e, "minLevel", 0);
        mi.MaxLevel = JInt(e, "maxLevel", 0);
        auto cr = e.find("contRect");
        if (cr != e.end() && cr->is_array() && cr->size() == 4)
        {
            for (int i = 0; i < 4; ++i)
                mi.ContRect[i] = (*cr)[i].is_number() ? (float)(*cr)[i].get<double>() : 0.f;
            mi.HasRect = true;
        }
        out.push_back(std::move(mi));
        ++n;
    }
    return n;
}
