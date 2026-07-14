#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "api/core/Permissions.h"
#include "api/v2/Common.h"
#include "api/v2/Account.h"

namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// AccountData: the ONE shared, disk-cached model behind every API-backed dashboard widget (wallet, dailies,
// achievements, bags, mastery, trading post, account, characters, alt inventory/equipment, bank). Mirrors the
// Journal's cache-then-refresh: LoadCache at startup so widgets show last-known data INSTANTLY, then a
// background refresh (via the existing API client) updates the model + rewrites the disk cache. Refresh is
// event-driven (login once, on character switch for the just-left character, and a gentle TTL while the
// dashboard is open) so alts are not polled on a timer. All callbacks land on the render thread (api.Pump()).
// -----------------------------------------------------------------------------------------------------
namespace AccountData
{
    struct WalletEntry  { int id = 0; long long value = 0; };
    struct WvObjective  { std::string title; int cur = 0, max = 0; bool claimed = false; };   // Wizard's Vault daily/weekly objective
    struct AcctChar    { std::string name, profession, race; int level = 0; long age = 0; int deaths = 0; };
    struct CharBag     { bool present = false; int id = 0; int size = 0; std::vector<Api::V2::ItemSlot> slots; };
    struct CharInv     { bool have = false; int usedSlots = 0, totalSlots = 0; std::vector<CharBag> bags; double at = 0; std::string error; };
    // Equipped pieces (each ItemSlot carries its `slot` name, id, upgrades/infusions/stats) + the computed
    // equipment-based attributes (Power/Precision/... + secondaries; filled once pieces + metas are loaded).
    struct CharEquip   { bool have = false; std::vector<Api::V2::ItemSlot> pieces; std::map<std::string, double> attributes; bool haveAttrs = false; };
    struct ItemMeta
    {
        bool have = false;
        int id = 0;
        int level = 0;
        int vendorValue = 0;
        std::string name, icon, rarity, type, chatLink, description;
        std::vector<std::string> flags, restrictions;
        nlohmann::json details;
    };

    enum class InventoryState { Idle, Loading, Complete, Partial, Error };
    struct InventoryIndexStatus
    {
        InventoryState state = InventoryState::Idle;
        int totalSources = 0;
        int completedSources = 0;
        int pendingItemMeta = 0;
        double startedAt = 0;
        double lastRefreshAt = 0;
        std::vector<std::string> errors;
    };

    // WvW match snapshot (parsed from /v2/wvw/matches/{id}). Color index 0=red, 1=blue, 2=green throughout;
    // Model::wvwMyColor is 1/2/3 so wvwSides[wvwMyColor-1] is your team.
    struct WvwSide
    {
        long long score = 0; int victoryPoints = 0; long long ppt = 0; int skirmishScore = 0;
        long long kills = 0, deaths = 0;          // live cumulative
        long long kdKills = 0, kdDeaths = 0;      // display-throttled K/D snapshot (re-published ~120s)
        std::string worldName; int primaryWorld = 0;
    };
    struct WvwObjState { std::string objId; std::string type; int owner = 0; long long pointsTick = 0; double lastFlipped = 0; };
    // mapType: 0=EternalBattlegrounds(Center), 1=RedBorderlands, 2=BlueBorderlands, 3=GreenBorderlands,
    //          4=EdgeOfTheMists, -1=unknown. mapId is the GW2 map id (matches MumbleLink CurrentMapId).
    struct WvwMapState { int mapType = -1; int mapId = 0; int ownedByColor[3] = { 0, 0, 0 }; std::vector<WvwObjState> objectives; };

    struct Model
    {
        bool keyPresent = false;

