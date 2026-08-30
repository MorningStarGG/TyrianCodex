#include "StaticData.h"
#include "api/Client.h"
#include "api/v2/Currencies.h"
#include "api/v2/ItemStats.h"
#include "api/v2/Maps.h"
#include "api/v2/Materials.h"
#include "util/Json.h"   // Json::WriteAtomic (the ONE cache/settings writer)
#include "api/v2/Specializations.h"
#include "api/v2/Worlds.h"     // WvW: world/team names
#include "api/v2/Wvw.h"        // WvW: objective metadata (name/type/map/coord)
#include "util/ImageCache.h"   // currency icon prefetch
#include <algorithm>           // sort the currency catalog by display order
#include <cstdio>           // snprintf for the material-category fallback label
#include <fstream>          // staticdata.json load/save
#include <imgui.h>          // ImGui::GetTime (monotonic seconds) for the map-name retry backoff
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

// Reference-data cache. Single-threaded access: the lookups below are called from the render/compute path, and
// the endpoint .Get callbacks are drained on the SAME main thread by the shared api.Pump(), so the maps below
// need no locking (and SaveCache, called only from those callbacks + Init, is likewise main-thread).
namespace
{
    Api::Client*               g_api = nullptr;
    std::string                g_cacheFile;           // <cache>/staticdata.json ("" = no disk persistence)
    std::map<int, std::string> g_specNames;
    std::set<int>              g_specInflight;

    std::map<uint32_t, std::string> g_mapNames;       // mapId -> name ("" = fetched ok but nameless; absence = not fetched)
    std::set<uint32_t>              g_mapInflight;     // in-flight requests (ask once)
    std::map<uint32_t, double>      g_mapRetryAt;      // mapId -> earliest retry time after a failed fetch (backoff)

    std::map<int, std::string> g_matCats;             // material-storage category id -> name (whole catalog, fetched once)
    bool                       g_matRequested = false; // the one-shot GetAll has been issued (reset on failure to allow retry)

    std::map<int, std::string> g_statNames;           // itemstats (attribute-combo) id -> name (whole catalog, fetched once)
    bool                       g_statRequested = false; // the one-shot GetAll has been issued (reset on failure to allow retry)

    std::map<int, StaticData::CurrencyRef>  g_currencies;     // wallet-currency id -> {name, icon, order} (whole catalog)
    std::vector<StaticData::CurrencyRef>    g_currencyList;   // g_currencies sorted by GW2 display order (rebuilt on change)
    bool                                    g_currRequested = false; // the one-shot GetAll has been issued (reset on failure)

    // WvW reference data (for the WvW Info Panel texts + widgets)
    std::map<int, std::string>  g_worlds;            // world/team id -> name (whole catalog, one bulk fetch)
    bool                        g_worldsRequested = false;
    struct WvwObjRef { std::string name; std::string type; int mapId = 0; float cx = 0.f, cy = 0.f; };
    std::map<std::string, WvwObjRef> g_wvwObj;       // objective id ("38-6") -> {name, type, map, continent coord}
    bool                        g_wvwObjRequested = false;
    struct WvwRectRef { float rect[4] = { 0,0,0,0 }; int contId = 2; bool have = false; };
    std::map<int, WvwRectRef>   g_wvwMapRects;        // WvW map id -> continent rect (lazy per-map /v2/maps)
    std::set<int>               g_wvwMapInflight;
    std::map<int, double>       g_wvwMapRetryAt;

    // Rebuild the sorted catalog view after g_currencies changes (Init load / WarmCurrencies fetch).
    void RebuildCurrencyList()
    {
        g_currencyList.clear();
        g_currencyList.reserve(g_currencies.size());
        for (const auto& kv : g_currencies) g_currencyList.push_back(kv.second);
        std::sort(g_currencyList.begin(), g_currencyList.end(),
                  [](const StaticData::CurrencyRef& a, const StaticData::CurrencyRef& b) {
                      return a.order != b.order ? a.order < b.order : a.id < b.id;
                  });
    }
    // Disk-cache + warm the currency icons so they're ready when a surface renders (idempotent; ~25 tiny icons).
    void PrefetchCurrencyIcons()
    {
        for (const auto& kv : g_currencies)
        {
            if (kv.second.icon.empty()) continue;
            char texId[40]; std::snprintf(texId, sizeof(texId), "TC_CURRENCY_%d", kv.first);
            ImageCache::PrefetchUrl(texId, kv.second.icon.c_str());
        }
    }

