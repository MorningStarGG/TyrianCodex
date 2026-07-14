#include "ItemCatalog.h"
#include "StaticData.h"
#include "Version.h"               // TC_VERSION_STRING -- version-keyed seed re-load marker (mirrors tiles)
#include "api/Client.h"
#include "api/v2/Build.h"
#include "api/v2/Items.h"
#include "ui/items/ItemScore.h"   // ItemUI::Lower / QueryTerms / TokenFieldScore

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Mostly single-threaded: every Api callback is drained on the main thread by the shared api.Pump(), and the
// render path calls Query/Types/At on that same thread -- so g_items + fetch state need no locking. The ONE
// exception is the startup load: it runs on a background thread (so AddonLoad doesn't block on a ~40 MB parse)
// and hands its result to the main thread via the g_loadDone atomic + PublishIfReady() (the main thread is the
// only one that ever touches g_items).
namespace
{
    Api::Client* g_api = nullptr;
    std::string  g_seedFile;     // <addon>\data\items.json        (bundled JSON seed, read-only)
    std::string  g_deltaFile;    // <addon>\cache\items-delta.json (writable: ONLY runtime-added items, NOT a full copy)
    std::unordered_set<int> g_seedIds;       // ids in the bundled seed -> the delta cache persists only non-seed items
    std::unordered_set<int> g_loadSeedIds;   // bg-load handoff of the seed ids (-> g_seedIds on publish)

    std::vector<ItemCatalog::Item>     g_items;
    std::unordered_map<int, int>       g_idIndex;     // item id -> index in g_items
    uint64_t                           g_version = 0;
    int                                g_buildId = 0;
    long long                          g_fetchedUtc = 0;

    ItemCatalog::ProgressInfo g_prog;

    // ---- async startup load (background parse -> main-thread publish) ----
    std::thread       g_loadThread;
    std::atomic<bool> g_loadDone{ false };   // bg parse finished filling g_loadResult (release/acquire handoff)
    std::vector<ItemCatalog::Item> g_loadResult;
    int               g_loadBuild = 0;
    long long         g_loadUtc = 0;
    bool              g_loadOk = false;
    bool              g_published = false;    // main: g_loadResult has been moved into g_items
    bool              g_warmPending = false;  // main: Warm() was called before the load published

    // ---- fetch driver (one at a time) ----
    std::vector<ItemCatalog::Item> g_fetchAccum;
    std::vector<int>               g_fetchStatId;     // parallel to g_fetchAccum: infix stat id to resolve at finalize
    int   g_fetchChunks = 0, g_fetchDone = 0;
    bool  g_fetchAppend = false;
    int   g_targetBuild = 0;

    const ItemCatalog::Item g_empty{};

    std::string JoinLower(const std::vector<std::string>& v)
    {
        std::string s; for (const std::string& x : v) { if (!s.empty()) s.push_back(' '); s += x; } return ItemUI::Lower(s);
    }

    void BuildKey(ItemCatalog::Item& it)
    {
        std::string k = it.name + " " + it.type + " " + it.subtype + " " + it.rarity + " " + it.statSet + " "
                      + it.description + " " + JoinLower(it.flags) + " " + JoinLower(it.restrictions) + " " + std::to_string(it.id);
        it.keyLower = ItemUI::Lower(k);
    }

    void RebuildIndex()
    {
        g_idIndex.clear();
        g_idIndex.reserve(g_items.size() * 2);
        for (int i = 0; i < (int)g_items.size(); ++i) g_idIndex[g_items[i].id] = i;
    }

    // Best-effort write of ONLY the runtime-added (non-seed) items -- the bundled seed is read-only, so the cache
    // stays tiny (the post-patch delta) instead of duplicating the whole ~40 MB catalog. `details` = compact JSON.
    void SaveDelta()
    {
        if (g_deltaFile.empty()) return;
        nlohmann::json j;
        j["schema"]     = 1;
        j["buildId"]    = g_buildId;
        j["fetchedUtc"] = g_fetchedUtc;
        nlohmann::json arr = nlohmann::json::array();
        for (const ItemCatalog::Item& it : g_items)
        {
            if (g_seedIds.count(it.id)) continue;   // seed items are read from data\items.json, not duplicated here
            nlohmann::json e;
            e["id"] = it.id; e["name"] = it.name; e["icon"] = it.icon; e["rarity"] = it.rarity;
            e["type"] = it.type; e["subtype"] = it.subtype; e["level"] = it.level; e["vendor"] = it.vendorValue;
            e["chat"] = it.chatLink; e["desc"] = it.description; e["stat"] = it.statSet;
            e["flags"] = it.flags; e["restrictions"] = it.restrictions;
            e["details"] = it.detailsRaw;
            arr.push_back(std::move(e));
        }
        j["count"] = (int)arr.size();
        j["items"] = std::move(arr);
        try
        {
            const std::string text = j.dump();
            std::ofstream(g_deltaFile, std::ios::binary).write(text.data(), (std::streamsize)text.size());
        }
        catch (...) { /* best effort */ }
    }

