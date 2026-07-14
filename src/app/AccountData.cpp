#include "AccountData.h"
#include "StaticData.h"
#include "AchievementPoints.h"
#include "ItemAttributes.h"
#include "api/Client.h"
#include "guide/CurrentChar.h"
#include "util/ImageCache.h"
#include "Shared.h"            // MumbleIdent (WvW home-world / team fallback)
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>              // _mkgmtime / time for WvW ISO-8601 skirmish countdown
#include <fstream>
#include <set>
#include <string>

using Api::TokenPermission;

namespace
{
    Api::Client*       g_api = nullptr;
    std::string        g_cacheFile;
    std::string        g_inventoryCacheFile;
    std::string        g_legendaryCacheFile;
    std::string        g_wardrobeCacheFile;
    std::string        g_homesteadCacheFile;
    std::string        g_cosmeticsCacheFile;
    AccountData::Model g_m;
    std::string        g_lastActive;
    std::set<std::string> g_inflight;
    std::set<std::string> g_invExpected;
    std::set<std::string> g_invDone;
    std::map<std::string, std::string> g_invErrors;
    std::set<int> g_itemPending;
    bool g_inventoryIndexRequested = false;
    bool g_inventoryLoadedFromDisk = false;
    bool g_inventoryForceCurrentIndex = false;
    bool g_equipWarmRequested = false;   // warm every character's equipment once the character list is known

    double Now() { return ImGui::GetTime(); }
    bool   Stale(double at, double ttl) { return at <= 0.0 || (Now() - at) > ttl; }

    void SaveCache();
    void SaveInventoryCache();
    void LoadInventoryCache();
    void SaveLegendaryCache();
    void LoadLegendaryCache();
    void SaveWardrobeCache();
    void LoadWardrobeCache();
    void SaveHomesteadCache();
    void LoadHomesteadCache();
    void SaveCosmeticsCache();
    void LoadCosmeticsCache();
    void ClearInventoryDetails();

    nlohmann::json SlotToJson(const Api::V2::ItemSlot& s)
    {
        if (s.id <= 0) return nullptr;
        nlohmann::json j = s.raw.is_object() ? s.raw : nlohmann::json::object();
        j["id"] = s.id;
        if (s.count > 0) j["count"] = s.count;
        if (s.charges > 0) j["charges"] = s.charges;
        if (s.skin > 0) j["skin"] = s.skin;
        if (!s.slot.empty()) j["slot"] = s.slot;
        if (!s.binding.empty()) j["binding"] = s.binding;
        if (!s.boundTo.empty()) j["bound_to"] = s.boundTo;
        if (!s.upgrades.empty()) j["upgrades"] = s.upgrades;
        if (!s.infusions.empty()) j["infusions"] = s.infusions;
        if (!s.dyes.empty()) j["dyes"] = s.dyes;
        return j;
    }

    nlohmann::json MaterialToJson(const Api::V2::MaterialStack& m)
    {
        nlohmann::json j = m.raw.is_object() ? m.raw : nlohmann::json::object();
        j["id"] = m.id;
        j["category"] = m.category;
        j["count"] = m.count;
        if (!m.binding.empty()) j["binding"] = m.binding;
        return j;
    }

    nlohmann::json ItemMetaToJson(const AccountData::ItemMeta& meta)
    {
        nlohmann::json j;
        j["id"] = meta.id;
        j["name"] = meta.name;
        j["icon"] = meta.icon;
        j["rarity"] = meta.rarity;
        j["type"] = meta.type;
        j["chatLink"] = meta.chatLink;
        j["description"] = meta.description;
        j["level"] = meta.level;
        j["vendorValue"] = meta.vendorValue;
        j["flags"] = meta.flags;
        j["restrictions"] = meta.restrictions;
        if (meta.details.is_object()) j["details"] = meta.details;
        return j;
    }

    AccountData::ItemMeta ParseItemMeta(const nlohmann::json& j)
    {
        AccountData::ItemMeta meta;
        if (!j.is_object()) return meta;
        meta.have = true;
        meta.id = j.value("id", 0);
        meta.name = j.value("name", std::string());
        meta.icon = j.value("icon", std::string());
        meta.rarity = j.value("rarity", std::string());
        meta.type = j.value("type", std::string());
        meta.chatLink = j.value("chatLink", std::string());
        meta.description = j.value("description", std::string());
        meta.level = j.value("level", 0);
        meta.vendorValue = j.value("vendorValue", 0);
        if (auto it = j.find("flags"); it != j.end() && it->is_array())
            for (const auto& f : *it) if (f.is_string()) meta.flags.push_back(f.get<std::string>());
        if (auto it = j.find("restrictions"); it != j.end() && it->is_array())
            for (const auto& r : *it) if (r.is_string()) meta.restrictions.push_back(r.get<std::string>());
        if (auto it = j.find("details"); it != j.end() && it->is_object())
            meta.details = *it;
        return meta;
    }

    // One uniform fetch (the escape hatch). Auth requests are skipped without a key / scope; the callback runs
    // on the render thread (api.Pump), so it can safely mutate the model + write the cache file.
    bool Fetch(const std::string& path, bool auth, bool useScope, TokenPermission scope,
               std::vector<std::pair<std::string, std::string>> query,
               std::function<void(const nlohmann::json&)> onOk,
               std::function<void(const Api::ApiError&)> onFail = {})
    {
        if (!g_api) return false;
        if (auth && !g_api->HasKey()) return false;
        if (auth && useScope && !g_api->HasPermission(scope)) return false;

        std::string key = path;
        if (!query.empty()) { key += "?"; key += query.front().second; }
        if (g_inflight.count(key)) return false;
        g_inflight.insert(key);

        Api::Request req;
        req.path = path; req.query = std::move(query);
        req.auth = auth; req.hasScope = useScope; req.scope = scope;
        g_api->Conn().GetJson(req, [key, onOk, onFail](Api::Result<nlohmann::json> r) {
            g_inflight.erase(key);
            if (!r.ok) { if (onFail) onFail(r.error); return; }
            try { onOk(r.value); } catch (...) {}
            SaveCache();
        });
        return true;
    }

