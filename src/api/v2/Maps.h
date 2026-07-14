#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include "../core/Result.h"

// GET /v2/maps/{id}  (anonymous; static). Names a map we have no bundled dataset for (an instance, a story
// map, a fractal...) so the guide panel can show "Cliffside Fractal" instead of "map 952". Anonymous, so it
// works with no API key, and the response is static, so it caches for a long time. `continentRect`/`mapRect`
// also feed the Zones-tab thumbnail crop. Wiki: API:2/maps.
namespace Api
{
    class Connection;
}
namespace Api::V2
{
    struct Map
    {
        int id = 0;
        std::string name;
        std::string type; // e.g. "Public", "Instance", "Tutorial", "Pvp"
        int minLevel = 0;
        int maxLevel = 0;
        int defaultFloor = 0;
        int continentId = 0;
        int regionId = 0;
        std::string regionName;
        nlohmann::json raw; // includes continent_rect / map_rect / floors for the thumbnail crop
    };

    Map ParseMap(const nlohmann::json &j);

    class MapsEndpoint
    {
    public:
        explicit MapsEndpoint(Connection *c) : _c(c) {}
        void Get(int id, std::function<void(Result<Map>)> cb) const;

    private:
        Connection *_c;
    };
}