    // Parse one /v2/items entry into a catalog Item (statSet resolved later, in FinalizeFetch).
    ItemCatalog::Item ParseFromApi(const Api::V2::Item& a, int& statIdOut)
    {
        ItemCatalog::Item it;
        it.id = a.id; it.name = a.name; it.icon = a.icon; it.rarity = a.rarity; it.type = a.type; it.chatLink = a.chatLink;
        statIdOut = 0;
        const nlohmann::json& j = a.raw;
        if (j.is_object())
        {
            it.level       = j.value("level", 0);
            it.vendorValue = j.value("vendor_value", 0);
            it.description = j.value("description", std::string());
            if (j.contains("flags") && j["flags"].is_array())
                for (const auto& f : j["flags"]) if (f.is_string()) it.flags.push_back(f.get<std::string>());
            if (j.contains("restrictions") && j["restrictions"].is_array())
                for (const auto& f : j["restrictions"]) if (f.is_string()) it.restrictions.push_back(f.get<std::string>());
            auto d = j.find("details");
            if (d != j.end() && d->is_object())
            {
                it.detailsRaw = d->dump();
                it.subtype = d->value("type", std::string());
                auto sc = d->find("stat_choices");
                if (sc != d->end() && sc->is_array() && !sc->empty()) it.statSet = "Selectable";
                else
                {
                    auto infix = d->find("infix_upgrade");
                    if (infix != d->end() && infix->is_object()) statIdOut = infix->value("id", 0);
                }
            }
        }
        return it;
    }

    void FinalizeFetch()
    {
        // resolve stat-set names (itemstats was warmed at fetch start) + build the search key for each new item
        for (size_t i = 0; i < g_fetchAccum.size(); ++i)
        {
            ItemCatalog::Item& it = g_fetchAccum[i];
            if (it.statSet.empty() && i < g_fetchStatId.size() && g_fetchStatId[i] > 0)
                it.statSet = StaticData::ItemStatName(g_fetchStatId[i]);
            BuildKey(it);
        }
        if (g_fetchAppend)
        {
            for (ItemCatalog::Item& it : g_fetchAccum)
            {
                auto f = g_idIndex.find(it.id);
                if (f != g_idIndex.end()) g_items[f->second] = std::move(it);
                else { g_idIndex[it.id] = (int)g_items.size(); g_items.push_back(std::move(it)); }
            }
        }
        else
        {
            g_items = std::move(g_fetchAccum);
            RebuildIndex();
        }
        g_fetchAccum.clear(); g_fetchStatId.clear();
        if (g_targetBuild > 0) g_buildId = g_targetBuild;
        g_fetchedUtc = (long long)std::time(nullptr);
        ++g_version;
        g_prog.active = false;
        SaveDelta();
    }

    void RunFetch(const std::vector<int>& ids, bool append)
    {
        if (!g_api) { g_prog.active = false; return; }
        if (ids.empty()) { g_prog.active = false; return; }   // nothing new
        StaticData::WarmItemStats();   // tiny; resolves stat-set names by the time chunks finalize
        g_fetchAccum.clear(); g_fetchAccum.reserve(ids.size());
        g_fetchStatId.clear(); g_fetchStatId.reserve(ids.size());
        g_fetchAppend = append;
        g_fetchDone = 0;
        g_prog = ItemCatalog::ProgressInfo{ true, 0, (int)ids.size() };

        std::vector<std::vector<int>> chunks;
        for (size_t i = 0; i < ids.size(); i += 200)
            chunks.emplace_back(ids.begin() + i, ids.begin() + std::min(ids.size(), i + 200));
        g_fetchChunks = (int)chunks.size();

        for (const std::vector<int>& ch : chunks)
            g_api->V2().Items().Get(ch, [](Api::Result<std::vector<Api::V2::Item>> r) {
                if (r.ok)
                    for (const Api::V2::Item& a : r.value)
                    {
                        if (a.name.empty()) continue;   // skip internal/unnamed items (not browsable)
                        int statId = 0;
                        g_fetchAccum.push_back(ParseFromApi(a, statId));
                        g_fetchStatId.push_back(statId);
                    }
                g_prog.fetched = (int)g_fetchAccum.size();
                if (++g_fetchDone >= g_fetchChunks) FinalizeFetch();
            });
    }