    // ---- WvW live match (anonymous; conditional + tiered poll via AccountData::TickWvw) ----
    const char* const kWvwColorKeys[3] = { "red", "blue", "green" };
    int WvwOwnerToColor(const std::string& s) { return s == "Red" ? 1 : s == "Blue" ? 2 : s == "Green" ? 3 : 0; }
    int WvwMapTypeToEnum(const std::string& s) {   // 0 EB / 1 RedBL / 2 BlueBL / 3 GreenBL / 4 EotM / -1
        if (s == "Center") return 0; if (s == "RedHome") return 1; if (s == "BlueHome") return 2;
        if (s == "GreenHome") return 3; if (s == "EdgeOfTheMists") return 4; return -1;
    }
    // ISO-8601 "YYYY-MM-DDThh:mm:ssZ" -> unix epoch seconds (UTC). 0 on failure.
    double ParseIso8601Utc(const std::string& s)
    {
        int Y, M, D, h, mi, se;
        if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) != 6) return 0.0;
        std::tm tm{}; tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
        time_t t = _mkgmtime(&tm);
        return t == (time_t)-1 ? 0.0 : (double)t;
    }
    long long ColorNum(const nlohmann::json& obj, const char* key, int color)
    {
        if (!obj.contains(key) || !obj[key].is_object()) return 0;
        const auto& o = obj[key];
        return (o.contains(kWvwColorKeys[color]) && o[kWvwColorKeys[color]].is_number()) ? o[kWvwColorKeys[color]].get<long long>() : 0;
    }
    // Which color (1/2/3) a team id sits in -- scans all_worlds[] then the primary worlds. 0 = not found.
    int WvwColorOfTeam(const nlohmann::json& ov, int teamId)
    {
        for (const char* set : { "all_worlds", "worlds" })
        {
            if (!ov.contains(set) || !ov[set].is_object()) continue;
            const auto& o = ov[set];
            for (int c = 0; c < 3; ++c)
            {
                if (!o.contains(kWvwColorKeys[c])) continue;
                const auto& v = o[kWvwColorKeys[c]];
                if (v.is_array()) { for (const auto& w : v) if (w.is_number() && w.get<int>() == teamId) return c + 1; }
                else if (v.is_number() && v.get<int>() == teamId) return c + 1;
            }
        }
        return 0;
    }
    void ParseWvwMatch(const nlohmann::json& j)
    {
        if (!j.is_object()) return;
        if (g_m.wvwTeamId != 0) { const int c = WvwColorOfTeam(j, g_m.wvwTeamId); if (c) g_m.wvwMyColor = c; }   // the full match carries all_worlds too -- robust color
        for (int c = 0; c < 3; ++c)
        {
            AccountData::WvwSide& s = g_m.wvwSides[c];
            s.score = ColorNum(j, "scores", c);
            s.victoryPoints = (int)ColorNum(j, "victory_points", c);
            s.kills = ColorNum(j, "kills", c);
            s.deaths = ColorNum(j, "deaths", c);
            s.ppt = 0; s.skirmishScore = 0;
            if (j.contains("worlds") && j["worlds"].is_object() && j["worlds"].contains(kWvwColorKeys[c]) && j["worlds"][kWvwColorKeys[c]].is_number())
            {
                s.primaryWorld = j["worlds"][kWvwColorKeys[c]].get<int>();
                const char* wn = StaticData::WorldName(s.primaryWorld);
                if (wn && *wn) s.worldName = wn;
            }
        }
        g_m.wvwSkirmishIndex = 0;
        if (j.contains("skirmishes") && j["skirmishes"].is_array() && !j["skirmishes"].empty())
        {
            const auto& sk = j["skirmishes"].back();
            g_m.wvwSkirmishIndex = sk.value("id", (int)j["skirmishes"].size());
            for (int c = 0; c < 3; ++c) g_m.wvwSides[c].skirmishScore = (int)ColorNum(sk, "scores", c);
        }
        g_m.wvwMaps.clear();
        if (j.contains("maps") && j["maps"].is_array())
            for (const auto& mp : j["maps"])
            {
                AccountData::WvwMapState ms;
                ms.mapId = mp.value("id", 0);
                ms.mapType = WvwMapTypeToEnum(mp.value("type", std::string()));
                if (mp.contains("objectives") && mp["objectives"].is_array())
                    for (const auto& ob : mp["objectives"])
                    {
                        AccountData::WvwObjState os;
                        os.objId = ob.value("id", std::string());
                        os.type  = ob.value("type", std::string());
                        os.owner = WvwOwnerToColor(ob.value("owner", std::string()));
                        os.pointsTick  = ob.value("points_tick", 0);
                        os.lastFlipped = ParseIso8601Utc(ob.value("last_flipped", std::string()));
                        if (os.owner >= 1 && os.owner <= 3) { ms.ownedByColor[os.owner - 1]++; g_m.wvwSides[os.owner - 1].ppt += os.pointsTick; }
                        ms.objectives.push_back(std::move(os));
                    }
                g_m.wvwMaps.push_back(std::move(ms));
            }
        // Skirmish "next in": skirmishes are 2h; current skirmish K ends at start_time + K*2h.
        if (j.contains("start_time") && j["start_time"].is_string())
        {
            const double startEpoch = ParseIso8601Utc(j["start_time"].get<std::string>());
            if (startEpoch > 0.0)
            {
                const double nextEpoch = startEpoch + (double)g_m.wvwSkirmishIndex * 2.0 * 3600.0;
                g_m.wvwSkirmishEndsAt = Now() + (nextEpoch - (double)time(nullptr));   // -> ImGui-time of the next flip
            }
        }
    }
    void ResolveWvwTeam()
    {
        if (g_m.wvwTeamId == 0)
        {
            // Prefer the AUTHORITATIVE account team (works in PvE too). MumbleLink WorldID is ONLY a keyless
            // fallback -- under World Restructuring it can report a legacy world that is absent from the match's
            // team lists, so it must NEVER override /v2/account/wvw for a keyed account (that was the "stuck on
            // Loading in PvE with an all-scopes key" bug).
            // Resolve the WvW TEAM strictly from the account (the only reliable source under World Restructuring).
            // MumbleLink's WorldID is NOT used: in PvE it is garbage (e.g. 268435459) and even in WvW it is a
            // legacy world absent from the team-keyed match data, so it only poisoned the lookup. The fetch keeps
            // retrying each frame until the token scopes finish loading and it returns the real team_id.
            // useScope=false: do NOT pre-gate on the local scope set. After a key change the token scopes can be
            // unloaded/stale, which silently deadlocked this fetch ("Resolving WvW team..." forever). Send it as
            // soon as a key exists and let the SERVER validate; a real scope problem then surfaces via onFail.
            // The actual JSON key is `team` (the changelog name `team_id` is not the key); tolerate both.
            Fetch("/v2/account/wvw", true, false, TokenPermission::Account, {}, [](const nlohmann::json& j) {
                int t = 0;   // guarded (no value() throw on a wrong type)
                if (j.is_object())
                {
                    if (j.contains("team")    && j["team"].is_number())    t = j["team"].get<int>();
                    else if (j.contains("team_id") && j["team_id"].is_number()) t = j["team_id"].get<int>();
                    else if (j.contains("world")   && j["world"].is_number())   t = j["world"].get<int>();
                }
                if (t) { g_m.wvwTeamId = t; g_m.wvwError.clear(); }
                else if (j.is_object() && j.contains("team")) g_m.wvwError = "No WvW team assigned (nominate a WvW guild)";   // team:0 -> account not placed on a team this matchup
                else g_m.wvwError = "account/wvw -> " + (j.is_object() ? j.dump().substr(0, 90) : std::string("(non-object)"));
            }, [](const Api::ApiError& e) { g_m.wvwError = std::string("account/wvw: ") + (!e.text.empty() ? e.text : ("HTTP " + std::to_string(e.status))); });
        }
        if (g_m.wvwTeamId == 0) return;   // no team yet (awaiting the account fetch, or the account isn't on a team) -> do NOT scan with team 0
        StaticData::WarmWorlds();
        StaticData::WarmWvwObjectives();
        // World Restructuring: the ?world= query still wants a LEGACY world id, but /v2/account/wvw gives a TEAM
        // id -- so the server rejects it ("world not currently in a match"). Instead pull the full match list
        // (whose all_worlds DO use the WR team ids), find the one containing your team, and populate from it
        // directly (one fetch resolves + fills; later polls re-fetch just that match by id).
        Fetch("/v2/wvw/matches", false, false, TokenPermission::Account, { {"ids","all"} },
            [](const nlohmann::json& j) {
                if (!j.is_array()) { g_m.wvwError = "unexpected match list"; return; }
                for (const auto& mt : j)
                {
                    const int c = WvwColorOfTeam(mt, g_m.wvwTeamId);
                    if (!c) continue;
                    g_m.wvwMatchId = mt.value("id", std::string());
                    g_m.wvwMyColor = c;
                    g_m.wvwTeamAt = Now();
                    ParseWvwMatch(mt);                          // the full match is in hand -> fill now
                    g_m.haveWvwMatch = true; g_m.wvwMatchAt = Now();
                    for (int k = 0; k < 3; ++k) { g_m.wvwSides[k].kdKills = g_m.wvwSides[k].kills; g_m.wvwSides[k].kdDeaths = g_m.wvwSides[k].deaths; }
                    g_m.wvwKdShownAt = Now();
                    g_m.wvwError.clear();
                    return;
                }
                g_m.wvwError = "team " + std::to_string(g_m.wvwTeamId) + " not in any match";   // id-space mismatch -> tells us to remap
            },
            [](const Api::ApiError& e) { g_m.wvwError = !e.text.empty() ? e.text : ("HTTP " + std::to_string(e.status)); });
    }
    void RefreshWvwMatch()
    {
        if (g_m.wvwMatchId.empty()) { ResolveWvwTeam(); return; }
        Fetch("/v2/wvw/matches/" + g_m.wvwMatchId, false, false, TokenPermission::Account, {}, [](const nlohmann::json& j) {
            ParseWvwMatch(j);
            g_m.haveWvwMatch = true; g_m.wvwMatchAt = Now();
            if (Stale(g_m.wvwKdShownAt, 120))   // K/D display throttle: re-publish the shown ratio ~every 2 min
            {
                for (int c = 0; c < 3; ++c) { g_m.wvwSides[c].kdKills = g_m.wvwSides[c].kills; g_m.wvwSides[c].kdDeaths = g_m.wvwSides[c].deaths; }
                g_m.wvwKdShownAt = Now();
            }
        });
    }

    void SyncInventoryStatus()
    {
        AccountData::InventoryIndexStatus& st = g_m.inventoryStatus;
        st.totalSources = (int)g_invExpected.size();
        st.completedSources = (int)g_invDone.size();
        st.pendingItemMeta = (int)g_itemPending.size();
        st.errors.clear();
        for (const auto& kv : g_invErrors)
            st.errors.push_back(kv.second.empty() ? kv.first : (kv.first + ": " + kv.second));

        if (!g_inventoryIndexRequested && st.totalSources == 0)
        {
            st.state = AccountData::InventoryState::Idle;
            return;
        }
        if (st.completedSources < st.totalSources || st.pendingItemMeta > 0)
        {
            st.state = AccountData::InventoryState::Loading;
            return;
        }
        if (!st.errors.empty())
            st.state = st.completedSources > (int)st.errors.size()
                ? AccountData::InventoryState::Partial
                : AccountData::InventoryState::Error;
        else
            st.state = AccountData::InventoryState::Complete;
    }

    void BeginInventoryIndex(bool force)
    {
        if (force || g_m.inventoryStatus.state != AccountData::InventoryState::Loading)
        {
            g_invExpected.clear();
            g_invDone.clear();
            g_invErrors.clear();
            g_m.inventoryStatus = AccountData::InventoryIndexStatus{};
            g_m.inventoryStatus.startedAt = Now();
        }
        g_inventoryIndexRequested = true;
        g_m.inventoryStatus.state = AccountData::InventoryState::Loading;
        SyncInventoryStatus();
    }

    void ExpectInventorySource(const std::string& key)
    {
        if (key.empty()) return;
        g_invExpected.insert(key);
        SyncInventoryStatus();
    }

    void FinishInventorySource(const std::string& key, const std::string& error = std::string())
    {
        if (key.empty()) return;
        g_invExpected.insert(key);
        g_invDone.insert(key);
        if (!error.empty()) g_invErrors[key] = error;
        else g_invErrors.erase(key);
        g_m.inventoryStatus.lastRefreshAt = Now();
        SyncInventoryStatus();
    }

    void CollectItemId(std::set<int>& ids, int id)
    {
        if (id > 0 && !g_m.itemMeta.count(id) && !g_itemPending.count(id))
            ids.insert(id);
    }

    void CollectSlotItemIds(std::set<int>& ids, const Api::V2::ItemSlot& s)
    {
        CollectItemId(ids, s.id);
        for (int id : s.upgrades) CollectItemId(ids, id);
        for (int id : s.infusions) CollectItemId(ids, id);
    }

    void EnsureItemMetadataInternal(const std::set<int>& ids)
    {
        if (!g_api || ids.empty()) { SyncInventoryStatus(); return; }
        std::vector<int> need;
        need.reserve(ids.size());
        for (int id : ids)
            if (id > 0 && !g_m.itemMeta.count(id) && !g_itemPending.count(id))
            {
                need.push_back(id);
                g_itemPending.insert(id);
            }
        if (need.empty()) { SyncInventoryStatus(); return; }
        SyncInventoryStatus();
        g_api->V2().Items().Get(need, [need](Api::Result<std::vector<Api::V2::Item>> r) {
            for (int id : need) g_itemPending.erase(id);
            if (r.ok)
            {
                for (const Api::V2::Item& item : r.value)
                {
                    AccountData::ItemMeta meta;
                    meta.have = true;
                    meta.id = item.id;
                    meta.name = item.name;
                    meta.icon = item.icon;
                    meta.rarity = item.rarity;
                    meta.type = item.type;
                    meta.chatLink = item.chatLink;
                    meta.description = item.raw.value("description", std::string());
                    meta.level = item.raw.value("level", 0);
                    meta.vendorValue = item.raw.value("vendor_value", 0);
                    if (auto it = item.raw.find("flags"); it != item.raw.end() && it->is_array())
                        for (const auto& f : *it) if (f.is_string()) meta.flags.push_back(f.get<std::string>());
                    if (auto it = item.raw.find("restrictions"); it != item.raw.end() && it->is_array())
                        for (const auto& r : *it) if (r.is_string()) meta.restrictions.push_back(r.get<std::string>());
                    if (auto it = item.raw.find("details"); it != item.raw.end() && it->is_object())
                        meta.details = *it;
                    if (!meta.icon.empty())
                    {
                        char texId[40];
                        std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", meta.id);
                        ImageCache::PrefetchUrl(texId, meta.icon.c_str());
                    }
                    if (meta.id > 0) g_m.itemMeta[meta.id] = std::move(meta);
                }
            }
            SyncInventoryStatus();
            SaveInventoryCache();
        });
    }

    void EnsureItemMetadataInternal(const std::vector<int>& ids)
    {
        std::set<int> unique;
        for (int id : ids) CollectItemId(unique, id);
        EnsureItemMetadataInternal(unique);
    }

    std::vector<Api::V2::ItemSlot> ParseItemSlotArray(const nlohmann::json& j)
    {
        std::vector<Api::V2::ItemSlot> out;
        if (!j.is_array()) return out;
        out.reserve(j.size());
        for (const auto& e : j)
            out.push_back(Api::V2::ParseItemSlot(e));
        return out;
    }

    void ApplyBankSlots(std::vector<Api::V2::ItemSlot> slots)
    {
        std::set<int> itemIds;
        int used = 0, items = 0;
        for (const Api::V2::ItemSlot& s : slots)
            if (s.id > 0)
            {
                ++used;
                items += std::max(1, s.count);
                CollectSlotItemIds(itemIds, s);
            }
        g_m.bankSlots = std::move(slots);
        g_m.bankTotal = (int)g_m.bankSlots.size();
        g_m.bankUsed = used;
        g_m.bankItems = items;
        g_m.haveBank = true;
        g_m.bankAt = Now();
        g_m.bankError.clear();
        EnsureItemMetadataInternal(itemIds);
        SaveInventoryCache();
    }

    void ApplySharedSlots(std::vector<Api::V2::ItemSlot> slots)
    {
        std::set<int> itemIds;
        int used = 0;
        for (const Api::V2::ItemSlot& s : slots)
            if (s.id > 0)
            {
                ++used;
                CollectSlotItemIds(itemIds, s);
            }
        g_m.sharedSlots = std::move(slots);
        g_m.sharedTotal = (int)g_m.sharedSlots.size();
        g_m.sharedUsed = used;
        g_m.haveShared = true;
        g_m.sharedAt = Now();
        g_m.sharedError.clear();
        EnsureItemMetadataInternal(itemIds);
        SaveInventoryCache();
    }

    void ApplyMaterials(const nlohmann::json& j)
    {
        if (!j.is_array()) return;
        std::set<int> itemIds;
        int types = 0;
        long long total = 0;
        g_m.materials.clear();
        g_m.materials.reserve(j.size());
        for (const auto& e : j)
        {
            if (!e.is_object()) continue;
            Api::V2::MaterialStack m;
            m.id = e.value("id", 0);
            m.category = e.value("category", 0);
            m.count = e.value("count", 0);
            m.binding = e.value("binding", std::string());
            m.raw = e;
            if (m.count > 0)
            {
                ++types;
                total += m.count;
                CollectItemId(itemIds, m.id);
            }
            g_m.materials.push_back(std::move(m));
        }
        g_m.matTypes = types;
        g_m.matTotal = total;
        g_m.haveMats = true;
        g_m.matsAt = Now();
        g_m.matsError.clear();
        EnsureItemMetadataInternal(itemIds);
        SaveInventoryCache();
    }

    void ApplyCharacterInventory(const std::string& name, const nlohmann::json& j)
    {
        AccountData::CharInv ci;
        ci.have = true;
        ci.at = Now();
        std::set<int> itemIds;
        if (j.is_object() && j.contains("bags") && j["bags"].is_array())
        {
            ci.bags.reserve(j["bags"].size());
            for (const auto& bagJson : j["bags"])
            {
                AccountData::CharBag bag;
                if (bagJson.is_object())
                {
                    bag.present = true;
                    bag.id = bagJson.value("id", 0);
                    bag.size = bagJson.value("size", 0);
                    CollectItemId(itemIds, bag.id);
                    if (bagJson.contains("inventory") && bagJson["inventory"].is_array())
                        bag.slots = ParseItemSlotArray(bagJson["inventory"]);
                    while ((int)bag.slots.size() < bag.size)
                        bag.slots.push_back(Api::V2::ItemSlot{});
                    ci.totalSlots += bag.size;
                    for (const Api::V2::ItemSlot& s : bag.slots)
                        if (s.id > 0)
                        {
                            ++ci.usedSlots;
                            CollectSlotItemIds(itemIds, s);
                        }
                }
                ci.bags.push_back(std::move(bag));
            }
        }
        ci.error.clear();
        g_m.inv[name] = std::move(ci);
        EnsureItemMetadataInternal(itemIds);
        SaveInventoryCache();
    }

    void RefreshBank(bool inventoryIndex = false);
    void RefreshMats(bool inventoryIndex = false);
    void RefreshShared(bool inventoryIndex = false);
    void FetchCharacterInventory(const std::string& name, bool force, bool inventoryIndex);
    void FetchCharacterEquipment(const std::string& name, bool force);

    void QueueCharacterInventories(bool force)
    {
        if (!g_api || !g_api->HasPermission(TokenPermission::Inventories) || !g_api->HasPermission(TokenPermission::Characters))
            return;
        for (const AccountData::AcctChar& ch : g_m.characters)
            if (!ch.name.empty())
                FetchCharacterInventory(ch.name, force, true);
        SyncInventoryStatus();
    }

    // Warm EVERY character's equipment (mirrors QueueCharacterInventories). Served stale from the disk cache:
    // FetchCharacterEquipment is a no-op when already cached unless `force`, so this only pulls the missing ones.
    void QueueCharacterEquipment(bool force)
    {
        if (!g_api || !g_api->HasPermission(TokenPermission::Characters)) return;
        for (const AccountData::AcctChar& ch : g_m.characters)
            if (!ch.name.empty())
                FetchCharacterEquipment(ch.name, force);
    }

    // --- per-domain refreshers ---
    void RefreshAccount()
    {
        Fetch("/v2/account", true, true, TokenPermission::Account, {}, [](const nlohmann::json& j) {
            if (j.is_object())
            {
                const std::string resolvedId = j.value("id", std::string());
                if (!g_m.accountId.empty() && !resolvedId.empty() && g_m.accountId != resolvedId)
                    ClearInventoryDetails();
                g_m.accountId = resolvedId;
                if (j.contains("name") && j["name"].is_string())  g_m.accountName = j["name"].get<std::string>();
                if (j.contains("age") && j["age"].is_number())     g_m.age = j["age"].get<long>();
                if (j.contains("fractal_level") && j["fractal_level"].is_number()) g_m.fractalLevel = j["fractal_level"].get<int>();
                if (j.contains("wvw_rank") && j["wvw_rank"].is_number())           g_m.wvwRank = j["wvw_rank"].get<int>();
                if (j.contains("daily_ap") && j["daily_ap"].is_number())           g_m.dailyAp = j["daily_ap"].get<int>();      // capped historical daily AP
                if (j.contains("monthly_ap") && j["monthly_ap"].is_number())       g_m.monthlyAp = j["monthly_ap"].get<int>();  // capped historical monthly AP
                g_m.totalAp = g_m.achPermAp + g_m.dailyAp + g_m.monthlyAp;   // fold into the (possibly already-computed) permanent sum
                g_m.haveAccount = true; g_m.accountAt = Now();
                SaveInventoryCache();
            }
        });
    }
    void RefreshWallet()
    {
        Fetch("/v2/account/wallet", true, true, TokenPermission::Wallet, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            g_m.wallet.clear();
            for (const auto& e : j)
                if (e.is_object() && e.contains("id") && e.contains("value"))
                    g_m.wallet.push_back({ e["id"].get<int>(), e["value"].get<long long>() });
            g_m.haveWallet = true; g_m.walletAt = Now();
        });
    }
    void RefreshChars()
    {
        Fetch("/v2/characters", true, true, TokenPermission::Characters, { {"ids", "all"} }, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            g_m.characters.clear();
            for (const auto& c : j)
            {
                if (!c.is_object()) continue;
                AccountData::AcctChar ch;
                ch.name       = c.value("name", std::string());
                ch.profession = c.value("profession", std::string());
                ch.race       = c.value("race", std::string());
                ch.level      = c.value("level", 0);
                ch.age        = c.value("age", 0L);
                ch.deaths     = c.value("deaths", 0);
                if (!ch.name.empty()) g_m.characters.push_back(std::move(ch));
            }
            g_m.haveChars = true; g_m.charsAt = Now();
            if (g_inventoryIndexRequested) QueueCharacterInventories(g_inventoryForceCurrentIndex);
            if (g_equipWarmRequested) { g_equipWarmRequested = false; QueueCharacterEquipment(false); }
        });
    }
    void RefreshAch()
    {
        Fetch("/v2/account/achievements", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            int prog = 0, done = 0;
            long long ap = 0;
            for (const auto& a : j)
            {
                if (!a.is_object()) continue;
                const bool d = a.value("done", false);
                if (d) ++done;
                else if (a.value("current", 0) > 0) ++prog;
                // Sum tier points vs progress (PointsFor handles done = all tiers + the repeatable cap).
                ap += AchPoints::PointsFor(a.value("id", 0), a.value("current", 0), a.value("repeated", 0), d);
            }
            g_m.achInProgress = prog; g_m.achDone = done; g_m.haveAch = true; g_m.achAt = Now();
            // Store the permanent tier sum; the total folds in daily_ap/monthly_ap (set by RefreshAccount, which
            // may complete before OR after this) -- recompute from the stored parts so neither order drops them.
            g_m.achPermAp = ap; g_m.haveTotalAp = true;
            g_m.totalAp = g_m.achPermAp + g_m.dailyAp + g_m.monthlyAp;
        });
    }
    void RefreshDailies()
    {
        Fetch("/v2/achievements/daily", false, false, TokenPermission::Account, {}, [](const nlohmann::json& j) {
            if (!j.is_object() || !j.contains("pve") || !j["pve"].is_array()) return;
            std::vector<std::string> ids;
            for (const auto& e : j["pve"]) { if (e.is_object() && e.contains("id")) ids.push_back(std::to_string(e["id"].get<long long>())); if (ids.size() >= 8) break; }
            if (ids.empty()) { g_m.dailyPve.clear(); g_m.haveDailies = true; g_m.dailiesAt = Now(); return; }
            std::string csv; for (size_t i = 0; i < ids.size(); ++i) { if (i) csv += ","; csv += ids[i]; }
            Fetch("/v2/achievements", false, false, TokenPermission::Account, { {"ids", csv} }, [](const nlohmann::json& a) {
                if (!a.is_array()) return;
                g_m.dailyPve.clear();
                for (const auto& e : a) if (e.is_object() && e.contains("name")) g_m.dailyPve.push_back(e["name"].get<std::string>());
                g_m.haveDailies = true; g_m.dailiesAt = Now();
            });
        });
    }
    void ParseWv(const nlohmann::json& j, std::vector<AccountData::WvObjective>& out, int& metaCur, int& metaMax)
    {
        out.clear();
        if (!j.is_object()) return;
        metaCur = j.value("meta_progress_current", 0);
        metaMax = j.value("meta_progress_complete", 0);
        if (j.contains("objectives") && j["objectives"].is_array())
            for (const auto& o : j["objectives"])
            {
                if (!o.is_object()) continue;
                AccountData::WvObjective w;
                w.title = o.value("title", std::string());
                w.cur = o.value("progress_current", 0);
                w.max = o.value("progress_complete", 0);
                w.claimed = o.value("claimed", false);
                out.push_back(std::move(w));
            }
    }
    void RefreshWv()   // Wizard's Vault daily + weekly (replaced the removed /v2/achievements/daily system)
    {
        // The error callbacks mark the data loaded-but-unavailable: the endpoints error for an account that has
        // not unlocked the Wizard's Vault (needs a level-80 character). Without them haveWv* never flips and both
        // the Today's Dailies widget AND the WV Daily/Weekly data texts sit on "Loading"/"..." forever (and Tick
        // would keep re-hitting the failing endpoint, since *At stays 0 = always stale).
        Fetch("/v2/account/wizardsvault/daily", true, true, TokenPermission::Account, {},
            [](const nlohmann::json& j) {
                ParseWv(j, g_m.wvDaily, g_m.wvDailyMetaCur, g_m.wvDailyMetaMax); g_m.haveWvDaily = true; g_m.wvDailyAt = Now(); g_m.wvUnavailable = false;
            },
            [](const Api::ApiError&) {
                g_m.wvDaily.clear(); g_m.wvDailyMetaCur = g_m.wvDailyMetaMax = 0; g_m.haveWvDaily = true; g_m.wvDailyAt = Now(); g_m.wvUnavailable = true;
            });
        Fetch("/v2/account/wizardsvault/weekly", true, true, TokenPermission::Account, {},
            [](const nlohmann::json& j) {
                ParseWv(j, g_m.wvWeekly, g_m.wvWeeklyMetaCur, g_m.wvWeeklyMetaMax); g_m.haveWvWeekly = true; g_m.wvWeeklyAt = Now();
            },
            [](const Api::ApiError&) {
                g_m.wvWeekly.clear(); g_m.wvWeeklyMetaCur = g_m.wvWeeklyMetaMax = 0; g_m.haveWvWeekly = true; g_m.wvWeeklyAt = Now(); g_m.wvUnavailable = true;
            });
    }
    void RefreshMastery()
    {
        Fetch("/v2/account/mastery/points", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            int earned = 0, spent = 0;
            if (j.is_object() && j.contains("totals") && j["totals"].is_array())
                for (const auto& t : j["totals"]) { earned += t.value("earned", 0); spent += t.value("spent", 0); }
            g_m.mpEarned = earned; g_m.mpSpent = spent;
        });
        Fetch("/v2/account/masteries", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.masteriesUnlocked = j.is_array() ? (int)j.size() : 0;
            g_m.haveMastery = true; g_m.masteryAt = Now();
        });
    }
    void RefreshBank(bool inventoryIndex)
    {
        const std::string src = "Bank";
        if (inventoryIndex) ExpectInventorySource(src);
        const bool queued = Fetch("/v2/account/bank", true, true, TokenPermission::Inventories, {}, [inventoryIndex, src](const nlohmann::json& j) {
            ApplyBankSlots(ParseItemSlotArray(j));
            if (inventoryIndex) FinishInventorySource(src);
        }, [inventoryIndex, src](const Api::ApiError& e) {
            g_m.bankError = e.text.empty() ? "request failed" : e.text;
            if (inventoryIndex) FinishInventorySource(src, g_m.bankError);
        });
        if (inventoryIndex && !queued) FinishInventorySource(src);
    }
    void RefreshShared(bool inventoryIndex)
    {
        const std::string src = "Shared";
        if (inventoryIndex) ExpectInventorySource(src);
        const bool queued = Fetch("/v2/account/inventory", true, true, TokenPermission::Inventories, {}, [inventoryIndex, src](const nlohmann::json& j) {
            ApplySharedSlots(ParseItemSlotArray(j));
            if (inventoryIndex) FinishInventorySource(src);
        }, [inventoryIndex, src](const Api::ApiError& e) {
            g_m.sharedError = e.text.empty() ? "request failed" : e.text;
            if (inventoryIndex) FinishInventorySource(src, g_m.sharedError);
        });
        if (inventoryIndex && !queued) FinishInventorySource(src);
    }
    void RefreshMats(bool inventoryIndex)
    {
        const std::string src = "Materials";
        if (inventoryIndex) ExpectInventorySource(src);
        const bool queued = Fetch("/v2/account/materials", true, true, TokenPermission::Inventories, {}, [inventoryIndex, src](const nlohmann::json& j) {
            ApplyMaterials(j);
            if (inventoryIndex) FinishInventorySource(src);
        }, [inventoryIndex, src](const Api::ApiError& e) {
            g_m.matsError = e.text.empty() ? "request failed" : e.text;
            if (inventoryIndex) FinishInventorySource(src, g_m.matsError);
        });
        if (inventoryIndex && !queued) FinishInventorySource(src);
    }
    void RefreshTp()
    {
        Fetch("/v2/commerce/delivery", true, true, TokenPermission::Tradingpost, {}, [](const nlohmann::json& j) {
            if (!j.is_object()) return;
            g_m.tpCoins = j.value("coins", 0LL);
            g_m.tpItems = (j.contains("items") && j["items"].is_array()) ? (int)j["items"].size() : 0;
            g_m.haveTp = true; g_m.tpAt = Now();
        });
        Fetch("/v2/commerce/transactions/current/buys", true, true, TokenPermission::Tradingpost, {}, [](const nlohmann::json& j) {
            g_m.tpBuys = j.is_array() ? (int)j.size() : 0;
        });
        Fetch("/v2/commerce/transactions/current/sells", true, true, TokenPermission::Tradingpost, {}, [](const nlohmann::json& j) {
            g_m.tpSells = j.is_array() ? (int)j.size() : 0;
        });
    }
    void RefreshRaids()
    {
        Fetch("/v2/account/raids", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.raidsClearedIds.clear();
            if (j.is_array()) for (const auto& e : j) if (e.is_string()) g_m.raidsClearedIds.push_back(e.get<std::string>());
            g_m.raidsCleared = (int)g_m.raidsClearedIds.size();   // cleared encounter ids this week (the per-boss tracker reads the ids)
            g_m.haveRaids = true; g_m.raidsAt = Now();
        });
    }
    void RefreshDungeons()   // /v2/account/dungeons: the path ids completed since the daily reset (per-path ticks)
    {
        Fetch("/v2/account/dungeons", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.dungeonPathsToday.clear();
            if (j.is_array()) for (const auto& e : j) if (e.is_string()) g_m.dungeonPathsToday.insert(e.get<std::string>());
            g_m.haveDungeons = true; g_m.dungeonsAt = Now();
        });
    }
    void RefreshLuck()
    {
        Fetch("/v2/account/luck", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            long long v = 0;
            if (j.is_array()) for (const auto& e : j) if (e.is_object() && e.value("id", std::string()) == "luck") v = e.value("value", 0LL);
            g_m.luck = v; g_m.haveLuck = true; g_m.luckAt = Now();
        });
    }
    void RefreshLegendaryArmory()   // Items-tab Legendary Armory scope: the static catalog (id->max) + owned (id->count)
    {
        // Static catalog (anonymous, day-cached): every legendary-armory item id -> how many may be equipped.
        Fetch("/v2/legendaryarmory", false, false, TokenPermission::Account, { { "ids", "all" } }, [](const nlohmann::json& j) {
            if (!j.is_array() || j.empty()) return;
            std::map<int, int> mx;
            for (const auto& e : j) if (e.is_object()) mx[e.value("id", 0)] = e.value("max_count", 0);
            if (!mx.empty()) { g_m.legendaryMax = std::move(mx); g_m.haveLegendary = true; g_m.legendaryAt = Now(); SaveLegendaryCache(); }
        });
        // Owned copies (auth): item id -> bound count.
        Fetch("/v2/account/legendaryarmory", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::map<int, int> owned;
            for (const auto& e : j) if (e.is_object()) owned[e.value("id", 0)] = e.value("count", 0);
            g_m.legendaryOwned = std::move(owned); g_m.legendaryAt = Now(); SaveLegendaryCache();
        });
    }
    void RefreshSkinsUnlocked()     // unlocked wardrobe skin ids (Items-tab Wardrobe scope)
    {
        Fetch("/v2/account/skins", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s; for (const auto& e : j) if (e.is_number_integer()) s.insert(e.get<int>());
            g_m.skinsUnlocked = std::move(s); g_m.haveSkins = true; g_m.skinsAt = Now(); SaveWardrobeCache();
        });
    }
    void RefreshDyesUnlocked()      // unlocked dye color ids (Items-tab Wardrobe scope)
    {
        Fetch("/v2/account/dyes", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s; for (const auto& e : j) if (e.is_number_integer()) s.insert(e.get<int>());
            g_m.dyesUnlocked = std::move(s); g_m.haveDyes = true; g_m.dyesAt = Now(); SaveWardrobeCache();
        });
    }

    // Collections-tab visual cosmetics (P2): each /v2/account/<cat> returns a flat id array (mounts uses the
    // nested /v2/account/mounts/skins). Mirrors RefreshSkinsUnlocked/RefreshDyesUnlocked; persisted to the shared
    // cosmetics-unlocks cache (per-account, serve-stale-then-refresh).
    void SaveCosmeticsCache();
    // FNV-1a 32-bit -> positive int (matches build_cosmetics._fnv). Lets a STRING-id endpoint (emotes) key into the
    // int-based CosmeticCatalog: the catalog id is _fnv(emote-string) and /v2/account/emotes is FNV'd the same way.
    int FnvId(const std::string& s) { unsigned h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return (int)(h & 0x7FFFFFFFu); }
    void RefreshCosmeticUnlocked(const char* path, std::set<int>& out, bool& have, double& at)
    {
        Fetch(path, true, true, TokenPermission::Unlocks, {}, [&out, &have, &at](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s; for (const auto& e : j) if (e.is_number_integer()) s.insert(e.get<int>());
            out = std::move(s); have = true; at = Now(); SaveCosmeticsCache();
        });
    }
    void RefreshMountsUnlocked()    { RefreshCosmeticUnlocked("/v2/account/mounts/skins", g_m.mountsUnlocked,    g_m.haveMounts,    g_m.mountsAt); }
    void RefreshOutfitsUnlocked()   { RefreshCosmeticUnlocked("/v2/account/outfits",      g_m.outfitsUnlocked,   g_m.haveOutfits,   g_m.outfitsAt); }
    void RefreshGlidersUnlocked()   { RefreshCosmeticUnlocked("/v2/account/gliders",      g_m.glidersUnlocked,   g_m.haveGliders,   g_m.glidersAt); }
    void RefreshJadeBotsUnlocked()  { RefreshCosmeticUnlocked("/v2/account/jadebots",     g_m.jadeBotsUnlocked,  g_m.haveJadeBots,  g_m.jadeBotsAt); }
    void RefreshSkiffsUnlocked()    { RefreshCosmeticUnlocked("/v2/account/skiffs",       g_m.skiffsUnlocked,    g_m.haveSkiffs,    g_m.skiffsAt); }
    void RefreshNoveltiesUnlocked() { RefreshCosmeticUnlocked("/v2/account/novelties",    g_m.noveltiesUnlocked, g_m.haveNovelties, g_m.noveltiesAt); }
    void RefreshMailCarriersUnlocked() { RefreshCosmeticUnlocked("/v2/account/mailcarriers", g_m.mailCarriersUnlocked, g_m.haveMailCarriers, g_m.mailCarriersAt); }
    void RefreshMinisUnlocked()        { RefreshCosmeticUnlocked("/v2/account/minis",        g_m.minisUnlocked,        g_m.haveMinis,        g_m.minisAt); }
    void RefreshTitlesUnlocked()       { RefreshCosmeticUnlocked("/v2/account/titles",       g_m.titlesUnlocked,       g_m.haveTitles,       g_m.titlesAt); }
    void RefreshRecipesUnlocked()      { RefreshCosmeticUnlocked("/v2/account/recipes",      g_m.recipesUnlocked,      g_m.haveRecipes,      g_m.recipesAt); }
    void RefreshEmotesUnlocked()   // /v2/account/emotes returns STRING ids -> FNV each to match the catalog's int ids
    {
        Fetch("/v2/account/emotes", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s; for (const auto& e : j) if (e.is_string()) s.insert(FnvId(e.get<std::string>()));
            g_m.emotesUnlocked = std::move(s); g_m.haveEmotes = true; g_m.emotesAt = Now(); SaveCosmeticsCache();
        });
    }
    // finishers: /v2/account/finishers returns OBJECTS [{id, permanent, quantity}], not bare ids -> extract id.
    void RefreshFinishersUnlocked()
    {
        Fetch("/v2/account/finishers", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s;
            for (const auto& e : j) if (e.is_object() && e.contains("id") && e["id"].is_number_integer()) s.insert(e["id"].get<int>());
            g_m.finishersUnlocked = std::move(s); g_m.haveFinishers = true; g_m.finishersAt = Now(); SaveCosmeticsCache();
        });
    }
    // mist champions: /v2/account/pvp/heroes -> the unlocked pvp-hero SKIN ids (ints, resolvable against the heroes'
    // skins[]) -- they ARE the catalog's skin ids, so it's the plain int-set refresh (per-skin, like gw2efficiency).
    void RefreshMistChampionsUnlocked() { RefreshCosmeticUnlocked("/v2/account/pvp/heroes", g_m.mistChampionsUnlocked, g_m.haveMistChampions, g_m.mistChampionsAt); }
    void RefreshHomestead()   // Items-tab Homestead scope: owned decorations (id->count) + glyph/cat/node unlock sets
    {
        // The onFail handlers mark the data loaded-but-stale-time-advanced so a failed/unavailable fetch (transient
        // 5xx, or an endpoint that errors instead of returning []) is NOT re-issued every Tick -- mirrors RefreshWv;
        // without them homesteadAt stays 0 (= always stale) and Tick re-hits all four endpoints every cycle. They
        // leave the last-known owned set intact (a failure never clobbers good data).
        auto onFail = [](const Api::ApiError&) { g_m.haveHomestead = true; g_m.homesteadAt = Now(); };
        // Decorations: id -> owned count (/v2/account/homestead/decorations).
        Fetch("/v2/account/homestead/decorations", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::map<int, int> m;
            for (const auto& e : j) if (e.is_object()) { int id = e.value("id", 0); if (id > 0) m[id] = e.value("count", 0); }
            g_m.decorationsOwned = std::move(m); g_m.haveHomestead = true; g_m.homesteadAt = Now(); SaveHomesteadCache();
        }, onFail);
        // Glyphs: unlocked glyph id strings (/v2/account/homestead/glyphs).
        Fetch("/v2/account/homestead/glyphs", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<std::string> s; for (const auto& e : j) if (e.is_string()) s.insert(e.get<std::string>());
            g_m.glyphsOwned = std::move(s); g_m.haveHomestead = true; g_m.homesteadAt = Now(); SaveHomesteadCache();
        }, onFail);
        // Home cats: unlocked cat ids (/v2/account/home/cats -- latest schema = ints; tolerate {id,hint} objects).
        Fetch("/v2/account/home/cats", true, true, TokenPermission::Unlocks, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<int> s;
            for (const auto& e : j) { if (e.is_number_integer()) s.insert(e.get<int>()); else if (e.is_object()) s.insert(e.value("id", 0)); }
            s.erase(0);
            g_m.catsOwned = std::move(s); g_m.haveHomestead = true; g_m.homesteadAt = Now(); SaveHomesteadCache();
        }, onFail);
        // Home gathering nodes: unlocked node id strings (/v2/account/home/nodes).
        Fetch("/v2/account/home/nodes", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            if (!j.is_array()) return;
            std::set<std::string> s; for (const auto& e : j) if (e.is_string()) s.insert(e.get<std::string>());
            g_m.nodesOwned = std::move(s); g_m.haveHomestead = true; g_m.homesteadAt = Now(); SaveHomesteadCache();
        }, onFail);
    }
    void RefreshDailyDone()   // today's reset-gated completion lists (counts only)
    {
        Fetch("/v2/account/worldbosses",   true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.worldBosses   = j.is_array() ? (int)j.size() : 0; g_m.haveDailyDone = true; g_m.dailyDoneAt = Now();
        });
        Fetch("/v2/account/mapchests",     true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.mapChests     = j.is_array() ? (int)j.size() : 0; g_m.haveDailyDone = true; g_m.dailyDoneAt = Now();
        });
        Fetch("/v2/account/dailycrafting", true, true, TokenPermission::Progression, {}, [](const nlohmann::json& j) {
            g_m.dailyCrafting = j.is_array() ? (int)j.size() : 0; g_m.haveDailyDone = true; g_m.dailyDoneAt = Now();
        });
    }

    void FetchCharacterInventory(const std::string& name, bool force, bool inventoryIndex)
    {
        if (name.empty() || !g_api || !g_api->HasPermission(TokenPermission::Inventories))
            return;
        const std::string src = "Character: " + name;
        if (inventoryIndex) ExpectInventorySource(src);
        auto cached = g_m.inv.find(name);
        if (!force && cached != g_m.inv.end() && cached->second.have && !cached->second.bags.empty())
        {
            if (inventoryIndex) FinishInventorySource(src);
            return;
        }
        const std::string enc = Api::UrlEncode(name);
        const bool queued = Fetch("/v2/characters/" + enc + "/inventory", true, true, TokenPermission::Inventories, {},
            [name, inventoryIndex, src](const nlohmann::json& j) {
                ApplyCharacterInventory(name, j);
                if (inventoryIndex) FinishInventorySource(src);
            },
            [name, inventoryIndex, src](const Api::ApiError& e) {
                AccountData::CharInv& ci = g_m.inv[name];
                ci.error = e.text.empty() ? "request failed" : e.text;
                if (inventoryIndex) FinishInventorySource(src, ci.error);
            });
        if (inventoryIndex && !queued) FinishInventorySource(src);
    }

    // Fetch one character's equipment (+ its item-icon metas). No-op when already cached unless `force` -- so it
    // serves the disk-cached gear stale and only re-pulls when missing or explicitly refreshed (character switch).
    void FetchCharacterEquipment(const std::string& name, bool force)
    {
        if (name.empty() || !g_api || !g_api->HasPermission(TokenPermission::Characters)) return;
        auto cached = g_m.equip.find(name);
        bool hasReal = false;
        std::vector<int> metaIds;
        if (cached != g_m.equip.end())
            for (const Api::V2::ItemSlot& s : cached->second.pieces)
                if (s.id > 0) { hasReal = true; metaIds.push_back(s.id); for (int u : s.upgrades) metaIds.push_back(u); for (int f : s.infusions) metaIds.push_back(f); }
        // Serve stale ONLY when the cache holds real items: the disk load restores the pieces but not their
        // item-icon metas, so ensure those load (or the cells render blank). A have=true entry with all-empty
        // pieces is a corrupt/failed cache (e.g. from an older build) -- fall through and re-fetch it.
        if (!force && cached != g_m.equip.end() && cached->second.have && hasReal)
        {
            EnsureItemMetadataInternal(metaIds);
            return;
        }
        const std::string enc = Api::UrlEncode(name);
        Fetch("/v2/characters/" + enc + "/equipment", true, true, TokenPermission::Characters, {}, [name](const nlohmann::json& j) {
            AccountData::CharEquip ce; ce.have = true;
            std::vector<int> metaIds;
            if (j.is_object() && j.contains("equipment") && j["equipment"].is_array())
                for (const auto& e : j["equipment"])
                {
                    if (!e.is_object() || !e.contains("slot")) continue;
                    Api::V2::ItemSlot s = Api::V2::ParseItemSlot(e);
                    if (s.id > 0) { metaIds.push_back(s.id); for (int u : s.upgrades) metaIds.push_back(u); for (int f : s.infusions) metaIds.push_back(f); }
                    ce.pieces.push_back(std::move(s));
                }
            g_m.equip[name] = std::move(ce);
            EnsureItemMetadataInternal(metaIds);   // so the equipment cells get icons/names (like bags/bank)
        });
    }

    void SaveCache()
    {
        if (g_cacheFile.empty()) return;
        nlohmann::json j;
        j["accountId"] = g_m.accountId; j["accountName"] = g_m.accountName; j["age"] = g_m.age; j["fractalLevel"] = g_m.fractalLevel; j["wvwRank"] = g_m.wvwRank; j["haveAccount"] = g_m.haveAccount;
        if (g_m.haveWvwMatch)   // full match snapshot -> the scoreboard + rendered map show last-known instantly on reload
        {
            nlohmann::json wv;
            wv["matchId"] = g_m.wvwMatchId; wv["myColor"] = g_m.wvwMyColor; wv["skirmishIndex"] = g_m.wvwSkirmishIndex; wv["have"] = true;
            nlohmann::json sides = nlohmann::json::array();
            for (int c = 0; c < 3; ++c) { const auto& s = g_m.wvwSides[c]; sides.push_back({ {"sc", s.score}, {"vp", s.victoryPoints}, {"pt", s.ppt}, {"sk", s.skirmishScore}, {"k", s.kills}, {"d", s.deaths}, {"kk", s.kdKills}, {"kd", s.kdDeaths}, {"w", s.primaryWorld}, {"n", s.worldName} }); }
            wv["sides"] = std::move(sides);
            nlohmann::json maps = nlohmann::json::array();
            for (const auto& mp : g_m.wvwMaps)
            {
                nlohmann::json mj; mj["t"] = mp.mapType; mj["id"] = mp.mapId; mj["o"] = { mp.ownedByColor[0], mp.ownedByColor[1], mp.ownedByColor[2] };
                nlohmann::json objs = nlohmann::json::array();
                for (const auto& ob : mp.objectives) objs.push_back({ {"i", ob.objId}, {"t", ob.type}, {"o", ob.owner}, {"p", ob.pointsTick}, {"f", ob.lastFlipped} });
                mj["objs"] = std::move(objs); maps.push_back(std::move(mj));
            }
            wv["maps"] = std::move(maps);
            j["wvw"] = std::move(wv);
        }
        { nlohmann::json w = nlohmann::json::array(); for (auto& e : g_m.wallet) w.push_back({ {"id", e.id}, {"value", e.value} }); j["wallet"] = w; j["haveWallet"] = g_m.haveWallet; }
        { nlohmann::json a = nlohmann::json::array(); for (auto& c : g_m.characters) a.push_back({ {"name", c.name}, {"profession", c.profession}, {"race", c.race}, {"level", c.level}, {"age", c.age}, {"deaths", c.deaths} }); j["characters"] = a; j["haveChars"] = g_m.haveChars; }
        j["achInProgress"] = g_m.achInProgress; j["achDone"] = g_m.achDone; j["haveAch"] = g_m.haveAch;
        j["totalAp"] = g_m.totalAp; j["achPermAp"] = g_m.achPermAp; j["dailyAp"] = g_m.dailyAp; j["monthlyAp"] = g_m.monthlyAp; j["haveTotalAp"] = g_m.haveTotalAp;
        j["dailyPve"] = g_m.dailyPve; j["haveDailies"] = g_m.haveDailies;
        j["mpEarned"] = g_m.mpEarned; j["mpSpent"] = g_m.mpSpent; j["masteriesUnlocked"] = g_m.masteriesUnlocked; j["haveMastery"] = g_m.haveMastery;
        j["bankUsed"] = g_m.bankUsed; j["bankTotal"] = g_m.bankTotal; j["bankItems"] = g_m.bankItems; j["haveBank"] = g_m.haveBank;
        j["matTypes"] = g_m.matTypes; j["matTotal"] = g_m.matTotal; j["haveMats"] = g_m.haveMats;
        j["tpCoins"] = g_m.tpCoins; j["tpItems"] = g_m.tpItems; j["tpBuys"] = g_m.tpBuys; j["tpSells"] = g_m.tpSells; j["haveTp"] = g_m.haveTp;
        j["raidsCleared"] = g_m.raidsCleared; j["haveRaids"] = g_m.haveRaids;
        j["raidsClearedIds"] = g_m.raidsClearedIds;                        // per-boss tracker: which encounters cleared this week
        j["dungeonPaths"] = g_m.dungeonPathsToday; j["haveDungeons"] = g_m.haveDungeons;   // per-path tracker: paths done today
        { nlohmann::json a = nlohmann::json::array(); for (auto& o : g_m.wvDaily)  a.push_back({ {"title", o.title}, {"cur", o.cur}, {"max", o.max}, {"claimed", o.claimed} }); j["wvDaily"]  = a; }
        { nlohmann::json a = nlohmann::json::array(); for (auto& o : g_m.wvWeekly) a.push_back({ {"title", o.title}, {"cur", o.cur}, {"max", o.max}, {"claimed", o.claimed} }); j["wvWeekly"] = a; }
        j["wvDailyMetaCur"]  = g_m.wvDailyMetaCur;  j["wvDailyMetaMax"]  = g_m.wvDailyMetaMax;  j["haveWvDaily"]  = g_m.haveWvDaily;
        j["wvWeeklyMetaCur"] = g_m.wvWeeklyMetaCur; j["wvWeeklyMetaMax"] = g_m.wvWeeklyMetaMax; j["haveWvWeekly"] = g_m.haveWvWeekly;
        j["wvUnavailable"] = g_m.wvUnavailable;
        j["luck"] = g_m.luck; j["haveLuck"] = g_m.haveLuck;
        j["worldBosses"] = g_m.worldBosses; j["mapChests"] = g_m.mapChests; j["dailyCrafting"] = g_m.dailyCrafting; j["haveDailyDone"] = g_m.haveDailyDone;
        { nlohmann::json inv = nlohmann::json::object(); for (auto& kv : g_m.inv) if (kv.second.have) inv[kv.first] = { {"used", kv.second.usedSlots}, {"total", kv.second.totalSlots} }; j["inv"] = inv; }
        { nlohmann::json eq = nlohmann::json::object(); for (auto& kv : g_m.equip) if (kv.second.have) { nlohmann::json arr = nlohmann::json::array(); for (const Api::V2::ItemSlot& s : kv.second.pieces) arr.push_back(SlotToJson(s)); eq[kv.first] = arr; } j["equip"] = eq; }
        try { std::ofstream(g_cacheFile) << j.dump(2); } catch (...) {}
    }

    // Legendary Armory has its OWN cache file (legendary-armory.json), like inventory-cache.json -- the catalog
    // (id->max) is global; the owned counts (id->count) are per-account (gated on accountId when restored).
    void SaveLegendaryCache()
    {
        if (g_legendaryCacheFile.empty()) return;
        nlohmann::json j;
        j["version"] = 1;
        j["accountId"] = g_m.accountId;
        j["savedAt"] = Now();
        j["haveLegendary"] = g_m.haveLegendary;
        { nlohmann::json mx = nlohmann::json::object(); for (auto& kv : g_m.legendaryMax)   mx[std::to_string(kv.first)] = kv.second; j["max"]   = mx; }
        { nlohmann::json ow = nlohmann::json::object(); for (auto& kv : g_m.legendaryOwned) ow[std::to_string(kv.first)] = kv.second; j["owned"] = ow; }
        try { std::ofstream(g_legendaryCacheFile) << j.dump(0); } catch (...) {}
    }

    void LoadLegendaryCache()
    {
        if (g_legendaryCacheFile.empty()) return;
        std::ifstream f(g_legendaryCacheFile);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object() || j.value("version", 0) != 1) return;

        if (j.contains("max") && j["max"].is_object())
            for (auto it = j["max"].begin(); it != j["max"].end(); ++it) g_m.legendaryMax[std::atoi(it.key().c_str())] = it.value().get<int>();

        // Owned is per-account -> only restore when the account matches (or either side is unknown).
        const std::string cachedAccountId = j.value("accountId", std::string());
        if (g_m.accountId.empty() || cachedAccountId.empty() || cachedAccountId == g_m.accountId)
            if (j.contains("owned") && j["owned"].is_object())
                for (auto it = j["owned"].begin(); it != j["owned"].end(); ++it) g_m.legendaryOwned[std::atoi(it.key().c_str())] = it.value().get<int>();

        g_m.haveLegendary = j.value("haveLegendary", false) || !g_m.legendaryMax.empty();
        g_m.legendaryAt = 0.0;   // serve stale now; the next Tick/Warm refreshes
    }

    // Wardrobe unlocked sets (skins + dyes) -- own file, per-account (gated on accountId), serve-stale-then-refresh.
    void SaveWardrobeCache()
    {
        if (g_wardrobeCacheFile.empty()) return;
        nlohmann::json j;
        j["version"] = 1;
        j["accountId"] = g_m.accountId;
        j["savedAt"] = Now();
        j["skins"] = std::vector<int>(g_m.skinsUnlocked.begin(), g_m.skinsUnlocked.end());
        j["dyes"]  = std::vector<int>(g_m.dyesUnlocked.begin(),  g_m.dyesUnlocked.end());
        try { std::ofstream(g_wardrobeCacheFile) << j.dump(0); } catch (...) {}
    }

    void LoadWardrobeCache()
    {
        if (g_wardrobeCacheFile.empty()) return;
        std::ifstream f(g_wardrobeCacheFile);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object() || j.value("version", 0) != 1) return;

        // Per-account -> only restore when the account matches (or either side is unknown).
        const std::string cachedAccountId = j.value("accountId", std::string());
        if (!(g_m.accountId.empty() || cachedAccountId.empty() || cachedAccountId == g_m.accountId)) return;

        if (j.contains("skins") && j["skins"].is_array())
            for (const auto& e : j["skins"]) if (e.is_number_integer()) g_m.skinsUnlocked.insert(e.get<int>());
        if (j.contains("dyes") && j["dyes"].is_array())
            for (const auto& e : j["dyes"]) if (e.is_number_integer()) g_m.dyesUnlocked.insert(e.get<int>());
        g_m.haveSkins = !g_m.skinsUnlocked.empty();
        g_m.haveDyes  = !g_m.dyesUnlocked.empty();
        g_m.skinsAt = 0.0; g_m.dyesAt = 0.0;   // serve stale now; the next Tick/Warm refreshes
    }

    // Homestead unlock sets (decorations+counts / glyphs / cats / nodes) -- own file, per-account, serve-stale-then-refresh.
    void SaveHomesteadCache()
    {
        if (g_homesteadCacheFile.empty()) return;
        nlohmann::json j;
        j["version"] = 1;
        j["accountId"] = g_m.accountId;
        j["savedAt"] = Now();
        nlohmann::json dec = nlohmann::json::object();
        for (const auto& [id, c] : g_m.decorationsOwned) dec[std::to_string(id)] = c;
        j["decorations"] = std::move(dec);
        j["glyphs"] = std::vector<std::string>(g_m.glyphsOwned.begin(), g_m.glyphsOwned.end());
        j["cats"]   = std::vector<int>(g_m.catsOwned.begin(),  g_m.catsOwned.end());
        j["nodes"]  = std::vector<std::string>(g_m.nodesOwned.begin(), g_m.nodesOwned.end());
        try { std::ofstream(g_homesteadCacheFile) << j.dump(0); } catch (...) {}
    }

    void LoadHomesteadCache()
    {
        if (g_homesteadCacheFile.empty()) return;
        std::ifstream f(g_homesteadCacheFile);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object() || j.value("version", 0) != 1) return;

        const std::string cachedAccountId = j.value("accountId", std::string());
        if (!(g_m.accountId.empty() || cachedAccountId.empty() || cachedAccountId == g_m.accountId)) return;

        if (j.contains("decorations") && j["decorations"].is_object())
            for (auto it = j["decorations"].begin(); it != j["decorations"].end(); ++it)
                if (it.value().is_number_integer()) g_m.decorationsOwned[std::atoi(it.key().c_str())] = it.value().get<int>();
        if (j.contains("glyphs") && j["glyphs"].is_array())
            for (const auto& e : j["glyphs"]) if (e.is_string()) g_m.glyphsOwned.insert(e.get<std::string>());
        if (j.contains("cats") && j["cats"].is_array())
            for (const auto& e : j["cats"]) if (e.is_number_integer()) g_m.catsOwned.insert(e.get<int>());
        if (j.contains("nodes") && j["nodes"].is_array())
            for (const auto& e : j["nodes"]) if (e.is_string()) g_m.nodesOwned.insert(e.get<std::string>());
        g_m.haveHomestead = !g_m.decorationsOwned.empty() || !g_m.glyphsOwned.empty() || !g_m.catsOwned.empty() || !g_m.nodesOwned.empty();
        g_m.homesteadAt = 0.0;   // serve stale now; the next Tick/Warm refreshes
    }

    // Collections visual-cosmetic unlock sets (mounts/outfits/gliders/jadebots/skiffs/novelties) -- own file,
    // per-account (gated on accountId), serve-stale-then-refresh. Mirrors SaveWardrobeCache.
    void SaveCosmeticsCache()
    {
        if (g_cosmeticsCacheFile.empty()) return;
        nlohmann::json j;
        j["version"] = 1;
        j["accountId"] = g_m.accountId;
        j["savedAt"] = Now();
        j["mounts"]    = std::vector<int>(g_m.mountsUnlocked.begin(),    g_m.mountsUnlocked.end());
        j["outfits"]   = std::vector<int>(g_m.outfitsUnlocked.begin(),   g_m.outfitsUnlocked.end());
        j["gliders"]   = std::vector<int>(g_m.glidersUnlocked.begin(),   g_m.glidersUnlocked.end());
        j["jadebots"]  = std::vector<int>(g_m.jadeBotsUnlocked.begin(),  g_m.jadeBotsUnlocked.end());
        j["skiffs"]    = std::vector<int>(g_m.skiffsUnlocked.begin(),    g_m.skiffsUnlocked.end());
        j["novelties"] = std::vector<int>(g_m.noveltiesUnlocked.begin(), g_m.noveltiesUnlocked.end());
        j["finishers"]     = std::vector<int>(g_m.finishersUnlocked.begin(),     g_m.finishersUnlocked.end());
        j["mailcarriers"]  = std::vector<int>(g_m.mailCarriersUnlocked.begin(),  g_m.mailCarriersUnlocked.end());
        j["minis"]         = std::vector<int>(g_m.minisUnlocked.begin(),         g_m.minisUnlocked.end());
        j["mistchampions"] = std::vector<int>(g_m.mistChampionsUnlocked.begin(), g_m.mistChampionsUnlocked.end());
        j["titles"]        = std::vector<int>(g_m.titlesUnlocked.begin(),        g_m.titlesUnlocked.end());
        j["emotes"]        = std::vector<int>(g_m.emotesUnlocked.begin(),        g_m.emotesUnlocked.end());
        j["recipes"]       = std::vector<int>(g_m.recipesUnlocked.begin(),       g_m.recipesUnlocked.end());
        try { std::ofstream(g_cosmeticsCacheFile) << j.dump(0); } catch (...) {}
    }

    void LoadCosmeticsCache()
    {
        if (g_cosmeticsCacheFile.empty()) return;
        std::ifstream f(g_cosmeticsCacheFile);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object() || j.value("version", 0) != 1) return;

        const std::string cachedAccountId = j.value("accountId", std::string());
        if (!(g_m.accountId.empty() || cachedAccountId.empty() || cachedAccountId == g_m.accountId)) return;

        auto loadSet = [&](const char* key, std::set<int>& out, bool& have) {
            if (j.contains(key) && j[key].is_array())
                for (const auto& e : j[key]) if (e.is_number_integer()) out.insert(e.get<int>());
            have = !out.empty();
        };
        loadSet("mounts",    g_m.mountsUnlocked,    g_m.haveMounts);
        loadSet("outfits",   g_m.outfitsUnlocked,   g_m.haveOutfits);
        loadSet("gliders",   g_m.glidersUnlocked,   g_m.haveGliders);
        loadSet("jadebots",  g_m.jadeBotsUnlocked,  g_m.haveJadeBots);
        loadSet("skiffs",    g_m.skiffsUnlocked,    g_m.haveSkiffs);
        loadSet("novelties", g_m.noveltiesUnlocked, g_m.haveNovelties);
        loadSet("finishers",     g_m.finishersUnlocked,     g_m.haveFinishers);
        loadSet("mailcarriers",  g_m.mailCarriersUnlocked,  g_m.haveMailCarriers);
        loadSet("minis",         g_m.minisUnlocked,         g_m.haveMinis);
        loadSet("mistchampions", g_m.mistChampionsUnlocked, g_m.haveMistChampions);
        loadSet("titles",        g_m.titlesUnlocked,        g_m.haveTitles);
        loadSet("emotes",        g_m.emotesUnlocked,        g_m.haveEmotes);
        loadSet("recipes",       g_m.recipesUnlocked,       g_m.haveRecipes);
        // serve stale now; the next Tick/Warm refreshes
        g_m.mountsAt = g_m.outfitsAt = g_m.glidersAt = g_m.jadeBotsAt = g_m.skiffsAt = g_m.noveltiesAt = 0.0;
        g_m.finishersAt = g_m.mailCarriersAt = g_m.minisAt = g_m.mistChampionsAt = g_m.titlesAt = g_m.emotesAt = g_m.recipesAt = 0.0;
    }

    void SaveInventoryCache()
    {
        if (g_inventoryCacheFile.empty()) return;
        nlohmann::json j;
        j["version"] = 1;
        j["accountId"] = g_m.accountId;
        j["accountName"] = g_m.accountName;
        j["savedAt"] = Now();
        j["bankAt"] = g_m.bankAt;
        j["sharedAt"] = g_m.sharedAt;
        j["matsAt"] = g_m.matsAt;
        j["inventoryAt"] = g_m.inventoryStatus.lastRefreshAt;

        nlohmann::json shared = nlohmann::json::array();
        for (const Api::V2::ItemSlot& s : g_m.sharedSlots) shared.push_back(SlotToJson(s));
        j["sharedSlots"] = shared;

        nlohmann::json bank = nlohmann::json::array();
        for (const Api::V2::ItemSlot& s : g_m.bankSlots) bank.push_back(SlotToJson(s));
        j["bankSlots"] = bank;

        nlohmann::json mats = nlohmann::json::array();
        for (const Api::V2::MaterialStack& m : g_m.materials) mats.push_back(MaterialToJson(m));
        j["materials"] = mats;

        nlohmann::json chars = nlohmann::json::object();
        for (const auto& kv : g_m.inv)
        {
            const AccountData::CharInv& ci = kv.second;
            if (!ci.have || ci.bags.empty()) continue;
            nlohmann::json cj;
            cj["used"] = ci.usedSlots;
            cj["total"] = ci.totalSlots;
            cj["at"] = ci.at;
            nlohmann::json bags = nlohmann::json::array();
            for (const AccountData::CharBag& b : ci.bags)
            {
                nlohmann::json bj;
                bj["present"] = b.present;
                bj["id"] = b.id;
                bj["size"] = b.size;
                nlohmann::json slots = nlohmann::json::array();
                for (const Api::V2::ItemSlot& s : b.slots) slots.push_back(SlotToJson(s));
                bj["slots"] = slots;
                bags.push_back(std::move(bj));
            }
            cj["bags"] = bags;
            chars[kv.first] = cj;
        }
        j["characters"] = chars;

        nlohmann::json items = nlohmann::json::object();
        for (const auto& kv : g_m.itemMeta)
            if (kv.first > 0 && kv.second.have)
                items[std::to_string(kv.first)] = ItemMetaToJson(kv.second);
        j["items"] = items;

        try { std::ofstream(g_inventoryCacheFile) << j.dump(2); } catch (...) {}
    }

    void LoadInventoryCache()
    {
        if (g_inventoryCacheFile.empty()) return;
        std::ifstream f(g_inventoryCacheFile);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object() || j.value("version", 0) != 1) return;

        const std::string cachedAccountId = j.value("accountId", std::string());
        if (!g_m.accountId.empty() && !cachedAccountId.empty() && cachedAccountId != g_m.accountId)
            return;
        if (g_m.accountId.empty()) g_m.accountId = cachedAccountId;
        if (g_m.accountName.empty()) g_m.accountName = j.value("accountName", std::string());

        g_m.sharedSlots = ParseItemSlotArray(j.value("sharedSlots", nlohmann::json::array()));
        g_m.sharedTotal = (int)g_m.sharedSlots.size();
        g_m.sharedUsed = 0;
        for (const Api::V2::ItemSlot& s : g_m.sharedSlots) if (s.id > 0) ++g_m.sharedUsed;
        g_m.haveShared = !g_m.sharedSlots.empty();
        g_m.sharedAt = 0.0;

        g_m.bankSlots = ParseItemSlotArray(j.value("bankSlots", nlohmann::json::array()));
        g_m.bankTotal = (int)g_m.bankSlots.size();
        g_m.bankUsed = 0; g_m.bankItems = 0;
        for (const Api::V2::ItemSlot& s : g_m.bankSlots) if (s.id > 0) { ++g_m.bankUsed; g_m.bankItems += std::max(1, s.count); }
        g_m.haveBank = !g_m.bankSlots.empty();
        g_m.bankAt = 0.0;

        g_m.materials.clear();
        g_m.matTypes = 0; g_m.matTotal = 0;
        if (auto it = j.find("materials"); it != j.end() && it->is_array())
        {
            for (const auto& e : *it)
            {
                if (!e.is_object()) continue;
                Api::V2::MaterialStack m;
                m.id = e.value("id", 0);
                m.category = e.value("category", 0);
                m.count = e.value("count", 0);
                m.binding = e.value("binding", std::string());
                m.raw = e;
                if (m.count > 0) { ++g_m.matTypes; g_m.matTotal += m.count; }
                g_m.materials.push_back(std::move(m));
            }
        }
        g_m.haveMats = !g_m.materials.empty();
        g_m.matsAt = 0.0;

        if (auto it = j.find("characters"); it != j.end() && it->is_object())
        {
            for (auto ciIt = it->begin(); ciIt != it->end(); ++ciIt)
            {
                if (!ciIt.value().is_object()) continue;
                AccountData::CharInv ci;
                ci.have = true;
                ci.usedSlots = ciIt.value().value("used", 0);
                ci.totalSlots = ciIt.value().value("total", 0);
                ci.at = 0.0;
                if (auto bagsIt = ciIt.value().find("bags"); bagsIt != ciIt.value().end() && bagsIt->is_array())
                {
                    for (const auto& bj : *bagsIt)
                    {
                        AccountData::CharBag bag;
                        if (bj.is_object())
                        {
                            bag.present = bj.value("present", false);
                            bag.id = bj.value("id", 0);
                            bag.size = bj.value("size", 0);
                            bag.slots = ParseItemSlotArray(bj.value("slots", nlohmann::json::array()));
                            while ((int)bag.slots.size() < bag.size)
                                bag.slots.push_back(Api::V2::ItemSlot{});
                        }
                        ci.bags.push_back(std::move(bag));
                    }
                }
                g_m.inv[ciIt.key()] = std::move(ci);
                bool known = false;
                for (const AccountData::AcctChar& ch : g_m.characters)
                    if (ch.name == ciIt.key()) { known = true; break; }
                if (!known)
                {
                    AccountData::AcctChar ch;
                    ch.name = ciIt.key();
                    g_m.characters.push_back(std::move(ch));
                    g_m.haveChars = true;
                }
            }
        }

        if (auto it = j.find("items"); it != j.end() && it->is_object())
        {
            for (auto mi = it->begin(); mi != it->end(); ++mi)
            {
                AccountData::ItemMeta meta = ParseItemMeta(mi.value());
                if (meta.id > 0)
                {
                    if (!meta.icon.empty())
                    {
                        char texId[40];
                        std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", meta.id);
                        ImageCache::PrefetchUrl(texId, meta.icon.c_str());
                    }
                    g_m.itemMeta[meta.id] = std::move(meta);
                }
            }
        }

        g_m.inventoryStatus.state = AccountData::InventoryState::Idle;
        g_m.inventoryStatus.lastRefreshAt = 0.0;
        g_inventoryLoadedFromDisk = !g_m.inv.empty() || !g_m.bankSlots.empty() || !g_m.sharedSlots.empty() || !g_m.materials.empty();
    }

    void ClearInventoryDetails()
    {
        g_m.bankSlots.clear();
        g_m.sharedSlots.clear();
        g_m.materials.clear();
        g_m.itemMeta.clear();
        for (auto& kv : g_m.inv)
        {
            kv.second.bags.clear();
            kv.second.have = false;
        }
        g_m.haveBank = g_m.haveShared = g_m.haveMats = false;
        g_m.bankUsed = g_m.bankTotal = g_m.bankItems = 0;
        g_m.sharedUsed = g_m.sharedTotal = 0;
        g_m.matTypes = 0; g_m.matTotal = 0;
        g_m.inventoryStatus = AccountData::InventoryIndexStatus{};
        g_inventoryLoadedFromDisk = false;
        g_inventoryForceCurrentIndex = false;
    }
}