        bool        haveAccount = false; std::string accountId, accountName; long age = 0; int fractalLevel = 0; int wvwRank = 0; double accountAt = 0;
        bool        haveWallet  = false; std::vector<WalletEntry> wallet; double walletAt = 0;
        bool        haveChars   = false; std::vector<AcctChar> characters; double charsAt = 0;
        bool        haveAch     = false; int achInProgress = 0, achDone = 0; double achAt = 0;
        bool        haveTotalAp = false; long long totalAp = 0, achPermAp = 0; int dailyAp = 0, monthlyAp = 0;   // total = achPermAp (perm tier sum) + daily_ap + monthly_ap
        bool        haveDailies = false; std::vector<std::string> dailyPve; double dailiesAt = 0;
        bool        haveMastery = false; int mpEarned = 0, mpSpent = 0, masteriesUnlocked = 0; double masteryAt = 0;
        bool        haveBank    = false; int bankUsed = 0, bankTotal = 0, bankItems = 0; double bankAt = 0; std::vector<Api::V2::ItemSlot> bankSlots; std::string bankError;
        bool        haveShared  = false; int sharedUsed = 0, sharedTotal = 0; double sharedAt = 0; std::vector<Api::V2::ItemSlot> sharedSlots; std::string sharedError;
        bool        haveMats    = false; int matTypes = 0; long long matTotal = 0; double matsAt = 0; std::vector<Api::V2::MaterialStack> materials; std::string matsError;
        bool        haveTp      = false; long long tpCoins = 0; int tpItems = 0, tpBuys = 0, tpSells = 0; double tpAt = 0;
        bool        haveWvDaily  = false; std::vector<WvObjective> wvDaily;  int wvDailyMetaCur = 0, wvDailyMetaMax = 0;  double wvDailyAt = 0;   // Wizard's Vault (replaced old dailies)
        bool        haveWvWeekly = false; std::vector<WvObjective> wvWeekly; int wvWeeklyMetaCur = 0, wvWeeklyMetaMax = 0; double wvWeeklyAt = 0;
        bool        wvUnavailable = false;   // the Wizard's Vault endpoints errored (account hasn't unlocked it -- needs a level-80 character)
        bool        haveRaids     = false; int raidsCleared = 0; std::vector<std::string> raidsClearedIds; double raidsAt = 0;   // /v2/account/raids: encounter ids cleared this week
        bool        haveDungeons  = false; std::set<std::string> dungeonPathsToday; double dungeonsAt = 0;             // /v2/account/dungeons: path ids completed today (daily reset)
        bool        haveLuck      = false; long long luck = 0; double luckAt = 0;                                      // /v2/account/luck: total consumed luck (raw; MF% later)
        bool        haveDailyDone = false; int worldBosses = 0, mapChests = 0, dailyCrafting = 0; double dailyDoneAt = 0; // today's reset-gated completions (counts)
        bool        haveLegendary = false; std::map<int, int> legendaryMax; std::map<int, int> legendaryOwned; double legendaryAt = 0;  // /v2/legendaryarmory (id->max) + /v2/account/legendaryarmory (id->owned)
        bool        haveSkins     = false; std::set<int> skinsUnlocked;       double skinsAt = 0;       // /v2/account/skins (wardrobe unlocked)
        bool        haveDyes      = false; std::set<int> dyesUnlocked;        double dyesAt = 0;        // /v2/account/dyes (unlocked colors)
        bool        haveHomestead = false; std::map<int, int> decorationsOwned; std::set<std::string> glyphsOwned, nodesOwned; std::set<int> catsOwned; double homesteadAt = 0;  // /v2/account/homestead/* + /v2/account/home/* (Items Homestead scope: owned decorations+counts, glyphs, cats, nodes)
        // Collections-tab visual cosmetics (P2): unlocked id sets per category (account-wide). Catalogs are the
        // bundled CosmeticCatalog seeds; these are the live unlock sets (/v2/account/<cat>; mounts = .../mounts/skins).
        bool        haveMounts    = false; std::set<int> mountsUnlocked;    double mountsAt = 0;     // /v2/account/mounts/skins (unlocked mount skins)
        bool        haveOutfits   = false; std::set<int> outfitsUnlocked;   double outfitsAt = 0;    // /v2/account/outfits
        bool        haveGliders   = false; std::set<int> glidersUnlocked;   double glidersAt = 0;    // /v2/account/gliders
        bool        haveJadeBots  = false; std::set<int> jadeBotsUnlocked;  double jadeBotsAt = 0;   // /v2/account/jadebots
        bool        haveSkiffs    = false; std::set<int> skiffsUnlocked;    double skiffsAt = 0;     // /v2/account/skiffs
        bool        haveNovelties = false; std::set<int> noveltiesUnlocked; double noveltiesAt = 0;  // /v2/account/novelties
        // Collections-tab icon collections (P3): same unlock-set model. finishers = OBJECTS [{id,...}]; mist
        // champions = pvp heroes (GUIDs -> FNV synthetic id, matched in the builder).
        bool        haveFinishers     = false; std::set<int> finishersUnlocked;     double finishersAt = 0;     // /v2/account/finishers
        bool        haveMailCarriers  = false; std::set<int> mailCarriersUnlocked;  double mailCarriersAt = 0;  // /v2/account/mailcarriers
        bool        haveMinis         = false; std::set<int> minisUnlocked;         double minisAt = 0;         // /v2/account/minis
        bool        haveMistChampions = false; std::set<int> mistChampionsUnlocked; double mistChampionsAt = 0; // /v2/account/pvp/heroes
        bool        haveTitles        = false; std::set<int> titlesUnlocked;        double titlesAt = 0;        // /v2/account/titles (P4 text collection)
        bool        haveEmotes        = false; std::set<int> emotesUnlocked;        double emotesAt = 0;        // /v2/account/emotes (P4; string ids -> FnvId int)
        bool        haveRecipes       = false; std::set<int> recipesUnlocked;       double recipesAt = 0;       // /v2/account/recipes (P4 Recipes scope: LEARNED crafting recipe ids)

