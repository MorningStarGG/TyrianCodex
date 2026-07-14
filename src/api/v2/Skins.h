#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/skins (anonymous, day-cached): the wardrobe skin catalog (id -> name/type/rarity/icon + flags). The
// `details` (appearance) + `restrictions` stay in `raw`. Wiki: API:2/skins.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Skin { int id = 0; std::string name; std::string type; std::string rarity; std::string icon; std::vector<std::string> flags; nlohmann::json raw; };

    inline Skin ParseSkin(const nlohmann::json& j)
    {
        Skin s;
        s.id     = Json::Int(j, "id");
        s.name   = Json::Str(j, "name");
        s.type   = Json::Str(j, "type");
        s.rarity = Json::Str(j, "rarity");
        s.icon   = Json::Str(j, "icon");
        s.flags  = Json::StrArray(j, "flags");
        s.raw    = j;   // `details` (appearance) + `restrictions` live in raw
        return s;
    }

    class SkinsEndpoint : public BulkEndpoint<Skin>
    { public: explicit SkinsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/skins", &ParseSkin, kStaticTtlSec) {} };
}