void AccountData::Init(Api::Client* api, std::string cacheDir)
{
    g_api = api;
    g_cacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\dashboard-cache.json");
    g_inventoryCacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\inventory-cache.json");
    g_legendaryCacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\legendary-armory.json");
    g_wardrobeCacheFile  = cacheDir.empty() ? std::string() : (cacheDir + "\\wardrobe-unlocks.json");
    g_homesteadCacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\homestead-cache.json");
    g_cosmeticsCacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\cosmetics-unlocks.json");
}

void AccountData::Shutdown()
{
    // Free the account model (item metadata + full inventory index can be several MB after browsing) so a
    // disable/re-enable cycle doesn't retain/compound it. The disk caches persist; LoadCache re-reads them.
    g_m = AccountData::Model{};
    g_lastActive.clear();
    g_inflight.clear(); g_invExpected.clear(); g_invDone.clear(); g_invErrors.clear(); g_itemPending.clear();
    g_inventoryIndexRequested = false; g_inventoryLoadedFromDisk = false; g_inventoryForceCurrentIndex = false;
    g_cacheFile.clear(); g_inventoryCacheFile.clear(); g_legendaryCacheFile.clear(); g_wardrobeCacheFile.clear(); g_homesteadCacheFile.clear(); g_cosmeticsCacheFile.clear();
    g_api = nullptr;
}