    // Best-effort write of all id->name maps to staticdata.json (called whenever a fetch lands).
    void SaveCache()
    {
        if (g_cacheFile.empty()) return;
        nlohmann::json j;
        j["specs"]      = nlohmann::json::object();
        j["maps"]       = nlohmann::json::object();
        j["materials"]  = nlohmann::json::object();
        j["itemstats"]  = nlohmann::json::object();
        j["currencies"] = nlohmann::json::object();
        for (const auto& kv : g_specNames) j["specs"][std::to_string(kv.first)] = kv.second;
        for (const auto& kv : g_mapNames)  j["maps"][std::to_string(kv.first)]  = kv.second;
        for (const auto& kv : g_matCats)   j["materials"][std::to_string(kv.first)] = kv.second;
        for (const auto& kv : g_statNames) j["itemstats"][std::to_string(kv.first)] = kv.second;
        for (const auto& kv : g_currencies)
            j["currencies"][std::to_string(kv.first)] = { {"n", kv.second.name}, {"i", kv.second.icon}, {"o", kv.second.order} };
        j["worlds"]  = nlohmann::json::object();
        for (const auto& kv : g_worlds) j["worlds"][std::to_string(kv.first)] = kv.second;
        j["wvwobj"]  = nlohmann::json::object();
        for (const auto& kv : g_wvwObj)
            j["wvwobj"][kv.first] = { {"n", kv.second.name}, {"t", kv.second.type}, {"m", kv.second.mapId}, {"x", kv.second.cx}, {"y", kv.second.cy} };
        j["wvwrect"] = nlohmann::json::object();
        for (const auto& kv : g_wvwMapRects)
            if (kv.second.have)
                j["wvwrect"][std::to_string(kv.first)] = { {"l", kv.second.rect[0]}, {"t", kv.second.rect[1]}, {"r", kv.second.rect[2]}, {"b", kv.second.rect[3]}, {"c", kv.second.contId} };
        try { Json::WriteAtomic(g_cacheFile, j.dump()); } catch (...) { /* best effort */ }
    }
}

void StaticData::Shutdown()
{
    // Clear every reference map so a disable/re-enable cycle starts clean (no merge/compounding if the DLL
    // stays resident).
    g_specNames.clear(); g_specInflight.clear();
    g_mapNames.clear();  g_mapInflight.clear(); g_mapRetryAt.clear();
    g_matCats.clear();   g_matRequested  = false;
    g_statNames.clear(); g_statRequested = false;
    g_currencies.clear(); g_currencyList.clear(); g_currRequested = false;
    g_worlds.clear(); g_worldsRequested = false;
    g_wvwObj.clear(); g_wvwObjRequested = false;
    g_wvwMapRects.clear(); g_wvwMapInflight.clear(); g_wvwMapRetryAt.clear();
    g_cacheFile.clear();
    g_api = nullptr;
}