        // WvW live match (anonymous; polled by TickWvw only when a WvW surface is shown or you're in WvW)
        bool        haveWvwMatch = false; double wvwMatchAt = 0, wvwKdShownAt = 0, wvwTeamAt = 0;
        std::string wvwMatchId; int wvwTeamId = 0; int wvwMyColor = 0;   // color 1=red,2=blue,3=green
        std::string wvwError;   // last WvW fetch error (surfaced in the loading text for diagnosis)
        WvwSide     wvwSides[3];
        int         wvwSkirmishIndex = 0; double wvwSkirmishEndsAt = 0;  // wvwSkirmishEndsAt = ImGui-time of the next skirmish flip
        std::vector<WvwMapState> wvwMaps;

        // per-character (alt widgets), keyed by character name
        std::map<std::string, CharInv>   inv;
        std::map<std::string, CharEquip> equip;
        std::map<int, ItemMeta> itemMeta;
        InventoryIndexStatus inventoryStatus;
    };

    // Refresh "domains" -- a consumer (the Dashboard, the HUD Info Panel) declares via an interest MASK which
    // data it currently shows, so we poll ONLY what something is displaying (an always-on bar showing just Gold
    // must not start polling bank/materials/characters). Char-switch detection runs regardless of the mask.
    enum Domain : unsigned
    {
        DomAccount      = 1u << 0,
        DomWallet       = 1u << 1,
        DomCharacters   = 1u << 2,
        DomAchievements = 1u << 3,
        DomDailies      = 1u << 4,
        DomMastery      = 1u << 5,
        DomBank         = 1u << 6,
        DomMaterials    = 1u << 7,
        DomTradingPost  = 1u << 8,
        DomWizardVault  = 1u << 9,
        DomShared       = 1u << 10,
        DomRaids        = 1u << 11,
        DomLuck         = 1u << 12,
        DomDailyDone    = 1u << 13,
        DomLegendaryArmory = 1u << 14,   // Collections Wardrobe "Legendary" sub-chip (owned vs total)
        DomSkins           = 1u << 15,   // Items-tab "Wardrobe" scope: unlocked skins
        DomDyes            = 1u << 16,   // Items-tab "Wardrobe" scope: unlocked dyes
        DomDungeons        = 1u << 17,   // Content-tab "Dungeons" scope: /v2/account/dungeons paths done today
        DomHomestead       = 1u << 18,   // Items-tab "Homestead" scope: owned decorations(+counts)/glyphs/cats/nodes
        DomMounts          = 1u << 19,   // Collections-tab "Mounts" scope: unlocked mount skins
        DomOutfits         = 1u << 20,   // Collections-tab "Outfits" scope: unlocked outfits
        DomGliders         = 1u << 21,   // Collections-tab "Gliders" scope: unlocked gliders
        DomJadeBots        = 1u << 22,   // Collections-tab "Jade Bots" scope: unlocked jade bot skins
        DomSkiffs          = 1u << 23,   // Collections-tab "Skiffs" scope: unlocked skiff skins
        DomNovelties       = 1u << 24,   // Collections-tab "Novelties" scope: unlocked novelties
        DomFinishers       = 1u << 25,   // Collections-tab "Finishers" scope: unlocked finishers
        DomMailCarriers    = 1u << 26,   // Collections-tab "Mail Carriers" scope: unlocked mail carriers
        DomMinis           = 1u << 27,   // Collections-tab "Minis" scope: unlocked miniatures
        DomMistChampions   = 1u << 28,   // Collections-tab "Mist Champions" scope: unlocked pvp heroes
        DomTitles          = 1u << 29,   // Collections-tab "Titles" scope: unlocked titles (/v2/account/titles)
        DomEmotes          = 1u << 30,   // Collections-tab "Emotes" scope: unlocked emotes (/v2/account/emotes, FNV'd)
        DomRecipes         = 1u << 31,   // Collections-tab "Recipes" scope: LEARNED crafting recipes (/v2/account/recipes)
        // DomAll = the dashboard's "show everything" mask; the scope-only domains above are intentionally
        // NOT included (nothing on the dashboard shows them -- they poll only via ItemsNeededDomains when active).
        DomAll = DomAccount | DomWallet | DomCharacters | DomAchievements | DomDailies | DomMastery | DomBank | DomMaterials | DomTradingPost | DomWizardVault | DomShared | DomRaids | DomLuck | DomDailyDone,
    };