void AccountData::LoadCache()
{
    if (g_cacheFile.empty()) { LoadInventoryCache(); LoadLegendaryCache(); LoadWardrobeCache(); LoadHomesteadCache(); LoadCosmeticsCache(); return; }
    std::ifstream f(g_cacheFile);
    if (!f) { LoadInventoryCache(); LoadLegendaryCache(); LoadWardrobeCache(); LoadHomesteadCache(); LoadCosmeticsCache(); return; }
    nlohmann::json j;
    try { f >> j; } catch (...) { LoadInventoryCache(); LoadLegendaryCache(); LoadWardrobeCache(); LoadHomesteadCache(); LoadCosmeticsCache(); return; }
    if (!j.is_object()) { LoadInventoryCache(); LoadLegendaryCache(); LoadWardrobeCache(); LoadHomesteadCache(); LoadCosmeticsCache(); return; }

    g_m.accountId = j.value("accountId", std::string()); g_m.accountName = j.value("accountName", std::string()); g_m.age = j.value("age", 0L);
    g_m.fractalLevel = j.value("fractalLevel", 0); g_m.wvwRank = j.value("wvwRank", 0); g_m.haveAccount = j.value("haveAccount", false);
    if (j.contains("wvw") && j["wvw"].is_object())   // last-known WvW match (re-verified by ResolveWvwTeam: wvwTeamId left 0)
    {
        const auto& wv = j["wvw"];
        g_m.wvwMatchId = wv.value("matchId", std::string());
        g_m.wvwMyColor = wv.value("myColor", 0);
        g_m.wvwSkirmishIndex = wv.value("skirmishIndex", 0);
        g_m.haveWvwMatch = wv.value("have", false);
        if (wv.contains("sides") && wv["sides"].is_array())
        {
            int c = 0;
            for (const auto& s : wv["sides"]) { if (c >= 3) break; if (!s.is_object()) continue; WvwSide& d = g_m.wvwSides[c]; d.score = s.value("sc", 0LL); d.victoryPoints = s.value("vp", 0); d.ppt = s.value("pt", 0LL); d.skirmishScore = s.value("sk", 0); d.kills = s.value("k", 0LL); d.deaths = s.value("d", 0LL); d.kdKills = s.value("kk", 0LL); d.kdDeaths = s.value("kd", 0LL); d.primaryWorld = s.value("w", 0); d.worldName = s.value("n", std::string()); ++c; }
        }
        g_m.wvwMaps.clear();
        if (wv.contains("maps") && wv["maps"].is_array())
            for (const auto& mj : wv["maps"])
            {
                if (!mj.is_object()) continue;
                WvwMapState ms; ms.mapType = mj.value("t", -1); ms.mapId = mj.value("id", 0);
                if (mj.contains("o") && mj["o"].is_array() && mj["o"].size() >= 3) for (int i = 0; i < 3; ++i) if (mj["o"][i].is_number_integer()) ms.ownedByColor[i] = mj["o"][i].get<int>();
                if (mj.contains("objs") && mj["objs"].is_array())
                    for (const auto& oj : mj["objs"]) { if (!oj.is_object()) continue; WvwObjState os; os.objId = oj.value("i", std::string()); os.type = oj.value("t", std::string()); os.owner = oj.value("o", 0); os.pointsTick = oj.value("p", 0LL); os.lastFlipped = oj.value("f", 0.0); ms.objectives.push_back(std::move(os)); }
                g_m.wvwMaps.push_back(std::move(ms));
            }
    }
    if (j.contains("wallet") && j["wallet"].is_array()) { g_m.wallet.clear(); for (auto& e : j["wallet"]) if (e.is_object()) g_m.wallet.push_back({ e.value("id", 0), e.value("value", 0LL) }); }
    g_m.haveWallet = j.value("haveWallet", false);
    if (j.contains("characters") && j["characters"].is_array()) { g_m.characters.clear(); for (auto& c : j["characters"]) { if (!c.is_object()) continue; AcctChar ch; ch.name = c.value("name", std::string()); ch.profession = c.value("profession", std::string()); ch.race = c.value("race", std::string()); ch.level = c.value("level", 0); ch.age = c.value("age", 0L); ch.deaths = c.value("deaths", 0); if (!ch.name.empty()) g_m.characters.push_back(std::move(ch)); } }
    g_m.haveChars = j.value("haveChars", false);
    g_m.achInProgress = j.value("achInProgress", 0); g_m.achDone = j.value("achDone", 0); g_m.haveAch = j.value("haveAch", false);
    g_m.totalAp = j.value("totalAp", 0LL); g_m.achPermAp = j.value("achPermAp", 0LL); g_m.dailyAp = j.value("dailyAp", 0); g_m.monthlyAp = j.value("monthlyAp", 0); g_m.haveTotalAp = j.value("haveTotalAp", false);
    if (j.contains("dailyPve") && j["dailyPve"].is_array()) { g_m.dailyPve.clear(); for (auto& e : j["dailyPve"]) if (e.is_string()) g_m.dailyPve.push_back(e.get<std::string>()); }
    g_m.haveDailies = j.value("haveDailies", false);
    g_m.mpEarned = j.value("mpEarned", 0); g_m.mpSpent = j.value("mpSpent", 0); g_m.masteriesUnlocked = j.value("masteriesUnlocked", 0); g_m.haveMastery = j.value("haveMastery", false);
    g_m.bankUsed = j.value("bankUsed", 0); g_m.bankTotal = j.value("bankTotal", 0); g_m.bankItems = j.value("bankItems", 0); g_m.haveBank = j.value("haveBank", false);
    g_m.matTypes = j.value("matTypes", 0); g_m.matTotal = j.value("matTotal", 0LL); g_m.haveMats = j.value("haveMats", false);
    g_m.tpCoins = j.value("tpCoins", 0LL); g_m.tpItems = j.value("tpItems", 0); g_m.tpBuys = j.value("tpBuys", 0); g_m.tpSells = j.value("tpSells", 0); g_m.haveTp = j.value("haveTp", false);
    g_m.raidsCleared = j.value("raidsCleared", 0); g_m.haveRaids = j.value("haveRaids", false);
    if (j.contains("raidsClearedIds") && j["raidsClearedIds"].is_array()) { g_m.raidsClearedIds.clear(); for (auto& e : j["raidsClearedIds"]) if (e.is_string()) g_m.raidsClearedIds.push_back(e.get<std::string>()); }
    if (j.contains("dungeonPaths") && j["dungeonPaths"].is_array()) { g_m.dungeonPathsToday.clear(); for (auto& e : j["dungeonPaths"]) if (e.is_string()) g_m.dungeonPathsToday.insert(e.get<std::string>()); }
    g_m.haveDungeons = j.value("haveDungeons", false);
    {   // Wizard's Vault objectives -- the daily/weekly tiles every Content-tab tracker shows. Caching them here
        // (they were omitted) is what makes the "Daily Fractals" etc. counts appear INSTANTLY instead of after login.
        auto wvLoad = [](const nlohmann::json& arr, std::vector<AccountData::WvObjective>& out) {
            if (!arr.is_array()) return; out.clear();
            for (auto& o : arr) { if (!o.is_object()) continue; AccountData::WvObjective w; w.title = o.value("title", std::string()); w.cur = o.value("cur", 0); w.max = o.value("max", 0); w.claimed = o.value("claimed", false); out.push_back(std::move(w)); }
        };
        if (j.contains("wvDaily"))  wvLoad(j["wvDaily"],  g_m.wvDaily);
        if (j.contains("wvWeekly")) wvLoad(j["wvWeekly"], g_m.wvWeekly);
    }
    g_m.wvDailyMetaCur  = j.value("wvDailyMetaCur", 0);  g_m.wvDailyMetaMax  = j.value("wvDailyMetaMax", 0);  g_m.haveWvDaily  = j.value("haveWvDaily", false);
    g_m.wvWeeklyMetaCur = j.value("wvWeeklyMetaCur", 0); g_m.wvWeeklyMetaMax = j.value("wvWeeklyMetaMax", 0); g_m.haveWvWeekly = j.value("haveWvWeekly", false);
    g_m.wvUnavailable = j.value("wvUnavailable", false);
    g_m.luck = j.value("luck", 0LL); g_m.haveLuck = j.value("haveLuck", false);
    g_m.worldBosses = j.value("worldBosses", 0); g_m.mapChests = j.value("mapChests", 0); g_m.dailyCrafting = j.value("dailyCrafting", 0); g_m.haveDailyDone = j.value("haveDailyDone", false);
    if (j.contains("inv") && j["inv"].is_object()) for (auto it = j["inv"].begin(); it != j["inv"].end(); ++it) { if (!it.value().is_object()) continue; CharInv ci; ci.have = true; ci.usedSlots = it.value().value("used", 0); ci.totalSlots = it.value().value("total", 0); g_m.inv[it.key()] = ci; }
    if (j.contains("equip") && j["equip"].is_object()) for (auto it = j["equip"].begin(); it != j["equip"].end(); ++it) { CharEquip ce; ce.have = true; if (it.value().is_array()) ce.pieces = ParseItemSlotArray(it.value()); g_m.equip[it.key()] = ce; }
    // all timestamps left 0 -> the first Tick/Warm refreshes them (stale-then-fresh)
    LoadInventoryCache();
    LoadLegendaryCache();   // after accountId is known, so the per-account owned counts gate correctly
    LoadWardrobeCache();    // skins/dyes unlocked sets (per-account, gated)
    LoadHomesteadCache();   // homestead owned sets (decorations+counts/glyphs/cats/nodes, per-account, gated)
    LoadCosmeticsCache();   // visual-cosmetic unlock sets (mounts/outfits/gliders/jadebots/skiffs/novelties, per-account, gated)
}