    void StartFullFetch()
    {
        if (!g_api || g_prog.active) return;
        g_prog = ItemCatalog::ProgressInfo{ true, 0, 0 };
        g_targetBuild = 0;
        g_api->V2().Build().Get([](Api::Result<int> r) { if (r.ok) g_targetBuild = r.value; });
        g_api->V2().Items().GetIds([](Api::Result<std::vector<int>> r) {
            if (!r.ok || r.value.empty()) { g_prog.active = false; return; }
            RunFetch(r.value, /*append*/ false);
        });
    }

    void StartDeltaFetch(int newBuild)
    {
        if (!g_api || g_prog.active) return;
        g_targetBuild = newBuild;
        g_prog = ItemCatalog::ProgressInfo{ true, 0, 0 };
        g_api->V2().Items().GetIds([](Api::Result<std::vector<int>> r) {
            if (!r.ok) { g_prog.active = false; return; }
            std::vector<int> need;
            for (int id : r.value) if (!g_idIndex.count(id)) need.push_back(id);
            if (need.empty()) { g_buildId = g_targetBuild; g_fetchedUtc = (long long)std::time(nullptr); g_prog.active = false; SaveDelta(); return; }
            RunFetch(need, /*append*/ true);
        });
    }

    // Parse a catalog file into a LOCAL vector (no global mutation) so it can run on the background load
    // thread. Returns the items + header fields by out-param. Caller publishes into g_items on the main thread.
    bool LoadFile(const std::string& path, std::vector<ItemCatalog::Item>& out, int& buildId, long long& fetchedUtc,
                  bool append = false, const std::unordered_set<int>* skip = nullptr)
    {
        if (path.empty()) return false;
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss; ss << f.rdbuf();
        const std::string bytes = ss.str();
        if (bytes.empty()) return false;
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(bytes);   // JSON seed + delta (CBOR fully dropped)
        }
        catch (...) { return false; }
        if (!j.is_object()) return false;
        auto arr = j.find("items");
        if (arr == j.end() || !arr->is_array()) return false;
        if (!append) out.clear();
        out.reserve(out.size() + arr->size());
        if (!append) { buildId = j.value("buildId", 0); fetchedUtc = j.value("fetchedUtc", (long long)0); }
        else { if (int b = j.value("buildId", 0)) buildId = b; if (long long u = j.value("fetchedUtc", (long long)0)) fetchedUtc = u; }
        for (const auto& e : *arr)
        {
            if (!e.is_object()) continue;
            ItemCatalog::Item it;
            it.id          = e.value("id", 0);
            it.name        = e.value("name", std::string());
            it.icon        = e.value("icon", std::string());
            it.rarity      = e.value("rarity", std::string());
            it.type        = e.value("type", std::string());
            it.subtype     = e.value("subtype", std::string());
            it.level       = e.value("level", 0);
            it.vendorValue = e.value("vendor", 0);
            it.chatLink    = e.value("chat", std::string());
            it.description = e.value("desc", std::string());
            it.statSet     = e.value("stat", std::string());
            if (e.contains("flags") && e["flags"].is_array())
                for (const auto& x : e["flags"]) if (x.is_string()) it.flags.push_back(x.get<std::string>());
            if (e.contains("restrictions") && e["restrictions"].is_array())
                for (const auto& x : e["restrictions"]) if (x.is_string()) it.restrictions.push_back(x.get<std::string>());
            auto d = e.find("details");
            if (d != e.end() && d->is_object()) it.detailsRaw = d->dump();
            else if (d != e.end() && d->is_string()) it.detailsRaw = d->get<std::string>();
            if (it.id <= 0 || it.name.empty() || (skip && skip->count(it.id))) continue;   // skip internal/unnamed + (delta) seed dups
            BuildKey(it);
            out.push_back(std::move(it));
        }
        return !out.empty();
    }

    // Kick the startup parse onto a background thread (cache first, else the bundled seed). g_seedFile /
    // g_deltaFile are set before this and never mutated after, so the thread reads them safely.
    void StartAsyncLoad()
    {
        g_published = false; g_loadOk = false; g_loadDone.store(false);
        g_prog = ItemCatalog::ProgressInfo{ true, 0, 0 };   // show the ring while the parse runs
        g_loadThread = std::thread([] {
            std::vector<ItemCatalog::Item> out; int bid = 0; long long utc = 0;
            bool ok = LoadFile(g_seedFile, out, bid, utc);           // bundled read-only seed
            std::unordered_set<int> seedIds; seedIds.reserve(out.size() * 2);
            for (const ItemCatalog::Item& it : out) seedIds.insert(it.id);
            LoadFile(g_deltaFile, out, bid, utc, /*append*/ true, &seedIds);   // + ONLY the runtime-added (non-seed) items
            g_loadResult = std::move(out); g_loadSeedIds = std::move(seedIds);
            g_loadBuild = bid; g_loadUtc = utc; g_loadOk = ok || !g_loadResult.empty();
            g_loadDone.store(true);   // release: publishes the writes above to the main thread
        });
    }

    void DoWarm()   // the build-id freshness check (runs on the main thread once the catalog is loaded)
    {
        if (!g_api) return;
        if (g_items.empty()) { StartFullFetch(); return; }   // no seed + no cache -> build from scratch
        g_api->V2().Build().Get([](Api::Result<int> r) {
            if (!r.ok) return;
            if (g_buildId != 0 && r.value == g_buildId) return;   // unchanged -> trust disk
            StartDeltaFetch(r.value);
        });
    }

    // Main thread: once the background parse is done, move its result into g_items (so all later access stays
    // lockless on the main thread). Called at the top of every public accessor.
    void PublishIfReady()
    {
        if (g_published || !g_loadDone.load()) return;
        if (g_loadThread.joinable()) g_loadThread.join();    // finished -> returns immediately
        if (g_loadOk) { g_items = std::move(g_loadResult); g_buildId = g_loadBuild; g_fetchedUtc = g_loadUtc; }
        g_seedIds = std::move(g_loadSeedIds);
        std::vector<ItemCatalog::Item>().swap(g_loadResult);
        RebuildIndex();
        ++g_version;
        g_published = true;
        g_prog.active = false;   // load finished (a fetch below may turn it back on)
        if (g_warmPending) { g_warmPending = false; DoWarm(); }
    }
}