    void Init(Api::Client* api, std::string cacheDir);   // store the client + cache path
    void Shutdown();                                     // free the account model on disable/unload (after api.Shutdown)
    void LoadCache();                                    // read dashboard-cache.json (instant last-known data)
    void WarmAll();                                      // login: refresh every domain incl. the inventory index (force)
    void Tick(unsigned interestMask);                    // per-frame: char-switch (always) + gentle TTL refresh for the requested domains
    void TickWvw(bool wantSurface, bool inWvw);          // WvW match poll: conditional (only if a WvW surface is shown OR you're in WvW) + tiered (fast in WvW, slow in PvE)
    const char* WvwLoadStage();                          // "" once loaded, else a stage hint ("Resolving WvW team..." / "Finding your match..." / "Loading WvW match...")

    const Model& Get();
    bool HasKey();
    bool HasScope(Api::TokenPermission p);
    bool Refreshing();                                   // any fetch in flight (for an "updating..." hint)

    void EnsureCharDetail(const std::string& name, bool force = false);   // fetch a character's inventory + equipment (force = refetch even if cached)
    void EnsureEquipAttributes(const std::string& name);                  // compute equipment-based attributes (memoized) once pieces + item metas are loaded
    void WarmInventoryIndex(bool force = false);          // fetch character bags + account storage for the inventory browser
    void RefreshInventorySource(const std::string& sourceKey);
    const InventoryIndexStatus& InventoryStatus();
    void EnsureItemMetadata(const std::vector<int>& ids);
    double LastRefresh();                                // newest domain timestamp (for the "as of" line)

    // -- per-character cache maintenance (rename detector + Diagnostics cleanup); inv/equip are caches that
    //    otherwise self-heal from the API, so these are best-effort housekeeping --
    void RenameChar(const std::string& from, const std::string& to);   // move inv/equip cache entries + save
    void PurgeChar(const std::string& name);                           // drop inv/equip cache entries + save
    void CollectCharNames(std::set<std::string>& out);                 // cached + live-roster character names
}
