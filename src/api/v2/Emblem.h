#pragma once
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/emblem/{backgrounds,foregrounds} (anonymous, day-cached): the guild-emblem art layers. The
// `EmblemEndpoint` facade hands out the two bulk sub-endpoints. The `layers` (image-url arrays) stay in `raw`.
// (Separate /emblem root, extracted from the guild module.) Wiki: API:2/emblem.
namespace Api { class Connection; }
namespace Api::V2
{
    struct EmblemPart { int id = 0; nlohmann::json raw; };   // `layers` (image url arrays) in raw

    inline EmblemPart ParseEmblemPart(const nlohmann::json& j)
    {
        EmblemPart e;
        e.id  = Json::Int(j, "id");
        e.raw = j;
        return e;
    }

    class EmblemEndpoint
    {
    public:
        explicit EmblemEndpoint(Connection* c) : _c(c) {}
        BulkEndpoint<EmblemPart> Backgrounds() const { return BulkEndpoint<EmblemPart>(_c, "/v2/emblem/backgrounds", &ParseEmblemPart, kStaticTtlSec); }
        BulkEndpoint<EmblemPart> Foregrounds() const { return BulkEndpoint<EmblemPart>(_c, "/v2/emblem/foregrounds", &ParseEmblemPart, kStaticTtlSec); }
    private:
        Connection* _c;
    };
}
