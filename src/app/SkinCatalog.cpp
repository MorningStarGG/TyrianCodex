#include "app/SkinCatalog.h"
#include "api/Client.h"
#include "api/v2/Skins.h"
#include "util/Json.h"   // Json::WriteAtomic (the ONE cache/settings writer)

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
    namespace fs = std::filesystem;

    Api::Client *g_api = nullptr;
    std::string g_seedFile;            // <addon>\data\skins.json   (bundled JSON seed, read-only)
    std::string g_deltaFile;           // <addon>\cache\skins-delta.json  (writable: ONLY the post-seed runtime items, NOT a full copy)
    std::unordered_set<int> g_seedIds; // ids in the bundled seed -> the delta cache persists only NON-seed items
    std::vector<SkinCatalog::Skin> g_items;
    std::unordered_map<int, int> g_index; // id -> index in g_items
    long long g_fetchedUtc = 0;
    bool g_loaded = false;
    bool g_requested = false;
    bool g_refreshing = false;
    uint64_t g_version = 1;

    constexpr int kMinSane = 1000; // a sane catalog has thousands of skins; guard against partial/empty writes

    SkinCatalog::Skin g_empty;

    void BuildKey(SkinCatalog::Skin &s)
    {
        std::string k = s.name + " " + s.type + " " + s.subtype + " " + s.rarity;
        for (char &c : k)
            c = (char)std::tolower((unsigned char)c);
        s.keyLower = std::move(k);
    }

    void RebuildIndex()
    {
        g_index.clear();
        g_index.reserve(g_items.size() * 2);
        for (int i = 0; i < (int)g_items.size(); ++i)
            g_index[g_items[i].id] = i;
    }

    SkinCatalog::Skin FromApi(const Api::V2::Skin &s)
    {
        SkinCatalog::Skin o;
        o.id = s.id;
        o.name = s.name;
        o.icon = s.icon;
        o.rarity = s.rarity;
        o.type = s.type;
        o.flags = s.flags;
        if (s.raw.is_object())
        {
            auto d = s.raw.find("details");
            if (d != s.raw.end() && d->is_object())
            {
                o.subtype = d->value("type", std::string());
                o.weight = d->value("weight_class", std::string());
            }
        }
        for (const std::string &f : o.flags)
            if (f == "ShowInWardrobe")
            {
                o.wardrobe = true;
                break;
            }
        BuildKey(o);
        return o;
    }

    bool LoadFile(const std::string &path)
    {
        if (path.empty())
            return false;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string bytes = ss.str();
        if (bytes.empty())
            return false;
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(bytes); // JSON seed + cache (CBOR fully dropped)
        }
        catch (...)
        {
            return false;
        }
        if (!j.is_object())
            return false;
        auto arr = j.find("skins");
        if (arr == j.end() || !arr->is_array())
            return false;

        std::vector<SkinCatalog::Skin> out;
        out.reserve(arr->size());
        for (const auto &e : *arr)
        {
            if (!e.is_object())
                continue;
            SkinCatalog::Skin s;
            s.id = e.value("id", 0);
            s.name = e.value("name", std::string());
            s.icon = e.value("icon", std::string());
            s.rarity = e.value("rarity", std::string());
            s.type = e.value("type", std::string());
            s.subtype = e.value("subtype", std::string());
            s.weight = e.value("weight", std::string());
            if (e.contains("flags") && e["flags"].is_array())
                for (const auto &x : e["flags"])
                    if (x.is_string())
                        s.flags.push_back(x.get<std::string>());
            if (s.id <= 0 || s.name.empty())
                continue;
            for (const std::string &fl : s.flags)
                if (fl == "ShowInWardrobe")
                {
                    s.wardrobe = true;
                    break;
                }
            BuildKey(s);
            out.push_back(std::move(s));
        }
        if (out.empty())
            return false;
        g_items = std::move(out);
        g_fetchedUtc = j.value("fetchedUtc", (long long)0);
        return true;
    }

    // Append the runtime-added (non-seed) skins from the small delta cache -- the seed is loaded separately + read-only.
    void LoadDelta(const std::string &path)
    {
        if (path.empty())
            return;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return;
        std::ostringstream ss;
        ss << f.rdbuf();
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(ss.str());
        }
        catch (...)
        {
            return;
        }
        auto arr = j.find("skins");
        if (arr == j.end() || !arr->is_array())
            return;
        for (const auto &e : *arr)
        {
            if (!e.is_object())
                continue;
            SkinCatalog::Skin s;
            s.id = e.value("id", 0);
            s.name = e.value("name", std::string());
            s.icon = e.value("icon", std::string());
            s.rarity = e.value("rarity", std::string());
            s.type = e.value("type", std::string());
            s.subtype = e.value("subtype", std::string());
            s.weight = e.value("weight", std::string());
            if (e.contains("flags") && e["flags"].is_array())
                for (const auto &x : e["flags"])
                    if (x.is_string())
                        s.flags.push_back(x.get<std::string>());
            if (s.id <= 0 || s.name.empty() || g_seedIds.count(s.id))
                continue; // skip empties + anything already in the seed
            for (const std::string &fl : s.flags)
                if (fl == "ShowInWardrobe")
                {
                    s.wardrobe = true;
                    break;
                }
            BuildKey(s);
            g_items.push_back(std::move(s));
        }
    }

    // Persist ONLY the runtime-added (non-seed) skins, so the cache stays tiny (a few new skins after a GW2 patch)
    // instead of duplicating the whole bundled catalog.
    void SaveDelta()
    {
        if (g_deltaFile.empty())
            return;
        nlohmann::json arr = nlohmann::json::array();
        for (const SkinCatalog::Skin &s : g_items)
            if (!g_seedIds.count(s.id))
                arr.push_back({{"id", s.id}, {"name", s.name}, {"icon", s.icon}, {"rarity", s.rarity}, {"type", s.type}, {"subtype", s.subtype}, {"weight", s.weight}, {"flags", s.flags}});
        nlohmann::json j;
        j["schema"] = 1;
        j["fetchedUtc"] = g_fetchedUtc;
        j["count"] = (int)arr.size();
        j["skins"] = std::move(arr);
        try { Json::WriteAtomic(g_deltaFile, j.dump()); }
        catch (...)
        { /* best effort */
        }
    }

    // Overlay armor SET names from data/skin_sets.json ({ "<id>": "<set>" }) onto the loaded skins. Built by
    // build_skin_sets.py (the wiki `| set =` field); absent / partial = no-op (sets are optional).
    void ApplySets(const std::string &dataDir)
    {
        if (dataDir.empty())
            return;
        std::ifstream f(dataDir + "\\skin_sets.json", std::ios::binary);
        if (!f)
            return;
        std::ostringstream ss;
        ss << f.rdbuf();
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(ss.str());
        }
        catch (...)
        {
            return;
        }
        if (!j.is_object())
            return;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_string())
                continue;
            int id = 0;
            try
            {
                id = std::stoi(it.key());
            }
            catch (...)
            {
                continue;
            }
            auto idx = g_index.find(id);
            if (idx != g_index.end())
                g_items[idx->second].set = it.value().get<std::string>();
        }
    }

    // Overlay the premium (gem-store-only) flag from data/skin_premium.json ({ "ids": [...] }, build_premium.py):
    // a wardrobe skin with NO in-game route (gold/TP/vendor/...)
    void LoadPremium(const std::string &dataDir)
    {
        if (dataDir.empty())
            return;
        std::ifstream f(dataDir + "\\skin_premium.json", std::ios::binary);
        if (!f)
            return;
        std::ostringstream ss;
        ss << f.rdbuf();
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(ss.str());
        }
        catch (...)
        {
            return;
        }
        auto arr = j.find("ids");
        if (arr == j.end() || !arr->is_array())
            return;
        for (const auto &e : *arr)
        {
            if (!e.is_number_integer())
                continue;
            auto idx = g_index.find(e.get<int>());
            if (idx != g_index.end())
                g_items[idx->second].premium = true;
        }
    }

    std::unordered_map<int, SkinCatalog::Anim> g_anim; // id -> sprite-sheet animation (data/skin_anim.json)

    void LoadAnim(const std::string &dataDir)
    {
        g_anim.clear();
        if (dataDir.empty())
            return;
        std::ifstream f(dataDir + "\\skin_anim.json", std::ios::binary);
        if (!f)
            return;
        std::ostringstream ss;
        ss << f.rdbuf();
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(ss.str());
        }
        catch (...)
        {
            return;
        }
        if (!j.is_object())
            return;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_object())
                continue;
            int id = 0;
            try
            {
                id = std::stoi(it.key());
            }
            catch (...)
            {
                continue;
            }
            SkinCatalog::Anim a;
            a.frames = it.value().value("frames", 0);
            a.w = it.value().value("w", 0);
            a.h = it.value().value("h", 0);
            if (it.value().contains("delayMs") && it.value()["delayMs"].is_array())
                for (const auto &d : it.value()["delayMs"])
                    if (d.is_number_integer())
                        a.delayMs.push_back(d.get<int>());
            if (a.frames > 0)
                g_anim[id] = std::move(a);
        }
    }
}