void AccountData::EnsureCharDetail(const std::string& name, bool force)
{
    if (name.empty() || !g_api) return;
    const auto invIt = g_m.inv.find(name);
    const bool haveInv = invIt != g_m.inv.end() && invIt->second.have && !invIt->second.bags.empty();
    const bool haveEquip = g_m.equip.find(name) != g_m.equip.end() && g_m.equip[name].have;
    if (!force && haveInv && haveEquip) return;  // already cached
    if (force || !haveInv) FetchCharacterInventory(name, force, false);
    if (force || !haveEquip) FetchCharacterEquipment(name, force);
}

void AccountData::EnsureEquipAttributes(const std::string& name)
{
    auto it = g_m.equip.find(name);
    if (it == g_m.equip.end() || !it->second.have || it->second.haveAttrs) return;

    int level = 0; std::string prof;
    for (const AcctChar& c : g_m.characters) if (c.name == name) { level = c.level; prof = c.profession; break; }
    if (level <= 0 || prof.empty()) return;   // character detail not loaded yet -> try again next frame

    // All piece + upgrade + infusion metas must be present, or the sum would be partial (and then locked in).
    auto haveMeta = [&](int id) { return id <= 0 || g_m.itemMeta.count(id) != 0; };
    for (const Api::V2::ItemSlot& s : it->second.pieces)
    {
        if (!haveMeta(s.id)) return;
        for (int u : s.upgrades)  if (!haveMeta(u)) return;
        for (int f : s.infusions) if (!haveMeta(f)) return;
    }
    auto metaFor = [&](int id) -> const ItemMeta* { auto m = g_m.itemMeta.find(id); return m == g_m.itemMeta.end() ? nullptr : &m->second; };
    it->second.attributes = ItemAttributes::ParseCharacter(level, prof, it->second.pieces, metaFor);
    it->second.haveAttrs = true;
}

