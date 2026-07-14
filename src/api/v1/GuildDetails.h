#pragma once
#include "Common.h"

// GET /v1/guild_details.json?guild_id=|guild_name= (anonymous): a guild's public card + emblem. Wiki: API:1/guild_details.
namespace Api::V1
{
    struct Emblem { int backgroundId = 0; int foregroundId = 0; std::vector<std::string> flags; int backgroundColorId = 0; int foregroundPrimaryColorId = 0; int foregroundSecondaryColorId = 0; nlohmann::json raw; };
    struct Guild  { std::string guildId; std::string guildName; std::string tag; Emblem emblem; nlohmann::json raw; };

    inline Guild ParseGuild(const nlohmann::json& j)
    {
        Guild g;
        g.guildId = Json::Str(j, "guild_id"); g.guildName = Json::Str(j, "guild_name"); g.tag = Json::Str(j, "tag");
        const nlohmann::json& e = Json::Node(j, "emblem");
        g.emblem.backgroundId = IntS(e, "background_id"); g.emblem.foregroundId = IntS(e, "foreground_id");
        g.emblem.flags = Json::StrArray(e, "flags"); g.emblem.backgroundColorId = IntS(e, "background_color_id");
        g.emblem.foregroundPrimaryColorId = IntS(e, "foreground_primary_color_id"); g.emblem.foregroundSecondaryColorId = IntS(e, "foreground_secondary_color_id");
        g.emblem.raw = e; g.raw = j;
        return g;
    }

    class GuildDetailsEndpoint
    {
    public:
        explicit GuildDetailsEndpoint(Connection* c) : _c(c) {}
        void ById(const std::string& guildId, std::function<void(Result<Guild>)> cb) const   { FetchQ<Guild>(_c, "/v1/guild_details.json", "guild_id",   guildId, 5 * 60, &ParseGuild, std::move(cb)); }
        void ByName(const std::string& name, std::function<void(Result<Guild>)> cb) const     { FetchQ<Guild>(_c, "/v1/guild_details.json", "guild_name", name,    5 * 60, &ParseGuild, std::move(cb)); }
    private:
        Connection* _c;
    };
}