void StaticData::Init(Api::Client* api, const std::string& cacheDir)
{
    // start from a clean slate (defensive: Init may run again on re-enable without Shutdown having cleared)
    g_specNames.clear(); g_specInflight.clear();
    g_mapNames.clear();  g_mapInflight.clear(); g_mapRetryAt.clear();
    g_matCats.clear();   g_matRequested  = false;
    g_statNames.clear(); g_statRequested = false;
    g_currencies.clear(); g_currencyList.clear(); g_currRequested = false;
    g_worlds.clear(); g_worldsRequested = false;
    g_wvwObj.clear(); g_wvwObjRequested = false;
    g_wvwMapRects.clear(); g_wvwMapInflight.clear(); g_wvwMapRetryAt.clear();
    g_api = api;
    g_cacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\staticdata.json");
    if (g_cacheFile.empty()) return;
    std::ifstream f(g_cacheFile);
    if (!f) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    auto loadInt = [](const nlohmann::json& sec, std::map<int, std::string>& dst) {
        if (!sec.is_object()) return;
        for (auto it = sec.begin(); it != sec.end(); ++it)
            if (it.value().is_string()) try { dst[std::stoi(it.key())] = it.value().get<std::string>(); } catch (...) {}
    };
    loadInt(j.value("specs", nlohmann::json::object()), g_specNames);
    loadInt(j.value("materials", nlohmann::json::object()), g_matCats);
    loadInt(j.value("itemstats", nlohmann::json::object()), g_statNames);
    const nlohmann::json& maps = j.contains("maps") ? j["maps"] : nlohmann::json::object();
    if (maps.is_object())
        for (auto it = maps.begin(); it != maps.end(); ++it)
            if (it.value().is_string()) try { g_mapNames[(uint32_t)std::stoul(it.key())] = it.value().get<std::string>(); } catch (...) {}
    const nlohmann::json& curr = j.contains("currencies") ? j["currencies"] : nlohmann::json::object();
    if (curr.is_object())
        for (auto it = curr.begin(); it != curr.end(); ++it)
        {
            if (!it.value().is_object()) continue;
            try {
                int id = std::stoi(it.key());
                CurrencyRef c; c.id = id;
                c.name  = it.value().value("n", std::string());
                c.icon  = it.value().value("i", std::string());
                c.order = it.value().value("o", 0);
                g_currencies[id] = std::move(c);
            } catch (...) {}
        }
    RebuildCurrencyList();
    loadInt(j.value("worlds", nlohmann::json::object()), g_worlds);
    const nlohmann::json& wobj = j.contains("wvwobj") ? j["wvwobj"] : nlohmann::json::object();
    if (wobj.is_object())
        for (auto it = wobj.begin(); it != wobj.end(); ++it)
            if (it.value().is_object())
            {
                WvwObjRef o;
                o.name = it.value().value("n", std::string());
                o.type = it.value().value("t", std::string());
                o.mapId = it.value().value("m", 0);
                o.cx = it.value().value("x", 0.f);
                o.cy = it.value().value("y", 0.f);
                g_wvwObj[it.key()] = std::move(o);
            }
    const nlohmann::json& wrect = j.contains("wvwrect") ? j["wvwrect"] : nlohmann::json::object();
    if (wrect.is_object())
        for (auto it = wrect.begin(); it != wrect.end(); ++it)
            if (it.value().is_object())
                try {
                    WvwRectRef wr;
                    wr.rect[0] = it.value().value("l", 0.f); wr.rect[1] = it.value().value("t", 0.f);
                    wr.rect[2] = it.value().value("r", 0.f); wr.rect[3] = it.value().value("b", 0.f);
                    wr.contId = it.value().value("c", 2); wr.have = true;
                    g_wvwMapRects[std::stoi(it.key())] = wr;
                } catch (...) {}
}

const char* StaticData::SpecName(int id)
{
    if (id <= 0) return "";
    auto it = g_specNames.find(id);
    if (it != g_specNames.end()) return it->second.c_str();
    if (g_api && !g_specInflight.count(id))
    {
        g_specInflight.insert(id);
        g_api->V2().Specializations().Get(id, [id](Api::Result<Api::V2::Specialization> r) {
            g_specInflight.erase(id);
            if (r.ok) { g_specNames[id] = r.value.name; SaveCache(); }
        });
    }
    return "";
}

