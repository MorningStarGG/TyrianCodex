#include "Guild.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    Guild ParseGuild(const nlohmann::json& j)
    {
        Guild g;
        g.id    = Json::Str(j, "id");
        g.name  = Json::Str(j, "name");
        g.tag   = Json::Str(j, "tag");
        g.level = Json::Int(j, "level");
        g.motd  = Json::Str(j, "motd");
        g.raw   = j;   // emblem + member_count / influence / aetherium (leader-only) live in raw
        return g;
    }

    GuildPermission ParseGuildPermission(const nlohmann::json& j)
    {
        GuildPermission p;
        p.id          = Json::Str(j, "id");
        p.name        = Json::Str(j, "name");
        p.description = Json::Str(j, "description");
        p.raw         = j;
        return p;
    }

    GuildUpgrade ParseGuildUpgrade(const nlohmann::json& j)
    {
        GuildUpgrade u;
        u.id            = Json::Int(j, "id");
        u.name          = Json::Str(j, "name");
        u.type          = Json::Str(j, "type");
        u.icon          = Json::Str(j, "icon");
        u.buildTime     = Json::Int(j, "build_time");
        u.requiredLevel = Json::Int(j, "required_level");
        u.raw           = j;   // costs / prerequisites / bag bonuses live in raw
        return u;
    }

    void GuildEndpoint::Get(const std::string& id, std::function<void(Result<Guild>)> cb) const
    {
        Request req;
        req.path        = "/v2/guild/" + id;
        req.cacheTtlSec = 5 * 60;   // public guild card; changes rarely
        _c->Get<Guild>(std::move(req), &ParseGuild, std::move(cb));
    }

    void GuildEndpoint::Search(const std::string& name, std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req;
        req.path        = "/v2/guild/search";
        req.query       = { { "name", name } };
        req.cacheTtlSec = 5 * 60;
        _c->Get<std::vector<std::string>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<std::string> out;
                if (j.is_array()) for (const auto& e : j) if (e.is_string()) out.push_back(e.get<std::string>());
                return out;
            },
            std::move(cb));
    }

    // ---- leader-only sub-resource parsers --------------------------------------------------------------------

    static std::vector<GuildMember> ParseGuildMembers(const nlohmann::json& j)
    {
        std::vector<GuildMember> out;
        if (j.is_array()) for (const auto& e : j) { GuildMember m; m.name = Json::Str(e, "name"); m.rank = Json::Str(e, "rank"); m.joined = Json::Str(e, "joined"); m.raw = e; out.push_back(std::move(m)); }
        return out;
    }
    static std::vector<GuildRank> ParseGuildRanks(const nlohmann::json& j)
    {
        std::vector<GuildRank> out;
        if (j.is_array()) for (const auto& e : j) { GuildRank r; r.id = Json::Str(e, "id"); r.order = Json::Int(e, "order"); r.icon = Json::Str(e, "icon"); r.permissions = Json::StrArray(e, "permissions"); r.raw = e; out.push_back(std::move(r)); }
        return out;
    }
    static std::vector<GuildLogEntry> ParseGuildLog(const nlohmann::json& j)
    {
        std::vector<GuildLogEntry> out;
        if (j.is_array()) for (const auto& e : j) { GuildLogEntry l; l.id = Json::Int(e, "id"); l.time = Json::Str(e, "time"); l.type = Json::Str(e, "type"); l.user = Json::Str(e, "user"); l.raw = e; out.push_back(std::move(l)); }
        return out;
    }
    static std::vector<GuildStashTab> ParseGuildStash(const nlohmann::json& j)
    {
        std::vector<GuildStashTab> out;
        if (j.is_array()) for (const auto& e : j)
        {
            GuildStashTab t; t.upgradeId = Json::Int(e, "upgrade_id"); t.size = Json::Int(e, "size"); t.coins = Json::Int64(e, "coins"); t.note = Json::Str(e, "note");
            for (const auto& s : Json::Node(e, "inventory")) t.inventory.push_back(ParseItemSlot(s));
            t.raw = e; out.push_back(std::move(t));
        }
        return out;
    }
    static std::vector<GuildTreasuryRow> ParseGuildTreasury(const nlohmann::json& j)
    {
        std::vector<GuildTreasuryRow> out;
        if (j.is_array()) for (const auto& e : j) { GuildTreasuryRow r; r.itemId = Json::Int(e, "item_id"); r.count = Json::Int(e, "count"); r.raw = e; out.push_back(std::move(r)); }
        return out;
    }
    static std::vector<GuildTeam> ParseGuildTeams(const nlohmann::json& j)
    {
        std::vector<GuildTeam> out;
        if (j.is_array()) for (const auto& e : j) { GuildTeam t; t.id = Json::Int(e, "id"); t.name = Json::Str(e, "name"); t.raw = e; out.push_back(std::move(t)); }
        return out;
    }
    static std::vector<int> ParseGuildUpgradeIds(const nlohmann::json& j)
    {
        std::vector<int> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_number_integer()) out.push_back(e.get<int>());
        return out;
    }
    static std::vector<CountedId> ParseGuildStorage(const nlohmann::json& j)
    {
        std::vector<CountedId> out;
        if (j.is_array()) for (const auto& e : j) out.push_back(ParseCountedId(e));
        return out;
    }

    // ---- request plumbing ------------------------------------------------------------------------------------

    Request GuildEndpoint::MakeReq(const std::string& id, const char* sub) const
    {
        Request req;
        req.path = "/v2/guild/" + id + "/" + sub;
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Guilds;
        req.cacheTtlSec = 60;
        return req;
    }
    template <typename T>
    void GuildEndpoint::GetSub(const std::string& id, const char* sub, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const
    {
        _c->Get<T>(MakeReq(id, sub), parse, std::move(cb));
    }

    void GuildEndpoint::Members(const std::string& id,  std::function<void(Result<std::vector<GuildMember>>)> cb) const     { GetSub(id, "members",  &ParseGuildMembers,    std::move(cb)); }
    void GuildEndpoint::Ranks(const std::string& id,    std::function<void(Result<std::vector<GuildRank>>)> cb) const       { GetSub(id, "ranks",    &ParseGuildRanks,      std::move(cb)); }
    void GuildEndpoint::Log(const std::string& id,      std::function<void(Result<std::vector<GuildLogEntry>>)> cb) const   { GetSub(id, "log",      &ParseGuildLog,        std::move(cb)); }
    void GuildEndpoint::Stash(const std::string& id,    std::function<void(Result<std::vector<GuildStashTab>>)> cb) const   { GetSub(id, "stash",    &ParseGuildStash,      std::move(cb)); }
    void GuildEndpoint::Treasury(const std::string& id, std::function<void(Result<std::vector<GuildTreasuryRow>>)> cb) const{ GetSub(id, "treasury", &ParseGuildTreasury,   std::move(cb)); }
    void GuildEndpoint::Teams(const std::string& id,    std::function<void(Result<std::vector<GuildTeam>>)> cb) const       { GetSub(id, "teams",    &ParseGuildTeams,      std::move(cb)); }
    void GuildEndpoint::Upgrades(const std::string& id, std::function<void(Result<std::vector<int>>)> cb) const             { GetSub(id, "upgrades", &ParseGuildUpgradeIds, std::move(cb)); }
    void GuildEndpoint::Storage(const std::string& id,  std::function<void(Result<std::vector<CountedId>>)> cb) const       { GetSub(id, "storage",  &ParseGuildStorage,    std::move(cb)); }
}
