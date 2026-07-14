#include "app/CraftingEngine.h"
#include "app/TpPrices.h"
#include "app/TpEligibility.h"
#include "util/Trading.h"
#include "api/Client.h"
#include "api/v2/Recipes.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>

// Recipe catalog (JSON seed + /v2/build delta) + the buy-vs-craft engine. All state lives on the main thread;
// the only off-thread work is the startup parse (published lazily) and the API delta (callbacks land on the
// main thread via api.Pump()), exactly like ItemCatalog.
namespace
{
    namespace fs = std::filesystem;
    using CraftingEngine::Recipe;

    Api::Client* g_api = nullptr;
    std::string  g_seedFile;    // <addon>\data\recipes.json        (bundled JSON seed, read-only)
    std::string  g_deltaFile;   // <addon>\cache\recipes-delta.json (writable: ONLY runtime-added OFFICIAL recipes, NOT a copy)
    std::unordered_set<int> g_seedRids;       // OFFICIAL ids (id>0) in the seed -> the delta persists only non-seed officials
    std::unordered_set<int> g_loadSeedRids;   // bg-load handoff of the seed rids (-> g_seedRids on publish)

    std::vector<Recipe>                     g_recipes;
    std::unordered_map<int, std::vector<int>> g_byOut;   // output item id -> recipe indices
    std::unordered_map<int, int>            g_haveRid;   // OFFICIAL recipe id (id>0) -> index into g_recipes (delta dedup + RecipeById)
    // RecipesFor() hands back a per-output list of recipe COPIES by pointer; cached here, invalidated on a
    // g_version change. Namespace-scoped (was a function-local static) so Shutdown frees it on addon disable.
    std::unordered_map<int, std::vector<Recipe>> g_recCache;
    uint64_t                                g_recCacheVer = ~0ull;
    uint64_t                                g_version = 0;
    int                                     g_buildId = 0;

    // async startup parse -> main-thread publish (release/acquire handoff, like ItemCatalog)
    std::thread          g_loadThread;
    std::atomic<bool>    g_loadDone{ false };
    std::vector<Recipe>  g_loadResult;
    int                  g_loadBuild = 0;
    bool                 g_loadOk = false;
    bool                 g_published = false;
    bool                 g_warmPending = false;

    // delta fetch driver (one at a time)
    std::vector<Recipe> g_fetchAccum;
    int   g_fetchChunks = 0, g_fetchDone = 0;
    int   g_targetBuild = 0;
    bool  g_fetching = false;

    const std::vector<Recipe> g_emptyRecs;

    void RebuildIndex()
    {
        g_byOut.clear(); g_byOut.reserve(g_recipes.size() * 2);
        g_haveRid.clear();
        for (int i = 0; i < (int)g_recipes.size(); ++i)
        {
            g_byOut[g_recipes[i].out].push_back(i);
            if (g_recipes[i].id > 0) g_haveRid[g_recipes[i].id] = i;
        }
    }

    // Write ONLY the runtime-added OFFICIAL recipes (id>0 not in the seed) -- the seed is read-only, so the cache
    // stays tiny instead of duplicating the catalog. Mystic Forge recipes (id 0) are seed-only + never written here.
    void SaveDelta()
    {
        if (g_deltaFile.empty()) return;
        nlohmann::json arr = nlohmann::json::array();
        for (const Recipe& r : g_recipes)
        {
            if (r.id <= 0 || g_seedRids.count(r.id)) continue;
            nlohmann::json ing = nlohmann::json::array();
            for (const auto& p : r.ing) ing.push_back({ p.first, p.second });
            arr.push_back({ { "id", r.id }, { "out", r.out }, { "cnt", r.cnt }, { "rating", r.rating },
                            { "ing", std::move(ing) }, { "type", r.type }, { "disc", r.disc } });
        }
        nlohmann::json j{ { "schema", 1 }, { "buildId", g_buildId }, { "count", (int)arr.size() }, { "recipes", std::move(arr) } };
        Json::WriteAtomic(g_deltaFile, j.dump());
    }