const char* StaticData::MapName(uint32_t id)
{
    if (id == 0) return "";
    auto it = g_mapNames.find(id);
    if (it != g_mapNames.end()) return it->second.c_str();   // cached (even an empty name -> never refetch)
    if (!g_api || g_mapInflight.count(id)) return "";
    // A previous fetch FAILED -> back off briefly instead of refetching every frame (this is polled per-frame
    // from the off-coverage header). A SUCCESS caches the name; only a transient failure is retried after the
    // backoff -- so a network blip no longer pins the fallback forever.
    auto rt = g_mapRetryAt.find(id);
    if (rt != g_mapRetryAt.end() && ImGui::GetTime() < rt->second) return "";
    g_mapInflight.insert(id);
    g_api->V2().Maps().Get((int)id, [id](Api::Result<Api::V2::Map> r) {
        g_mapInflight.erase(id);
        if (r.ok) { g_mapNames[id] = r.value.name; g_mapRetryAt.erase(id); SaveCache(); }   // fetched -> cache (even if empty)
        else      { g_mapRetryAt[id] = ImGui::GetTime() + 30.0; }                            // failed -> retry in 30s
    });
    return "";
}

const char* StaticData::MapNameOr(uint32_t id, const char* fallback)
{
    const char* n = MapName(id);
    return (n && n[0]) ? n : (fallback ? fallback : "");
}

void StaticData::WarmMaterials()
{
    if (!g_api || g_matRequested) return;   // the whole catalog is small + day-cached -> one bulk fetch warms every id
    g_matRequested = true;
    g_api->V2().Materials().GetAll([](Api::Result<std::vector<Api::V2::Material>> r) {
        if (!r.ok) { g_matRequested = false; return; }   // transient failure -> allow a retry on the next ask
        for (const Api::V2::Material& m : r.value) g_matCats[m.id] = m.name;
        SaveCache();
    });
}

std::string StaticData::MaterialCategoryName(int id)
{
    auto it = g_matCats.find(id);
    if (it != g_matCats.end() && !it->second.empty()) return it->second;
    WarmMaterials();   // safety net if a category is asked before the inventory warm ran -- normally a no-op
    char b[48]; std::snprintf(b, sizeof(b), "Category %d", id);
    return b;
}

void StaticData::WarmItemStats()
{
    if (!g_api || g_statRequested) return;   // tiny day-cached catalog -> one bulk fetch warms every id
    g_statRequested = true;
    g_api->V2().ItemStats().GetAll([](Api::Result<std::vector<Api::V2::ItemStat>> r) {
        if (!r.ok) { g_statRequested = false; return; }   // transient failure -> allow a retry on the next ask
        for (const Api::V2::ItemStat& s : r.value) g_statNames[s.id] = s.name;
        SaveCache();
    });
}

std::string StaticData::ItemStatName(int id)
{
    if (id <= 0) return "";
    auto it = g_statNames.find(id);
    if (it != g_statNames.end()) return it->second;
    WarmItemStats();   // safety net if asked before the catalog warm ran -- normally a no-op
    return "";
}

void StaticData::WarmCurrencies()
{
    PrefetchCurrencyIcons();   // warm whatever loaded from disk now (no-op cold); fetch below tops it up + revalidates
    if (!g_api || g_currRequested) return;   // small day-cached catalog -> one bulk fetch warms every id
    g_currRequested = true;
    g_api->V2().Currencies().GetAll([](Api::Result<std::vector<Api::V2::Currency>> r) {
        if (!r.ok) { g_currRequested = false; return; }   // transient failure -> allow a retry on the next ask
        for (const Api::V2::Currency& c : r.value) g_currencies[c.id] = { c.id, c.name, c.icon, c.order };
        RebuildCurrencyList();
        PrefetchCurrencyIcons();
        SaveCache();
    });
}

const char* StaticData::CurrencyName(int id)
{
    auto it = g_currencies.find(id);
    if (it != g_currencies.end()) return it->second.name.c_str();
    WarmCurrencies();   // safety net if asked before the catalog warm ran -- normally a no-op
    return "";
}

const char* StaticData::CurrencyIcon(int id)
{
    auto it = g_currencies.find(id);
    if (it != g_currencies.end()) return it->second.icon.c_str();
    WarmCurrencies();
    return "";
}

const std::vector<StaticData::CurrencyRef>& StaticData::Currencies()
{
    if (g_currencyList.empty()) WarmCurrencies();   // safety net; returns empty until the catalog lands
    return g_currencyList;
}

