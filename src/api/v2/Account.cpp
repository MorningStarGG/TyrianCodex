#include "Account.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    // ---- parsers (json -> typed model) ----------------------------------------------------------------------

    static std::vector<int> ParseIntList(const nlohmann::json& j)
    {
        std::vector<int> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_number_integer()) out.push_back(e.get<int>());
        return out;
    }
    static std::vector<std::string> ParseStrList(const nlohmann::json& j)
    {
        std::vector<std::string> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_string()) out.push_back(e.get<std::string>());
        return out;
    }

    static AccountInfo ParseAccountInfo(const nlohmann::json& j)
    {
        AccountInfo a;
        a.id      = Json::Str(j, "id");
        a.name    = Json::Str(j, "name");
        a.world   = Json::Int(j, "world");
        a.created = Json::Str(j, "created");
        a.guilds  = Json::StrArray(j, "guilds");
        a.raw     = j;
        return a;
    }

    static std::vector<AccountAchievement> ParseAchievements(const nlohmann::json& j)
    {
        std::vector<AccountAchievement> out;
        if (j.is_array()) for (const auto& e : j)
        {
            AccountAchievement a;
            a.id = Json::Int(e, "id"); a.done = Json::Bool(e, "done"); a.current = Json::Int(e, "current");
            a.max = Json::Int(e, "max"); a.repeated = Json::Int(e, "repeated"); a.raw = e;
            out.push_back(std::move(a));
        }
        return out;
    }

    static std::vector<WalletEntry> ParseWallet(const nlohmann::json& j)
    {
        std::vector<WalletEntry> out;
        if (j.is_array()) for (const auto& e : j) { WalletEntry w; w.id = Json::Int(e, "id"); w.value = Json::Int64(e, "value"); w.raw = e; out.push_back(std::move(w)); }
        return out;
    }

    static std::vector<ItemSlot> ParseItemSlots(const nlohmann::json& j)
    {
        std::vector<ItemSlot> out;
        if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseItemSlot(e)); }
        return out;
    }

    static std::vector<MaterialStack> ParseMaterials(const nlohmann::json& j)
    {
        std::vector<MaterialStack> out;
        if (j.is_array()) for (const auto& e : j)
        {
            MaterialStack m;
            m.id = Json::Int(e, "id"); m.category = Json::Int(e, "category"); m.count = Json::Int(e, "count");
            m.binding = Json::Str(e, "binding"); m.raw = e;
            out.push_back(std::move(m));
        }
        return out;
    }

    static std::vector<BuildTemplate> ParseBuildStorage(const nlohmann::json& j)
    {
        std::vector<BuildTemplate> out;
        if (j.is_array()) for (const auto& e : j) { BuildTemplate b; b.name = Json::Str(e, "name"); b.profession = Json::Str(e, "profession"); b.raw = e; out.push_back(std::move(b)); }
        return out;
    }

    static std::vector<AccountMastery> ParseMasteries(const nlohmann::json& j)
    {
        std::vector<AccountMastery> out;
        if (j.is_array()) for (const auto& e : j) { AccountMastery m; m.id = Json::Int(e, "id"); m.level = Json::Int(e, "level"); m.raw = e; out.push_back(std::move(m)); }
        return out;
    }

    static AccountMasteryPoints ParseMasteryPoints(const nlohmann::json& j)
    {
        AccountMasteryPoints p;
        for (const auto& t : Json::Node(j, "totals"))
            if (t.is_object()) { MasteryPointTotal mt; mt.region = Json::Str(t, "region"); mt.spent = Json::Int(t, "spent"); mt.earned = Json::Int(t, "earned"); mt.raw = t; p.totals.push_back(std::move(mt)); }
        p.unlocked = Json::IntArray(j, "unlocked");
        p.raw = j;
        return p;
    }

    static std::vector<ProgressionRow> ParseProgression(const nlohmann::json& j)
    {
        std::vector<ProgressionRow> out;
        if (j.is_array()) for (const auto& e : j) { ProgressionRow r; r.id = Json::Str(e, "id"); r.value = Json::Int64(e, "value"); r.raw = e; out.push_back(std::move(r)); }
        return out;
    }

    static int64_t ParseLuck(const nlohmann::json& j)
    {
        // /v2/account/luck -> [{ "id": "luck", "value": N }] (or empty); return the luck total.
        if (j.is_array()) for (const auto& e : j) if (Json::Str(e, "id") == "luck") return Json::Int64(e, "value");
        return 0;
    }

    static std::vector<AccountFinisher> ParseFinishers(const nlohmann::json& j)
    {
        std::vector<AccountFinisher> out;
        if (j.is_array()) for (const auto& e : j) { AccountFinisher f; f.id = Json::Int(e, "id"); f.permanent = Json::Bool(e, "permanent"); f.quantity = Json::Int(e, "quantity"); f.raw = e; out.push_back(std::move(f)); }
        return out;
    }

    static std::vector<AccountHomeCat> ParseHomeCats(const nlohmann::json& j)
    {
        std::vector<AccountHomeCat> out;
        if (j.is_array()) for (const auto& e : j)
        {
            AccountHomeCat c;
            if (e.is_object()) { c.id = Json::Int(e, "id"); c.hint = Json::Str(e, "hint"); }
            else if (e.is_number_integer()) c.id = e.get<int>();   // older API returned bare ids
            c.raw = e;
            out.push_back(std::move(c));
        }
        return out;
    }

    static std::vector<CountedId> ParseCountedIds(const nlohmann::json& j)
    {
        std::vector<CountedId> out;
        if (j.is_array()) for (const auto& e : j) { CountedId c; c.id = Json::Int(e, "id"); c.count = Json::Int(e, "count"); c.raw = e; out.push_back(std::move(c)); }
        return out;
    }

    static WizardsVaultProgress ParseWizardsVault(const nlohmann::json& j)
    {
        WizardsVaultProgress w;
        w.metaProgressCurrent  = Json::Int(j, "meta_progress_current");
        w.metaProgressComplete = Json::Int(j, "meta_progress_complete");
        w.metaRewardItemId     = Json::Int(j, "meta_reward_item_id");
        w.metaRewardClaimed    = Json::Bool(j, "meta_reward_claimed");
        for (const auto& o : Json::Node(j, "objectives"))
            if (o.is_object())
            {
                WizardsVaultObjective ob;
                ob.id = Json::Int(o, "id"); ob.title = Json::Str(o, "title"); ob.track = Json::Str(o, "track");
                ob.value = Json::Int(o, "value"); ob.target = Json::Int(o, "target");
                ob.rewardItemId = Json::Int(o, "reward_item_id"); ob.rewardCount = Json::Int(o, "reward_count");
                ob.claimed = Json::Bool(o, "claimed"); ob.progressComplete = Json::Bool(o, "progress_complete"); ob.raw = o;
                w.objectives.push_back(std::move(ob));
            }
        w.raw = j;
        return w;
    }

    static AccountWvw ParseAccountWvw(const nlohmann::json& j) { AccountWvw w; w.teamId = Json::Int(j, "team_id"); w.raw = j; return w; }
    static std::vector<AccountWizardsVaultListing> ParseAccountWizardsVaultListings(const nlohmann::json& j)
    {
        std::vector<AccountWizardsVaultListing> out;
        if (j.is_array()) for (const auto& e : j) { AccountWizardsVaultListing l; l.id = Json::Int(e, "id"); l.purchased = Json::Int(e, "purchased"); l.raw = e; out.push_back(std::move(l)); }
        return out;
    }

    // ---- request plumbing -----------------------------------------------------------------------------------

    Request AccountEndpoint::MakeReq(const char* sub, TokenPermission scope) const
    {
        Request req;
        req.path = std::string("/v2/account/") + sub;
        req.auth = true; req.hasScope = true; req.scope = scope;
        req.cacheTtlSec = 60;
        return req;
    }

    template <typename T>
    void AccountEndpoint::GetSub(const char* sub, TokenPermission scope, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const
    {
        _c->Get<T>(MakeReq(sub, scope), parse, std::move(cb));
    }

    void AccountEndpoint::Get(std::function<void(Result<AccountInfo>)> cb) const
    {
        Request req;
        req.path = "/v2/account";
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Account;
        req.cacheTtlSec = 60;
        _c->Get<AccountInfo>(std::move(req), &ParseAccountInfo, std::move(cb));
    }

    // account + progression (the story consumer checks both); scope set to Progression (superset gate).
    void AccountEndpoint::Achievements(std::function<void(Result<std::vector<AccountAchievement>>)> cb) const { GetSub("achievements", TokenPermission::Progression, &ParseAchievements, std::move(cb)); }
    void AccountEndpoint::Wallet(std::function<void(Result<std::vector<WalletEntry>>)> cb) const               { GetSub("wallet", TokenPermission::Wallet, &ParseWallet, std::move(cb)); }

    // Inventory / storage.
    void AccountEndpoint::Bank(std::function<void(Result<std::vector<ItemSlot>>)> cb) const            { GetSub("bank",       TokenPermission::Inventories, &ParseItemSlots,   std::move(cb)); }
    void AccountEndpoint::Materials(std::function<void(Result<std::vector<MaterialStack>>)> cb) const  { GetSub("materials",  TokenPermission::Inventories, &ParseMaterials,    std::move(cb)); }
    void AccountEndpoint::SharedInventory(std::function<void(Result<std::vector<ItemSlot>>)> cb) const { GetSub("inventory",  TokenPermission::Inventories, &ParseItemSlots,    std::move(cb)); }
    void AccountEndpoint::BuildStorage(std::function<void(Result<std::vector<BuildTemplate>>)> cb) const { GetSub("buildstorage", TokenPermission::Builds,   &ParseBuildStorage, std::move(cb)); }

    // Progression.
    void AccountEndpoint::Masteries(std::function<void(Result<std::vector<AccountMastery>>)> cb) const  { GetSub("masteries",      TokenPermission::Progression, &ParseMasteries,     std::move(cb)); }
    void AccountEndpoint::MasteryPoints(std::function<void(Result<AccountMasteryPoints>)> cb) const     { GetSub("mastery/points", TokenPermission::Progression, &ParseMasteryPoints, std::move(cb)); }
    void AccountEndpoint::Progression(std::function<void(Result<std::vector<ProgressionRow>>)> cb) const { GetSub("progression",    TokenPermission::Progression, &ParseProgression,   std::move(cb)); }
    void AccountEndpoint::Luck(std::function<void(Result<int64_t>)> cb) const                           { GetSub("luck",           TokenPermission::Progression, &ParseLuck,          std::move(cb)); }
    void AccountEndpoint::DailyCrafting(std::function<void(Result<std::vector<std::string>>)> cb) const { GetSub("dailycrafting",  TokenPermission::Progression, &ParseStrList,       std::move(cb)); }
    void AccountEndpoint::Raids(std::function<void(Result<std::vector<std::string>>)> cb) const         { GetSub("raids",          TokenPermission::Progression, &ParseStrList,       std::move(cb)); }
    void AccountEndpoint::WorldBosses(std::function<void(Result<std::vector<std::string>>)> cb) const   { GetSub("worldbosses",    TokenPermission::Progression, &ParseStrList,       std::move(cb)); }
    void AccountEndpoint::MapChests(std::function<void(Result<std::vector<std::string>>)> cb) const     { GetSub("mapchests",      TokenPermission::Progression, &ParseStrList,       std::move(cb)); }
    void AccountEndpoint::Dungeons(std::function<void(Result<std::vector<std::string>>)> cb) const      { GetSub("dungeons",       TokenPermission::Progression, &ParseStrList,       std::move(cb)); }
    void AccountEndpoint::HomeNodes(std::function<void(Result<std::vector<std::string>>)> cb) const     { GetSub("home/nodes",     TokenPermission::Progression, &ParseStrList,       std::move(cb)); }

    // Unlocks.
    void AccountEndpoint::Dyes(std::function<void(Result<std::vector<int>>)> cb) const            { GetSub("dyes",         TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Skins(std::function<void(Result<std::vector<int>>)> cb) const           { GetSub("skins",        TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Minis(std::function<void(Result<std::vector<int>>)> cb) const           { GetSub("minis",        TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Outfits(std::function<void(Result<std::vector<int>>)> cb) const         { GetSub("outfits",      TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Gliders(std::function<void(Result<std::vector<int>>)> cb) const         { GetSub("gliders",      TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Finishers(std::function<void(Result<std::vector<AccountFinisher>>)> cb) const { GetSub("finishers", TokenPermission::Unlocks, &ParseFinishers, std::move(cb)); }
    void AccountEndpoint::MailCarriers(std::function<void(Result<std::vector<int>>)> cb) const    { GetSub("mailcarriers", TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Novelties(std::function<void(Result<std::vector<int>>)> cb) const       { GetSub("novelties",    TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Titles(std::function<void(Result<std::vector<int>>)> cb) const          { GetSub("titles",       TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Emotes(std::function<void(Result<std::vector<std::string>>)> cb) const  { GetSub("emotes",       TokenPermission::Progression, &ParseStrList, std::move(cb)); }
    void AccountEndpoint::Recipes(std::function<void(Result<std::vector<int>>)> cb) const         { GetSub("recipes",      TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::MountSkins(std::function<void(Result<std::vector<int>>)> cb) const      { GetSub("mounts/skins", TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::MountTypes(std::function<void(Result<std::vector<std::string>>)> cb) const { GetSub("mounts/types", TokenPermission::Unlocks, &ParseStrList, std::move(cb)); }
    void AccountEndpoint::HomeCats(std::function<void(Result<std::vector<AccountHomeCat>>)> cb) const { GetSub("home/cats", TokenPermission::Unlocks, &ParseHomeCats, std::move(cb)); }
    void AccountEndpoint::HomesteadDecorations(std::function<void(Result<std::vector<CountedId>>)> cb) const { GetSub("homestead/decorations", TokenPermission::Unlocks, &ParseCountedIds, std::move(cb)); }
    void AccountEndpoint::HomesteadGlyphs(std::function<void(Result<std::vector<std::string>>)> cb) const    { GetSub("homestead/glyphs", TokenPermission::Unlocks, &ParseStrList, std::move(cb)); }
    void AccountEndpoint::JadeBots(std::function<void(Result<std::vector<int>>)> cb) const        { GetSub("jadebots",     TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::Skiffs(std::function<void(Result<std::vector<int>>)> cb) const          { GetSub("skiffs",       TokenPermission::Unlocks, &ParseIntList, std::move(cb)); }
    void AccountEndpoint::LegendaryArmory(std::function<void(Result<std::vector<CountedId>>)> cb) const { GetSub("legendaryarmory", TokenPermission::Unlocks, &ParseCountedIds, std::move(cb)); }

    // Wizard's Vault.
    void AccountEndpoint::WizardsVaultDaily(std::function<void(Result<WizardsVaultProgress>)> cb) const   { GetSub("wizardsvault/daily",   TokenPermission::Account, &ParseWizardsVault, std::move(cb)); }
    void AccountEndpoint::WizardsVaultWeekly(std::function<void(Result<WizardsVaultProgress>)> cb) const  { GetSub("wizardsvault/weekly",  TokenPermission::Account, &ParseWizardsVault, std::move(cb)); }
    void AccountEndpoint::WizardsVaultSpecial(std::function<void(Result<WizardsVaultProgress>)> cb) const { GetSub("wizardsvault/special", TokenPermission::Account, &ParseWizardsVault, std::move(cb)); }
    void AccountEndpoint::WizardsVaultListings(std::function<void(Result<std::vector<AccountWizardsVaultListing>>)> cb) const { GetSub("wizardsvault/listings", TokenPermission::Account, &ParseAccountWizardsVaultListings, std::move(cb)); }

    // Competitive.
    void AccountEndpoint::PvpHeroes(std::function<void(Result<std::vector<std::string>>)> cb) const { GetSub("pvp/heroes", TokenPermission::Account, &ParseStrList,    std::move(cb)); }
    void AccountEndpoint::Wvw(std::function<void(Result<AccountWvw>)> cb) const                      { GetSub("wvw",        TokenPermission::Account, &ParseAccountWvw, std::move(cb)); }
}