    // Parse data\recipes.json (seed) or the small delta into a LOCAL vector -- safe on the load thread. In append
    // mode (the delta) the seed officials are skipped + the buildId only advances.
    bool LoadFile(const std::string& path, std::vector<Recipe>& out, int& buildId,
                  bool append = false, const std::unordered_set<int>* skipRids = nullptr)
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
        auto arr = j.find("recipes");
        if (arr == j.end() || !arr->is_array()) return false;
        if (!append) { buildId = j.value("buildId", 0); out.clear(); }
        else if (int b = j.value("buildId", 0)) buildId = b;
        out.reserve(out.size() + arr->size());
        for (const auto& e : *arr)
        {
            if (!e.is_object()) continue;
            Recipe r;
            r.id     = e.value("id", 0);
            r.out    = e.value("out", 0);
            r.cnt    = e.value("cnt", 1);
            r.rating = e.value("rating", 0);
            r.type   = e.value("type", std::string());
            if (auto d = e.find("disc"); d != e.end() && d->is_array())
                for (const auto& x : *d) if (x.is_string()) r.disc.push_back(x.get<std::string>());
            if (auto ig = e.find("ing"); ig != e.end() && ig->is_array())
                for (const auto& pr : *ig)
                    if (pr.is_array() && pr.size() == 2 && pr[0].is_number_integer() && pr[1].is_number_integer())
                        r.ing.emplace_back(pr[0].get<int>(), pr[1].get<int>());
            if (r.out <= 0 || r.cnt <= 0 || r.ing.empty()) continue;
            if (append && (r.id <= 0 || (skipRids && skipRids->count(r.id)))) continue;   // delta: officials only, no seed dups
            out.push_back(std::move(r));
        }
        return !out.empty();
    }

    void StartAsyncLoad()
    {
        g_published = false; g_loadOk = false; g_loadDone.store(false);
        g_loadThread = std::thread([] {
            std::vector<Recipe> out; int bid = 0;
            bool ok = LoadFile(g_seedFile, out, bid);                 // bundled read-only seed
            std::unordered_set<int> seedRids; seedRids.reserve(out.size());
            for (const Recipe& r : out) if (r.id > 0) seedRids.insert(r.id);
            LoadFile(g_deltaFile, out, bid, /*append*/ true, &seedRids);   // + ONLY runtime-added official recipes
            g_loadResult = std::move(out); g_loadSeedRids = std::move(seedRids);
            g_loadBuild = bid; g_loadOk = ok || !g_loadResult.empty();
            g_loadDone.store(true);
        });
    }

    Recipe ParseFromApi(const Api::V2::Recipe& a)
    {
        Recipe r;
        r.id = a.id; r.out = a.outputItemId; r.cnt = a.outputItemCount > 0 ? a.outputItemCount : 1;
        r.rating = a.minRating; r.type = a.type; r.disc = a.disciplines;
        if (a.raw.is_object())
            if (auto ig = a.raw.find("ingredients"); ig != a.raw.end() && ig->is_array())
                for (const auto& e : *ig)
                    if (e.is_object())
                    {
                        const int id = e.value("item_id", 0), cnt = e.value("count", 0);
                        if (id > 0 && cnt > 0) r.ing.emplace_back(id, cnt);
                    }
        return r;
    }

    void FinalizeFetch()
    {
        for (Recipe& r : g_fetchAccum)
            if (r.out > 0 && r.cnt > 0 && !r.ing.empty() && (r.id == 0 || !g_haveRid.count(r.id)))
                g_recipes.push_back(std::move(r));
        g_fetchAccum.clear();
        if (g_targetBuild > 0) g_buildId = g_targetBuild;
        RebuildIndex();
        ++g_version;
        g_fetching = false;
        SaveDelta();
    }

    void RunFetch(const std::vector<int>& ids)
    {
        if (!g_api || ids.empty()) { g_fetching = false; return; }
        g_fetchAccum.clear(); g_fetchAccum.reserve(ids.size());
        g_fetchDone = 0;
        std::vector<std::vector<int>> chunks;
        for (size_t i = 0; i < ids.size(); i += 200)
            chunks.emplace_back(ids.begin() + i, ids.begin() + std::min(ids.size(), i + 200));
        g_fetchChunks = (int)chunks.size();
        for (const std::vector<int>& ch : chunks)
            g_api->V2().Recipes().Get(ch, [](Api::Result<std::vector<Api::V2::Recipe>> r) {
                if (r.ok) for (const Api::V2::Recipe& a : r.value) if (a.outputItemId > 0) g_fetchAccum.push_back(ParseFromApi(a));
                if (++g_fetchDone >= g_fetchChunks) FinalizeFetch();
            });
    }

    void StartDeltaFetch(int newBuild)
    {
        if (!g_api || g_fetching) return;
        g_fetching = true; g_targetBuild = newBuild;
        g_api->V2().Recipes().GetIds([](Api::Result<std::vector<int>> r) {
            if (!r.ok) { g_fetching = false; return; }
            std::vector<int> need;
            for (int id : r.value) if (!g_haveRid.count(id)) need.push_back(id);
            if (need.empty()) { g_buildId = g_targetBuild; g_fetching = false; SaveDelta(); return; }
            RunFetch(need);
        });
    }

    void DoWarm()
    {
        if (!g_api || g_recipes.empty()) return;   // no recipes (no seed) -> nothing to delta against; reseed offline
        g_api->V2().Build().Get([](Api::Result<int> r) {
            if (!r.ok) return;
            if (g_buildId != 0 && r.value == g_buildId) return;   // unchanged -> trust disk
            StartDeltaFetch(r.value);
        });
    }

    void PublishIfReady()
    {
        if (g_published || !g_loadDone.load()) return;
        if (g_loadThread.joinable()) g_loadThread.join();
        if (g_loadOk) { g_recipes = std::move(g_loadResult); g_buildId = g_loadBuild; }
        g_seedRids = std::move(g_loadSeedRids);
        std::vector<Recipe>().swap(g_loadResult);
        RebuildIndex();
        ++g_version;
        g_published = true;
        if (g_warmPending) { g_warmPending = false; DoWarm(); }
    }

    // ---- the buy-vs-craft engine ----
    constexpr int kMaxDepth = 14;

    struct Ctx
    {
        const CraftingEngine::Opts* opts;
        bool* pricesComplete;
        std::unordered_map<int, long long> unitMemo;   // item -> cheapest unit cost (-1 = unknown), memoized
    };

    // Cost to instant-BUY one unit (lowest sell), or -1 if it can't be bought. Gate on the official TP id set
    // (like the Flip Finder) so an account-bound item is a DEFINITIVE "no price" -- not an endless "loading".
    // pricesComplete only drops while something is genuinely still being fetched (eligibility set / a live price).
    long long BuyUnit(int item, Ctx& ctx)
    {
        if (!TpEligibility::IsReady()) { *ctx.pricesComplete = false; return -1; }   // TP id set still loading
        if (!TpEligibility::IsTradeable(item)) return -1;                            // not on the TP -> no buy price
        TpPrices::Want(item);
        TpPrices::Price p;
        if (!TpPrices::Get(item, p)) { *ctx.pricesComplete = false; return -1; }     // tradeable, price not fetched yet
        if (p.sellUnit <= 0) return -1;                                              // known: no sell offers right now
        return p.sellUnit;
    }

    long long UnitCost(int item, int depth, std::vector<int>& stack, Ctx& ctx);

    // Cheapest per-unit cost to CRAFT `item` (over all its recipes), with the chosen recipe index. -1 if none
    // craftable (missing ingredient price). Does not memoize (cheap; UnitCost memoizes the combined min).
    long long CraftUnit(int item, int depth, std::vector<int>& stack, Ctx& ctx, int* recipeOut)
    {
        const std::vector<Recipe>* recs = CraftingEngine::RecipesFor(item);
        if (recipeOut) *recipeOut = -1;
        if (!recs || depth >= kMaxDepth || std::find(stack.begin(), stack.end(), item) != stack.end()) return -1;
        long long best = -1; int bestIdx = -1;
        stack.push_back(item);
        for (int idx = 0; idx < (int)recs->size(); ++idx)
        {
            const Recipe& r = (*recs)[idx];
            long long c = 0; bool ok = true;
            for (const auto& [ing, cnt] : r.ing)
            {
                const long long iu = UnitCost(ing, depth + 1, stack, ctx);
                if (iu < 0) { ok = false; break; }
                c += iu * (long long)cnt;
            }
            if (ok)
            {
                const long long per = c / std::max(1, r.cnt);
                if (best < 0 || per < best) { best = per; bestIdx = idx; }
            }
        }
        stack.pop_back();
        if (recipeOut) *recipeOut = bestIdx;
        return best;
    }

    long long UnitCost(int item, int depth, std::vector<int>& stack, Ctx& ctx)
    {
        if (auto it = ctx.unitMemo.find(item); it != ctx.unitMemo.end()) return it->second;
        const long long buy = BuyUnit(item, ctx);
        const long long craft = CraftUnit(item, depth, stack, ctx, nullptr);
        long long u = -1;
        if (buy >= 0 && craft >= 0) u = std::min(buy, craft);
        else if (buy >= 0)          u = buy;
        else if (craft >= 0)        u = craft;
        // only memo a stable (non-cyclic, in-depth) answer
        if (depth < kMaxDepth) ctx.unitMemo[item] = u;
        return u;
    }

    // Build the plan tree: the root is FORCED to craft (the user queued it to craft); each ingredient is
    // buy-or-craft, whichever UnitCost says is cheaper. Aggregates base BUYs into `shop` and crafts into `steps`.
    CraftingEngine::PlanNode BuildNode(int item, long long qty, bool forceCraft, int depth, std::vector<int>& stack,
                                       Ctx& ctx, std::unordered_map<int, long long>& shop,
                                       std::unordered_map<int, CraftingEngine::StepRow>& steps)
    {
        CraftingEngine::PlanNode n; n.itemId = item; n.qty = qty;
        const long long buy = BuyUnit(item, ctx);
        const std::vector<Recipe>* recs = CraftingEngine::RecipesFor(item);
        const bool hasRecipe = recs && depth < kMaxDepth && std::find(stack.begin(), stack.end(), item) == stack.end();
        int recipeIdx = -1;
        const long long craft = hasRecipe ? CraftUnit(item, depth, stack, ctx, &recipeIdx) : -1;
        // Root (forceCraft): always craft -- the user queued it -- even before ingredient prices land. Ingredient:
        // craft only when "craft sub-components" is on AND it can't be bought / is cheaper to craft. When no recipe
        // is fully priced yet, fall back to the first recipe so the tree (steps) still shows.
        bool doCraft = hasRecipe && (forceCraft
                       || (ctx.opts->craftPrecursors && (buy < 0 || (craft >= 0 && craft < buy))));
        if (doCraft && recipeIdx < 0) recipeIdx = 0;
        n.craft = doCraft;

        if (doCraft)
        {
            const Recipe& r = (*recs)[recipeIdx];
            n.recipe = recipeIdx;
            n.unitCost = (craft >= 0) ? craft : -1;
            const long long crafts = (qty + r.cnt - 1) / r.cnt;   // batches needed to yield >= qty
            stack.push_back(item);
            for (const auto& [ing, cnt] : r.ing)
                n.kids.push_back(BuildNode(ing, (long long)cnt * crafts, false, depth + 1, stack, ctx, shop, steps));
            stack.pop_back();
            CraftingEngine::StepRow& st = steps[item];   // aggregate repeats of the same craft
            st.outId = item; st.outCnt = r.cnt; st.crafts += crafts; st.ing = r.ing;
            st.disc = r.disc.empty() ? r.type : r.disc.front();
        }
        else
        {
            n.unitCost = buy;            // -1 when unknown / untradeable (the UI flags it)
            shop[item] += qty;           // a base material to buy
        }
        return n;
    }
}

