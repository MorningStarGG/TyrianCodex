#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Result.h"
#include "Common.h"          // ItemSlot (guild stash), CountedId (guild storage)

// Guild endpoints. /v2/guild/:id returns a guild's PUBLIC fields anonymously (name, tag, emblem); the per-guild
// sub-resources (members, ranks, log, stash, treasury, teams, upgrades, storage) need the guild LEADER's key
// (guilds scope) and are deeply nested, so they're returned as raw JSON. The catalogs /v2/guild/permissions +
// /v2/guild/upgrades are anonymous bulk; /v2/guild/search?name= resolves a name to guild ids. (The guild-emblem
// art /v2/emblem lives in Emblem.h, its own root.) Wiki: API:2/guild.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Guild           { std::string id; std::string name; std::string tag; int level = 0; std::string motd; nlohmann::json raw; };
    struct GuildPermission { std::string id; std::string name; std::string description; nlohmann::json raw; };
    struct GuildUpgrade    { int id = 0; std::string name; std::string type; std::string icon; int buildTime = 0; int requiredLevel = 0; nlohmann::json raw; };

    // Leader-only sub-resources (scope: guilds). Headline fields are typed; the type-varying / nested tails
    // (log entry details, treasury `needed_by`, team ladders) stay in each row's `raw`.
    struct GuildMember     { std::string name; std::string rank; std::string joined; nlohmann::json raw; };
    struct GuildRank       { std::string id; int order = 0; std::string icon; std::vector<std::string> permissions; nlohmann::json raw; };
    struct GuildLogEntry   { int id = 0; std::string time; std::string type; std::string user; nlohmann::json raw; };
    struct GuildStashTab   { int upgradeId = 0; int size = 0; int64_t coins = 0; std::string note; std::vector<ItemSlot> inventory; nlohmann::json raw; };
    struct GuildTreasuryRow{ int itemId = 0; int count = 0; nlohmann::json raw; };
    struct GuildTeam       { int id = 0; std::string name; nlohmann::json raw; };

    Guild           ParseGuild(const nlohmann::json& j);
    GuildPermission ParseGuildPermission(const nlohmann::json& j);
    GuildUpgrade    ParseGuildUpgrade(const nlohmann::json& j);

    class GuildPermissionsEndpoint : public BulkEndpoint<GuildPermission, std::string> { public: explicit GuildPermissionsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/guild/permissions", &ParseGuildPermission, kStaticTtlSec) {} };
    class GuildUpgradesEndpoint    : public BulkEndpoint<GuildUpgrade>                  { public: explicit GuildUpgradesEndpoint(Connection* c)    : BulkEndpoint(c, "/v2/guild/upgrades",    &ParseGuildUpgrade,    kStaticTtlSec) {} };

    // The guild facade. Get(id) is the public guild card; the sub-resources need the leader's key (guilds).
    class GuildEndpoint
    {
    public:
        explicit GuildEndpoint(Connection* c) : _c(c) {}

        void Get(const std::string& id, std::function<void(Result<Guild>)> cb) const;                 // /v2/guild/:id (public fields)
        void Search(const std::string& name, std::function<void(Result<std::vector<std::string>>)> cb) const;  // /v2/guild/search?name=

        // Leader-only sub-resources (auth: guilds), typed (deep / type-varying tails kept in each row's raw):
        void Members(const std::string& id,  std::function<void(Result<std::vector<GuildMember>>)> cb) const;
        void Ranks(const std::string& id,    std::function<void(Result<std::vector<GuildRank>>)> cb) const;
        void Log(const std::string& id,      std::function<void(Result<std::vector<GuildLogEntry>>)> cb) const;
        void Stash(const std::string& id,    std::function<void(Result<std::vector<GuildStashTab>>)> cb) const;
        void Treasury(const std::string& id, std::function<void(Result<std::vector<GuildTreasuryRow>>)> cb) const;
        void Teams(const std::string& id,    std::function<void(Result<std::vector<GuildTeam>>)> cb) const;
        void Upgrades(const std::string& id, std::function<void(Result<std::vector<int>>)> cb) const;   // the guild's UNLOCKED upgrade ids
        void Storage(const std::string& id,  std::function<void(Result<std::vector<CountedId>>)> cb) const;

        GuildPermissionsEndpoint Permissions()     const { return GuildPermissionsEndpoint(_c); }     // /v2/guild/permissions (catalog)
        GuildUpgradesEndpoint    UpgradeCatalog()  const { return GuildUpgradesEndpoint(_c); }        // /v2/guild/upgrades (catalog)

    private:
        Request MakeReq(const std::string& id, const char* sub) const;
        template <typename T>
        void GetSub(const std::string& id, const char* sub, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const;
        Connection* _c;
    };
}