void AccountData::WarmAll()
{
    if (!g_api || !g_api->HasKey()) return;
    RefreshAccount(); RefreshWallet(); RefreshChars(); RefreshAch(); RefreshDailies();
    RefreshMastery(); RefreshTp(); RefreshLegendaryArmory(); RefreshSkinsUnlocked(); RefreshDyesUnlocked(); RefreshHomestead();
    RefreshMountsUnlocked(); RefreshOutfitsUnlocked(); RefreshGlidersUnlocked(); RefreshJadeBotsUnlocked(); RefreshSkiffsUnlocked(); RefreshNoveltiesUnlocked();
    RefreshFinishersUnlocked(); RefreshMailCarriersUnlocked(); RefreshMinisUnlocked(); RefreshMistChampionsUnlocked(); RefreshTitlesUnlocked(); RefreshEmotesUnlocked(); RefreshRecipesUnlocked();
    WarmInventoryIndex(false);   // covers shared + bank + materials + per-character bags (superset of RefreshBank/Mats)
    // Warm EVERY character's equipment too (served stale from the disk cache; only the missing ones are pulled).
    // Deferred to the RefreshChars callback when the character list isn't known yet -- same pattern as inventories.
    if (g_m.haveChars && !g_m.characters.empty()) QueueCharacterEquipment(false);
    else g_equipWarmRequested = true;
    const std::string cur = CurrentCharName();
    if (!cur.empty()) { EnsureCharDetail(cur, true); g_lastActive = cur; }   // force-refresh the active character's gear/bags
}