void CraftingEngine::Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir)
{
    g_api = api;
    g_seedFile  = dataDir.empty()  ? std::string() : (dataDir  + "\\recipes.json");
    g_deltaFile = cacheDir.empty() ? std::string() : (cacheDir + "\\recipes-delta.json");
    if (!cacheDir.empty())
    {
        std::error_code ec; fs::create_directories(cacheDir, ec);
        std::error_code e0; fs::remove(cacheDir + "\\recipes.json", e0); fs::remove(cacheDir + "\\recipes.cbor", e0);   // drop the legacy full cache
    }
    StartAsyncLoad();
}

void CraftingEngine::Shutdown()
{
    if (g_loadThread.joinable()) g_loadThread.join();
    g_api = nullptr; g_recipes.clear(); g_byOut.clear(); g_haveRid.clear(); g_recCache.clear(); g_recCacheVer = ~0ull;
    g_seedFile.clear(); g_deltaFile.clear(); g_seedRids.clear(); g_loadSeedRids.clear();
    g_version = 0; g_buildId = 0; g_published = false; g_warmPending = false;
    g_loadDone.store(false); g_loadOk = false; g_fetching = false;
}

void CraftingEngine::Warm()
{
    PublishIfReady();
    if (!g_published) { g_warmPending = true; return; }   // load still running -> warm right after it publishes
    DoWarm();
}

