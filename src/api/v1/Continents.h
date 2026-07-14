#pragma once
#include "Common.h"

// GET /v1/continents.json (anonymous): the continent catalog ({"continents":{id:{...}}}). Wiki: API:1/continents.
namespace Api::V1
{
    struct Continent { int id = 0; std::string name; std::vector<int> continentDims; int minZoom = 0; int maxZoom = 0; std::vector<int> floors; nlohmann::json raw; };

    inline std::vector<Continent> ParseContinents(const nlohmann::json& j)
    {
        std::vector<Continent> out;
        const nlohmann::json& cs = Json::Node(j, "continents");
        if (cs.is_object()) for (auto it = cs.begin(); it != cs.end(); ++it)
        {
            const nlohmann::json& c = it.value();
            Continent k;
            try { k.id = std::stoi(it.key()); } catch (...) {}
            k.name = Json::Str(c, "name"); k.continentDims = Json::IntArray(c, "continent_dims");
            k.minZoom = IntS(c, "min_zoom"); k.maxZoom = IntS(c, "max_zoom"); k.floors = Json::IntArray(c, "floors"); k.raw = c;
            out.push_back(std::move(k));
        }
        return out;
    }

    class ContinentsEndpoint
    {
    public:
        explicit ContinentsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<Continent>>)> cb) const { Fetch<std::vector<Continent>>(_c, "/v1/continents.json", kV1StaticTtl, &ParseContinents, std::move(cb)); }
    private:
        Connection* _c;
    };
}