void SkinCatalog::Init(Api::Client *api, const std::string &dataDir, const std::string &cacheDir)
{
    g_api = api;
    g_seedFile = dataDir.empty() ? std::string() : (dataDir + "\\skins.json");
    g_deltaFile = cacheDir.empty() ? std::string() : (cacheDir + "\\skins-delta.json");
    if (!cacheDir.empty())
    {
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
    }

    g_items.clear();
    g_index.clear();
    g_seedIds.clear();
    g_fetchedUtc = 0;
    g_loaded = false;
    g_requested = false;
    g_refreshing = false;
    LoadFile(g_seedFile); // bundled read-only seed -> g_items
    for (const Skin &s : g_items)
        g_seedIds.insert(s.id);
    LoadDelta(g_deltaFile); // append ONLY the runtime-added (non-seed) skins
    RebuildIndex();
    g_loaded = !g_items.empty();
    if (!cacheDir.empty()) // one-time: drop the legacy full-catalog cache + pre-JSON .cbor
    {
        std::error_code ec;
        fs::remove(cacheDir + "\\skins.json", ec);
        fs::remove(cacheDir + "\\skins.cbor", ec);
    }
    ApplySets(dataDir);   // overlay armor set names (data/skin_sets.json) once the id index exists
    LoadPremium(dataDir); // overlay the gem-store-only premium flag (data/skin_premium.json)
    LoadAnim(dataDir);    // animated-render sprite-sheet manifest (data/skin_anim.json)
    ++g_version;
}

