#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/skiffs (anonymous, day-cached): the skiff-skin catalog (id -> name/icon + dye slots). Wiki: API:2/skiffs.
namespace Api { class Connection; }
namespace Api::V2
{
    struct SkiffDyeSlot { int colorId = 0; std::string material; };
    struct Skiff        { int id = 0; std::string name; std::string icon; std::vector<SkiffDyeSlot> dyeSlots; nlohmann::json raw; };

    inline Skiff ParseSkiff(const nlohmann::json& j)
    {
        Skiff s;
        s.id = Json::Int(j, "id"); s.name = Json::Str(j, "name"); s.icon = Json::Str(j, "icon");
        for (const auto& d : Json::Node(j, "dye_slots"))
            if (d.is_object()) s.dyeSlots.push_back({ Json::Int(d, "color_id"), Json::Str(d, "material") });
        s.raw = j;
        return s;
    }

    class SkiffsEndpoint : public BulkEndpoint<Skiff>
    { public: explicit SkiffsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/skiffs", &ParseSkiff, kStaticTtlSec) {} };
}