void ItemCatalog::Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir)
{
    g_api = api;
    g_seedFile  = dataDir.empty()  ? std::string() : (dataDir  + "\\items.json");
    g_deltaFile = cacheDir.empty() ? std::string() : (cacheDir + "\\items-delta.json");
    if (!cacheDir.empty())
    {
        std::error_code ec; std::filesystem::create_directories(cacheDir, ec);
        { std::error_code e0; std::filesystem::remove(cacheDir + "\\items.json", e0); std::filesystem::remove(cacheDir + "\\items.cbor", e0); }   // drop the legacy full-catalog cache
        // Version-keyed delta reset: a new addon version ships a refreshed data\items.json (the seed is always read
        // fresh now), so drop the small delta too -- a stale entry can't shadow the updated seed. Warm()'s /v2/build
        // delta then re-tops anything the live API added since the new bundle.
        const std::string marker = cacheDir + "\\items.version";
        std::string cur; { std::ifstream f(marker, std::ios::binary); if (f) { std::ostringstream ss; ss << f.rdbuf(); cur = ss.str(); } }
        if (cur != TC_VERSION_STRING)
        {
            std::error_code e2; std::filesystem::remove(g_deltaFile, e2);
            std::ofstream(marker, std::ios::binary) << TC_VERSION_STRING;
        }
    }
    StartAsyncLoad();   // parse off the load thread so AddonLoad doesn't block; the ring shows until it lands
}

void ItemCatalog::Warm()
{
    PublishIfReady();
    if (!g_published) { g_warmPending = true; return; }   // load still in flight -> warm once it publishes
    DoWarm();
}

void ItemCatalog::Rebuild() { PublishIfReady(); StartFullFetch(); }

int                       ItemCatalog::Count()    { PublishIfReady(); return (int)g_items.size(); }
uint64_t                  ItemCatalog::Version()  { PublishIfReady(); return g_version; }
ItemCatalog::ProgressInfo ItemCatalog::Progress() { PublishIfReady(); return g_prog; }

int ItemCatalog::RarityRank(const std::string& r)
{
    if (r == "Junk") return 0;        if (r == "Basic") return 1;     if (r == "Fine") return 2;
    if (r == "Masterwork") return 3;  if (r == "Rare") return 4;      if (r == "Exotic") return 5;
    if (r == "Ascended") return 6;    if (r == "Legendary") return 7;
    return -1;
}

