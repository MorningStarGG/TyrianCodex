#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"
#include "../core/Json.h"

// GET /v2/continents (anonymous, day-cached): the continent catalog (id -> name/dims/zoom + floor ids), plus
// the deep /v2/continents/{id}/floors/{floor} sub-resource (regions -> maps -> sectors/pois/...) that backs the
// map tiles. The floor tree is too nested to flatten, so Floor() returns raw JSON (the thumbnail code reads map
// rects straight from it). Wiki: API:2/continents.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Continent { int id = 0; std::string name; std::vector<int> continentDims; int minZoom = 0; int maxZoom = 0; std::vector<int> floors; nlohmann::json raw; };

    inline Continent ParseContinent(const nlohmann::json& j)
    {
        Continent c;
        c.id            = Json::Int(j, "id");
        c.name          = Json::Str(j, "name");
        c.continentDims = Json::IntArray(j, "continent_dims");
        c.minZoom       = Json::Int(j, "min_zoom");
        c.maxZoom       = Json::Int(j, "max_zoom");
        c.floors        = Json::IntArray(j, "floors");
        c.raw           = j;
        return c;
    }

    class ContinentsEndpoint : public BulkEndpoint<Continent>
    {
    public:
        explicit ContinentsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/continents", &ParseContinent, kStaticTtlSec) {}
        // The deep floor sub-resource: regions -> maps -> sectors/pois/tasks. Returned raw (documented exception
        // to the no-bare-raw rule: a hand-typed DTO of this tree adds no value the map-rect reader can't get
        // straight from the JSON, and it changes whenever maps are added).
        void Floor(int continentId, int floorId, std::function<void(Result<nlohmann::json>)> cb) const
        {
            Request req;
            req.path        = "/v2/continents/" + std::to_string(continentId) + "/floors/" + std::to_string(floorId);
            req.cacheTtlSec = kStaticTtlSec;
            _c->GetJson(std::move(req), std::move(cb));
        }
    };
}