void StaticData::WarmWorlds()
{
    if (!g_api || g_worldsRequested) return;   // small day-cached catalog -> one bulk fetch warms every id
    g_worldsRequested = true;
    g_api->V2().Worlds().GetAll([](Api::Result<std::vector<Api::V2::World>> r) {
        if (!r.ok) { g_worldsRequested = false; return; }   // transient failure -> allow a retry on the next ask
        for (const Api::V2::World& w : r.value) g_worlds[w.id] = w.name;
        SaveCache();
    });
}

const char* StaticData::WorldName(int id)
{
    auto it = g_worlds.find(id);
    if (it != g_worlds.end()) return it->second.c_str();
    WarmWorlds();   // safety net if asked before the warm ran -- normally a no-op
    return "";
}

void StaticData::WarmWvwObjectives()
{
    if (!g_api || g_wvwObjRequested) return;   // a few hundred static entries -> one bulk fetch warms them all
    g_wvwObjRequested = true;
    g_api->V2().Wvw().Objectives().GetAll([](Api::Result<std::vector<Api::V2::WvwObjective>> r) {
        if (!r.ok) { g_wvwObjRequested = false; return; }
        for (const Api::V2::WvwObjective& o : r.value)
        {
            WvwObjRef ref; ref.name = o.name; ref.type = o.type; ref.mapId = o.mapId;
            if (o.raw.contains("coord") && o.raw["coord"].is_array() && o.raw["coord"].size() >= 2 &&
                o.raw["coord"][0].is_number() && o.raw["coord"][1].is_number())
            { ref.cx = o.raw["coord"][0].get<float>(); ref.cy = o.raw["coord"][1].get<float>(); }
            g_wvwObj[o.id] = std::move(ref);
        }
        SaveCache();
    });
}

const char* StaticData::ObjectiveName(const std::string& id)
{
    auto it = g_wvwObj.find(id);
    if (it != g_wvwObj.end()) return it->second.name.c_str();
    WarmWvwObjectives();
    return "";
}

bool StaticData::ObjectiveCoord(const std::string& id, float& cx, float& cy)
{
    auto it = g_wvwObj.find(id);
    if (it == g_wvwObj.end()) { WarmWvwObjectives(); return false; }
    cx = it->second.cx; cy = it->second.cy;
    return (cx != 0.f || cy != 0.f);
}

bool StaticData::WvwMapRect(int mapId, float rect[4], int& continentId)
{
    auto it = g_wvwMapRects.find(mapId);
    if (it != g_wvwMapRects.end())
    {
        if (!it->second.have) return false;
        for (int i = 0; i < 4; ++i) rect[i] = it->second.rect[i];
        continentId = it->second.contId;
        return true;
    }
    if (!g_api || g_wvwMapInflight.count(mapId)) return false;
    auto rt = g_wvwMapRetryAt.find(mapId);
    if (rt != g_wvwMapRetryAt.end() && ImGui::GetTime() < rt->second) return false;
    g_wvwMapInflight.insert(mapId);
    g_api->V2().Maps().Get(mapId, [mapId](Api::Result<Api::V2::Map> r) {
        g_wvwMapInflight.erase(mapId);
        const nlohmann::json* cr = (r.ok && r.value.raw.contains("continent_rect")) ? &r.value.raw["continent_rect"] : nullptr;
        if (cr && cr->is_array() && cr->size() >= 2 && (*cr)[0].is_array() && (*cr)[1].is_array() &&
            (*cr)[0].size() >= 2 && (*cr)[1].size() >= 2)
        {
            WvwRectRef wr;
            wr.rect[0] = (*cr)[0][0].get<float>(); wr.rect[1] = (*cr)[0][1].get<float>();
            wr.rect[2] = (*cr)[1][0].get<float>(); wr.rect[3] = (*cr)[1][1].get<float>();
            wr.contId = r.value.continentId ? r.value.continentId : 2; wr.have = true;
            g_wvwMapRects[mapId] = wr; g_wvwMapRetryAt.erase(mapId); SaveCache();
        }
        else { g_wvwMapRetryAt[mapId] = ImGui::GetTime() + 30.0; }   // failed -> retry in 30s
    });
    return false;
}