const ItemCatalog::Item& ItemCatalog::At(int index)
{
    PublishIfReady();
    return (index >= 0 && index < (int)g_items.size()) ? g_items[index] : g_empty;
}

const ItemCatalog::Item& ItemCatalog::ById(int id)
{
    PublishIfReady();
    auto it = g_idIndex.find(id);
    return it != g_idIndex.end() ? g_items[it->second] : g_empty;
}

// ---- filtered + sorted query (cached behind a change token) ----
namespace
{
    struct QCache
    {
        std::vector<int> out;
        bool have = false; uint64_t ver = (uint64_t)-1;
        std::string text, type, subtype; unsigned rarity = 0; int lvMin = 0, lvMax = 80;
        ItemCatalog::Sort sort = ItemCatalog::Sort::Name; bool asc = true;
    };
    QCache g_q;

    int ScoreItem(const ItemCatalog::Item& it, const std::vector<std::string>& terms)
    {
        const std::string nameLower = ItemUI::Lower(it.name);
        int score = 0;
        for (const std::string& t : terms)
        {
            int best = ItemUI::TokenFieldScore(nameLower, t, 1400, 1250, 1000);
            if (best <= 0) best = 50;   // matched elsewhere in keyLower (filter already guaranteed it)
            score += best;
        }
        return score;
    }
}

const std::vector<int>& ItemCatalog::Query(const char* text, const std::string& type, const std::string& subtype,
                                           unsigned rarityMask, int lvMin, int lvMax, Sort sort, bool asc)
{
    PublishIfReady();
    const std::string textStr = text ? text : "";
    const bool same = g_q.have && g_q.ver == g_version && g_q.text == textStr && g_q.type == type
                    && g_q.subtype == subtype && g_q.rarity == rarityMask && g_q.lvMin == lvMin
                    && g_q.lvMax == lvMax && g_q.sort == sort && g_q.asc == asc;
    if (same) return g_q.out;

    g_q.have = true; g_q.ver = g_version; g_q.text = textStr; g_q.type = type; g_q.subtype = subtype;
    g_q.rarity = rarityMask; g_q.lvMin = lvMin; g_q.lvMax = lvMax; g_q.sort = sort; g_q.asc = asc;

    const std::vector<std::string> terms = ItemUI::QueryTerms(text);
    const bool allTypes = type.empty() || type == "all";

    struct Cand { int idx; int score; };
    std::vector<Cand> cands; cands.reserve(g_items.size());
    for (int i = 0; i < (int)g_items.size(); ++i)
    {
        const Item& it = g_items[i];
        if (!allTypes && it.type != type) continue;
        if (!subtype.empty() && it.subtype != subtype) continue;
        if (rarityMask) { const int rk = RarityRank(it.rarity); if (rk < 0 || !(rarityMask & (1u << rk))) continue; }
        if (it.level < lvMin || it.level > lvMax) continue;
        int score = 0;
        if (!terms.empty())
        {
            bool ok = true;
            for (const std::string& t : terms) if (it.keyLower.find(t) == std::string::npos) { ok = false; break; }
            if (!ok) continue;
            score = ScoreItem(it, terms);
        }
        cands.push_back({ i, score });
    }

    const bool relevance = (sort == Sort::Relevance && !terms.empty());
    std::sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        const Item& A = g_items[a.idx];
        const Item& B = g_items[b.idx];
        if (relevance)
        {
            if (a.score != b.score) return a.score > b.score;
            return ItemUI::Lower(A.name) < ItemUI::Lower(B.name);
        }
        int cmp = 0;
        switch (sort)
        {
            case Sort::Level:  cmp = (A.level < B.level) ? -1 : (A.level > B.level) ? 1 : 0; break;
            case Sort::Rarity: { int ra = RarityRank(A.rarity), rb = RarityRank(B.rarity); cmp = (ra < rb) ? -1 : (ra > rb) ? 1 : 0; break; }
            case Sort::Value:  cmp = (A.vendorValue < B.vendorValue) ? -1 : (A.vendorValue > B.vendorValue) ? 1 : 0; break;
            case Sort::Type:   { std::string ka = A.type + "\x01" + A.subtype, kb = B.type + "\x01" + B.subtype; cmp = (ka < kb) ? -1 : (ka > kb) ? 1 : 0; break; }
            case Sort::Name:
            case Sort::Relevance:
            default: break;
        }
        if (cmp == 0) { std::string na = ItemUI::Lower(A.name), nb = ItemUI::Lower(B.name); cmp = (na < nb) ? -1 : (na > nb) ? 1 : 0; }
        if (cmp == 0) cmp = (A.id < B.id) ? -1 : (A.id > B.id) ? 1 : 0;
        return asc ? (cmp < 0) : (cmp > 0);
    });

    g_q.out.clear(); g_q.out.reserve(cands.size());
    for (const Cand& c : cands) g_q.out.push_back(c.idx);
    return g_q.out;
}