bool     CraftingEngine::Ready()   { PublishIfReady(); return g_published && !g_recipes.empty(); }
int      CraftingEngine::Count()   { PublishIfReady(); return (int)g_recipes.size(); }
uint64_t CraftingEngine::Version() { PublishIfReady(); return g_version; }

const std::vector<CraftingEngine::Recipe>& CraftingEngine::All() { PublishIfReady(); return g_recipes; }

const std::vector<Recipe>* CraftingEngine::RecipesFor(int itemId)
{
    PublishIfReady();
    auto it = g_byOut.find(itemId);
    if (it == g_byOut.end()) return nullptr;
    // g_byOut stores indices; materialize a small per-id list lazily into g_recCache to return by pointer.
    if (g_recCacheVer != g_version) { g_recCache.clear(); g_recCacheVer = g_version; }
    auto c = g_recCache.find(itemId);
    if (c == g_recCache.end())
    {
        std::vector<Recipe> v; v.reserve(it->second.size());
        for (int idx : it->second) v.push_back(g_recipes[idx]);
        c = g_recCache.emplace(itemId, std::move(v)).first;
    }
    return &c->second;
}

const Recipe* CraftingEngine::RecipeById(int recipeId)
{
    PublishIfReady();
    if (recipeId <= 0) return nullptr;
    auto it = g_haveRid.find(recipeId);
    if (it == g_haveRid.end()) return nullptr;
    const int idx = it->second;
    if (idx < 0 || idx >= (int)g_recipes.size()) return nullptr;
    return &g_recipes[idx];
}