int SkinCatalog::Anim::FrameAt(double seconds) const
{
    if (frames <= 0)
        return 0;
    int total = 0;
    for (int d : delayMs)
        total += d;
    if (total <= 0)
        return 0;
    int ms = (int)(seconds * 1000.0) % total;
    int acc = 0;
    for (int i = 0; i < frames && i < (int)delayMs.size(); ++i)
    {
        acc += delayMs[i];
        if (ms < acc)
            return i;
    }
    return frames - 1;
}

const SkinCatalog::Anim *SkinCatalog::AnimFor(int id)
{
    auto it = g_anim.find(id);
    return it == g_anim.end() ? nullptr : &it->second;
}

void SkinCatalog::Shutdown()
{
    g_api = nullptr;
    g_seedFile.clear();
    g_deltaFile.clear();
    g_seedIds.clear();
    std::vector<SkinCatalog::Skin>().swap(g_items);
    g_index.clear();
    g_anim.clear();
    g_fetchedUtc = 0;
    g_loaded = false;
    g_requested = false;
    g_refreshing = false;
    ++g_version;
}

void SkinCatalog::Warm()
{
    if (!g_api || g_requested || g_refreshing)
        return;
    g_requested = true;
    g_refreshing = true;
    // Diff the live id list against what we have; the RequestSplitter chunks a large Get() into 200s + merges.
    g_api->V2().Skins().GetIds([](Api::Result<std::vector<int>> r)
                               {
        if (!r.ok || (int)r.value.size() < kMinSane) { g_refreshing = false; g_requested = false; return; }
        std::vector<int> missing;
        for (int id : r.value) if (id > 0 && g_index.find(id) == g_index.end()) missing.push_back(id);
        if (missing.empty()) { g_refreshing = false; g_loaded = true; return; }   // up to date (seed covers everything)
        g_api->V2().Skins().Get(missing, [](Api::Result<std::vector<Api::V2::Skin>> rr) {
            g_refreshing = false;
            if (!rr.ok || rr.value.empty()) return;
            bool added = false;
            for (const Api::V2::Skin& s : rr.value)
            {
                if (s.id <= 0 || s.name.empty() || g_index.find(s.id) != g_index.end()) continue;
                g_index[s.id] = (int)g_items.size();
                g_items.push_back(FromApi(s));
                added = true;
            }
            if (!added) return;
            g_fetchedUtc = (long long)std::time(nullptr);
            g_loaded = true;
            ++g_version;
            SaveDelta();
        }); });
}

bool SkinCatalog::IsReady() { return g_loaded; }
bool SkinCatalog::Refreshing() { return g_refreshing; }
int SkinCatalog::Count() { return (int)g_items.size(); }
uint64_t SkinCatalog::Version() { return g_version; }
const std::vector<SkinCatalog::Skin> &SkinCatalog::Items() { return g_items; }

const SkinCatalog::Skin &SkinCatalog::ById(int id)
{
    auto it = g_index.find(id);
    return (it != g_index.end()) ? g_items[it->second] : g_empty;
}