void AccountData::TickWvw(bool wantSurface, bool inWvw)
{
    // WvW match data is ANONYMOUS (no key needed), so this is separate from Tick (which bails without a key).
    // Conditional: poll only when a WvW surface is shown OR you're in a WvW map. Tiered: fast in WvW, slow in PvE.
    if (!g_api) return;
    if (!wantSurface && !inWvw) return;
    if (g_m.wvwMatchId.empty() || Stale(g_m.wvwTeamAt, 6.0 * 3600.0)) ResolveWvwTeam();   // weekly matchups -> rare re-resolve
    if (g_m.wvwMatchId.empty()) return;   // not resolved yet (awaiting team id / overview scan)
    if (Stale(g_m.wvwMatchAt, inWvw ? 15.0 : 45.0)) RefreshWvwMatch();
}

const char* AccountData::WvwLoadStage()
{
    static std::string s;
    if (g_m.haveWvwMatch)   return "";
    if (!g_m.wvwError.empty())   { s = "WvW: " + g_m.wvwError; return s.c_str(); }            // the actual fetch error
    if (g_m.wvwTeamId == 0)      return "Resolving WvW team...";                              // awaiting /v2/account/wvw or MumbleLink
    if (g_m.wvwMatchId.empty())  { char b[64]; std::snprintf(b, sizeof(b), "Finding match (team %d)...", g_m.wvwTeamId); s = b; return s.c_str(); }
    return "Loading WvW match...";
}