CraftingEngine::Plan CraftingEngine::ComputePlan(int itemId, long long qty, const Opts& opts,
                                                 const std::unordered_map<int, long long>& owned)
{
    Plan plan;
    PublishIfReady();
    if (!g_published || g_recipes.empty() || itemId <= 0 || qty <= 0) return plan;

    Ctx ctx{ &opts, &plan.pricesComplete, {} };

    // root price -> tpCost + revenue + tradeable (eligibility-gated, like BuyUnit: account-bound != "loading")
    TpPrices::Price rp{};
    const bool rootElig = TpEligibility::IsReady() && TpEligibility::IsTradeable(itemId);
    if (!TpEligibility::IsReady()) plan.pricesComplete = false;
    else if (rootElig) { TpPrices::Want(itemId); if (!TpPrices::Get(itemId, rp)) plan.pricesComplete = false; }
    plan.outputTradeable = rootElig;

    std::unordered_map<int, long long> shopAgg;
    std::unordered_map<int, StepRow>   stepAgg;
    std::vector<int> stack;
    plan.root = BuildNode(itemId, qty, /*forceCraft*/ true, 0, stack, ctx, shopAgg, stepAgg);
    plan.ok = true;

    // shopping list (apply the owned-materials discount), and the craft cost = sum of base buys
    std::unordered_map<int, long long> ownedLeft = opts.useOwnMaterials ? owned : std::unordered_map<int, long long>{};
    for (const auto& [id, need] : shopAgg)
    {
        long long have = 0;
        if (opts.useOwnMaterials) { auto o = ownedLeft.find(id); if (o != ownedLeft.end()) { have = std::min(o->second, need); o->second -= have; } }
        const long long buyQ = std::max(0LL, need - have);
        ShopRow row; row.itemId = id; row.qty = buyQ;
        const bool elig = TpEligibility::IsReady() && TpEligibility::IsTradeable(id);
        if (!TpEligibility::IsReady()) plan.pricesComplete = false;
        else if (elig)
        {
            TpPrices::Want(id);
            TpPrices::Price p;
            if (!TpPrices::Get(id, p)) plan.pricesComplete = false;     // tradeable, still fetching
            else { row.priced = p.sellUnit > 0; row.unitSell = row.priced ? p.sellUnit : 0; }
        }
        row.total = row.priced ? (long long)row.unitSell * buyQ : (buyQ == 0 ? 0 : -1);
        if (row.total >= 0) plan.craftCost += row.total;   // untradeable mats: total -1, not counted, NOT "loading"
        plan.shopping.push_back(std::move(row));
    }
    std::sort(plan.shopping.begin(), plan.shopping.end(),
              [](const ShopRow& a, const ShopRow& b) { return a.total > b.total; });

    plan.steps.reserve(stepAgg.size());
    for (auto& kv : stepAgg) plan.steps.push_back(std::move(kv.second));

    plan.tpCost      = (rootElig && rp.sellUnit > 0) ? (long long)rp.sellUnit * qty : -1;
    plan.revenueSell = (rootElig && rp.sellUnit > 0) ? (long long)Trading::TpNet(rp.sellUnit) * qty : 0;
    plan.revenueBuy  = (rootElig && rp.buyUnit  > 0) ? (long long)Trading::TpNet(rp.buyUnit)  * qty : 0;
    return plan;
}