// ---- type / subtype rails (built once per catalog version) ----
namespace
{
    uint64_t g_typesVer = (uint64_t)-1;
    std::vector<ItemCatalog::TypeCount> g_typesList;
    std::map<std::string, std::vector<ItemCatalog::TypeCount>> g_subBy;

    int TypeOrder(const std::string& t)
    {
        static const char* kOrder[] = { "Armor", "Weapon", "Trinket", "Back", "UpgradeComponent", "Consumable",
                                        "Gathering", "Tool", "Bag", "Container", "CraftingMaterial", "Gizmo",
                                        "MiniPet", "Trophy", "Key", "Trait" };
        for (int i = 0; i < (int)(sizeof(kOrder) / sizeof(kOrder[0])); ++i) if (t == kOrder[i]) return i;
        return 100;   // unknown types sort after the curated set, then alphabetically
    }

    void EnsureTypes()
    {
        if (g_typesVer == g_version) return;
        g_typesVer = g_version;
        std::map<std::string, int> tc;
        std::map<std::string, std::map<std::string, int>> sc;
        for (const ItemCatalog::Item& it : g_items)
        {
            if (it.type.empty()) continue;
            tc[it.type]++;
            if (!it.subtype.empty()) sc[it.type][it.subtype]++;
        }
        g_typesList.clear();
        for (const auto& kv : tc) g_typesList.push_back({ kv.first, kv.second });
        std::sort(g_typesList.begin(), g_typesList.end(), [](const ItemCatalog::TypeCount& a, const ItemCatalog::TypeCount& b) {
            const int oa = TypeOrder(a.name), ob = TypeOrder(b.name);
            return oa != ob ? oa < ob : a.name < b.name;
        });
        g_subBy.clear();
        for (const auto& kv : sc)
        {
            std::vector<ItemCatalog::TypeCount>& v = g_subBy[kv.first];
            for (const auto& s : kv.second) v.push_back({ s.first, s.second });
            std::sort(v.begin(), v.end(), [](const ItemCatalog::TypeCount& a, const ItemCatalog::TypeCount& b) { return a.name < b.name; });
        }
    }
}

const std::vector<ItemCatalog::TypeCount>& ItemCatalog::Types()
{
    PublishIfReady();
    EnsureTypes();
    return g_typesList;
}

const std::vector<ItemCatalog::TypeCount>& ItemCatalog::Subtypes(const std::string& type)
{
    PublishIfReady();
    EnsureTypes();
    static const std::vector<TypeCount> empty;
    auto it = g_subBy.find(type);
    return it == g_subBy.end() ? empty : it->second;
}

void ItemCatalog::Shutdown()
{
    // Join the background load if it's still parsing (bounded by the parse time; the API/HTTP cancel handles
    // the network side). Then free every byte so a disable/re-enable cycle doesn't leave (or compound) ~tens of MB.
    if (g_loadThread.joinable()) g_loadThread.join();
    std::vector<Item>().swap(g_items);          // swap-with-empty actually releases capacity (clear() keeps it)
    g_idIndex.clear();
    g_seedIds.clear(); g_loadSeedIds.clear();
    std::vector<Item>().swap(g_loadResult);
    g_loadDone.store(false); g_published = false; g_warmPending = false; g_loadOk = false;
    std::vector<Item>().swap(g_fetchAccum);
    std::vector<int>().swap(g_fetchStatId);
    g_fetchChunks = 0; g_fetchDone = 0; g_fetchAppend = false; g_targetBuild = 0;
    g_q = QCache{};
    std::vector<TypeCount>().swap(g_typesList);
    g_subBy.clear();
    g_typesVer = (uint64_t)-1;
    g_version = 0; g_buildId = 0; g_fetchedUtc = 0;
    g_prog = ItemCatalog::ProgressInfo{};
    g_api = nullptr;
}