void AccountData::Tick(unsigned interestMask)
{
    if (!g_api) return;
    g_m.keyPresent = g_api->HasKey();
    if (!g_m.keyPresent) return;

    // character switch: refresh the character you just left (its gear/bags changed) + the new active one.
    const std::string cur = CurrentCharName();
    if (!cur.empty() && cur != g_lastActive)
    {
        if (!g_lastActive.empty()) EnsureCharDetail(g_lastActive, true);   // the just-left character's gear/bags changed
        EnsureCharDetail(cur, true);
        RefreshChars();   // levels may have changed
        g_lastActive = cur;
    }

    if (interestMask == 0) return;   // gentle TTL only for the domains a visible consumer is showing

    if ((interestMask & DomAccount)      && Stale(g_m.accountAt, 1800)) RefreshAccount();
    if ((interestMask & DomWallet)       && Stale(g_m.walletAt,  300))  RefreshWallet();
    if ((interestMask & DomCharacters)   && Stale(g_m.charsAt,   1800)) RefreshChars();
    if ((interestMask & DomAchievements) && Stale(g_m.achAt,     600))  RefreshAch();
    if ((interestMask & DomDailies)      && Stale(g_m.dailiesAt, 3600)) RefreshDailies();
    if ((interestMask & DomMastery)      && Stale(g_m.masteryAt, 1800)) RefreshMastery();
    if ((interestMask & DomBank)         && Stale(g_m.bankAt,    600))  RefreshBank();
    if ((interestMask & DomMaterials)    && Stale(g_m.matsAt,    600))  RefreshMats();
    if ((interestMask & DomTradingPost)  && Stale(g_m.tpAt,      300))  RefreshTp();
    if ((interestMask & DomWizardVault)  && Stale(g_m.wvDailyAt, 600))  RefreshWv();
    if ((interestMask & DomShared)       && Stale(g_m.sharedAt,  600))  RefreshShared();
    if ((interestMask & DomRaids)        && Stale(g_m.raidsAt,    900)) RefreshRaids();
    if ((interestMask & DomDungeons)     && Stale(g_m.dungeonsAt, 300)) RefreshDungeons();
    if ((interestMask & DomLuck)         && Stale(g_m.luckAt,    1800)) RefreshLuck();
    if ((interestMask & DomDailyDone)    && Stale(g_m.dailyDoneAt, 600)) RefreshDailyDone();
    if ((interestMask & DomLegendaryArmory) && Stale(g_m.legendaryAt, 1800)) RefreshLegendaryArmory();
    if ((interestMask & DomSkins)           && Stale(g_m.skinsAt,     1800)) RefreshSkinsUnlocked();
    if ((interestMask & DomDyes)            && Stale(g_m.dyesAt,      1800)) RefreshDyesUnlocked();
    if ((interestMask & DomHomestead)       && Stale(g_m.homesteadAt, 1800)) RefreshHomestead();
    if ((interestMask & DomMounts)          && Stale(g_m.mountsAt,     1800)) RefreshMountsUnlocked();
    if ((interestMask & DomOutfits)         && Stale(g_m.outfitsAt,    1800)) RefreshOutfitsUnlocked();
    if ((interestMask & DomGliders)         && Stale(g_m.glidersAt,    1800)) RefreshGlidersUnlocked();
    if ((interestMask & DomJadeBots)        && Stale(g_m.jadeBotsAt,   1800)) RefreshJadeBotsUnlocked();
    if ((interestMask & DomSkiffs)          && Stale(g_m.skiffsAt,     1800)) RefreshSkiffsUnlocked();
    if ((interestMask & DomNovelties)       && Stale(g_m.noveltiesAt,  1800)) RefreshNoveltiesUnlocked();
    if ((interestMask & DomFinishers)       && Stale(g_m.finishersAt,     1800)) RefreshFinishersUnlocked();
    if ((interestMask & DomMailCarriers)    && Stale(g_m.mailCarriersAt,  1800)) RefreshMailCarriersUnlocked();
    if ((interestMask & DomMinis)           && Stale(g_m.minisAt,         1800)) RefreshMinisUnlocked();
    if ((interestMask & DomMistChampions)   && Stale(g_m.mistChampionsAt, 1800)) RefreshMistChampionsUnlocked();
    if ((interestMask & DomTitles)          && Stale(g_m.titlesAt,        1800)) RefreshTitlesUnlocked();
    if ((interestMask & DomEmotes)          && Stale(g_m.emotesAt,        1800)) RefreshEmotesUnlocked();
    if ((interestMask & DomRecipes)         && Stale(g_m.recipesAt,       1800)) RefreshRecipesUnlocked();
}

const AccountData::Model& AccountData::Get() { return g_m; }
bool   AccountData::HasKey()                 { return g_api && g_api->HasKey(); }
bool   AccountData::HasScope(TokenPermission p) { return g_api && g_api->HasPermission(p); }
bool   AccountData::Refreshing()             { return !g_inflight.empty(); }
void AccountData::WarmInventoryIndex(bool force)
{
    if (!g_api || !g_api->HasKey()) return;
    BeginInventoryIndex(force);
    g_inventoryForceCurrentIndex = force || g_inventoryLoadedFromDisk;
    g_inventoryLoadedFromDisk = false;
    if (!g_api->HasPermission(TokenPermission::Inventories))
    {
        g_invExpected.insert("Storage");
        FinishInventorySource("Storage", "needs 'inventories' scope");
        return;
    }
    RefreshShared(true);
    RefreshBank(true);
    RefreshMats(true);
    StaticData::WarmMaterials();   // warm the category-name catalog with the storage stacks (so names are ready, not lazy)
    if (g_api->HasPermission(TokenPermission::Characters))
    {
        if (g_m.haveChars && !g_m.characters.empty()) QueueCharacterInventories(g_inventoryForceCurrentIndex);
        else RefreshChars();
    }
    else
    {
        g_invExpected.insert("Characters");
        FinishInventorySource("Characters", "needs 'characters' scope");
    }
    SyncInventoryStatus();
}

void AccountData::RefreshInventorySource(const std::string& sourceKey)
{
    if (sourceKey.empty() || sourceKey == "all") { WarmInventoryIndex(true); return; }
    BeginInventoryIndex(false);
    if (sourceKey == "shared") { RefreshShared(true); return; }
    if (sourceKey == "bank") { RefreshBank(true); return; }
    if (sourceKey == "materials") { RefreshMats(true); return; }
    const std::string prefix = "char:";
    if (sourceKey.rfind(prefix, 0) == 0)
    {
        FetchCharacterInventory(sourceKey.substr(prefix.size()), true, true);
        return;
    }
}

const AccountData::InventoryIndexStatus& AccountData::InventoryStatus()
{
    SyncInventoryStatus();
    return g_m.inventoryStatus;
}

void AccountData::EnsureItemMetadata(const std::vector<int>& ids)
{
    EnsureItemMetadataInternal(ids);
}

double AccountData::LastRefresh()
{
    double m = 0;
    for (double t : { g_m.accountAt, g_m.walletAt, g_m.charsAt, g_m.achAt, g_m.dailiesAt, g_m.masteryAt, g_m.bankAt, g_m.sharedAt, g_m.matsAt, g_m.tpAt }) m = std::max(m, t);
    return m;
}

void AccountData::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    bool any = false;
    auto i = g_m.inv.find(from);
    if (i != g_m.inv.end())   { g_m.inv[to]   = std::move(i->second); g_m.inv.erase(i);   any = true; }
    auto e = g_m.equip.find(from);
    if (e != g_m.equip.end()) { g_m.equip[to] = std::move(e->second); g_m.equip.erase(e); any = true; }
    if (any) SaveCache();
}

void AccountData::PurgeChar(const std::string& name)
{
    bool any = g_m.inv.erase(name) != 0;
    any = (g_m.equip.erase(name) != 0) || any;
    if (any) SaveCache();
}

void AccountData::CollectCharNames(std::set<std::string>& out)
{
    for (const auto& kv : g_m.inv)   out.insert(kv.first);
    for (const auto& kv : g_m.equip) out.insert(kv.first);
    for (const AcctChar& c : g_m.characters) if (!c.name.empty()) out.insert(c.name);   // live roster (no cache yet)
}
