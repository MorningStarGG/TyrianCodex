#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// The /v2/mounts root + its sub-routes /v2/mounts/types and /v2/mounts/skins (all anonymous, day-cached). The
// `MountsEndpoint` facade hands out the two bulk sub-endpoints. The per-type `skills` array stays in `raw`.
// Wiki: API:2/mounts.
namespace Api { class Connection; }
namespace Api::V2
{
    struct MountType { std::string id; std::string name; int defaultSkin = 0; std::vector<int> skins; nlohmann::json raw; };
    struct MountSkin { int id = 0; std::string name; std::string icon; std::string mount; nlohmann::json raw; };

    inline MountType ParseMountType(const nlohmann::json& j)
    {
        MountType m;
        m.id          = Json::Str(j, "id");
        m.name        = Json::Str(j, "name");
        m.defaultSkin = Json::Int(j, "default_skin");
        m.skins       = Json::IntArray(j, "skins");
        m.raw         = j;   // the `skills` array lives in raw
        return m;
    }

    inline MountSkin ParseMountSkin(const nlohmann::json& j)
    {
        MountSkin m;
        m.id    = Json::Int(j, "id");
        m.name  = Json::Str(j, "name");
        m.icon  = Json::Str(j, "icon");
        m.mount = Json::Str(j, "mount");
        m.raw   = j;
        return m;
    }

    class MountTypesEndpoint : public BulkEndpoint<MountType, std::string>
    { public: explicit MountTypesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/mounts/types", &ParseMountType, kStaticTtlSec) {} };
    class MountSkinsEndpoint : public BulkEndpoint<MountSkin>
    { public: explicit MountSkinsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/mounts/skins", &ParseMountSkin, kStaticTtlSec) {} };

    // /v2/mounts is a sub-tree: .Types() (/v2/mounts/types) + .Skins() (/v2/mounts/skins).
    class MountsEndpoint
    {
    public:
        explicit MountsEndpoint(Connection* c) : _c(c) {}
        MountTypesEndpoint Types() const { return MountTypesEndpoint(_c); }
        MountSkinsEndpoint Skins() const { return MountSkinsEndpoint(_c); }
    private:
        Connection* _c;
    };
}
