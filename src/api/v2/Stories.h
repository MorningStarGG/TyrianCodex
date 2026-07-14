#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Result.h"

// GET /v2/stories and /v2/stories/seasons  (anonymous; static). These ARE the game's Story Journal data:
// seasons (My Story, LWS1, LWS2, HoT, ...) each with an ordered `stories` id list, and per-story the name,
// official description, in-game `timeline` year ("1327 AE"), required `level`, and order. The Journal tab
// builds its collapsible tree from Seasons + the detail panel text from Stories. Wiki: API:2/stories.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Story
    {
        int            id    = 0;
        std::string    season;       // season GUID (resolve against Seasons)
        std::string    name;         // "2. Entanglement"
        std::string    description;  // official journal blurb
        std::string    timeline;     // "1327 AE"
        int            level = 0;    // playable-at level
        int            order = 0;
        nlohmann::json raw;
    };

    struct StorySeason
    {
        std::string      id;         // GUID
        std::string      name;       // "Living World Season 2"
        int              order = 0;
        std::vector<int> stories;    // ordered story ids
        nlohmann::json   raw;
    };

    Story       ParseStory(const nlohmann::json& j);
    StorySeason ParseStorySeason(const nlohmann::json& j);

    // /v2/stories/seasons
    class StorySeasonsEndpoint
    {
    public:
        explicit StorySeasonsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<StorySeason>>)> cb) const;
    private:
        Connection* _c;
    };

    // /v2/stories (+ .Seasons() for /v2/stories/seasons)
    class StoriesEndpoint
    {
    public:
        explicit StoriesEndpoint(Connection* c) : _c(c) {}
        void Get(int id, std::function<void(Result<Story>)> cb) const;
        void Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Story>>)> cb) const;
        void GetAll(std::function<void(Result<std::vector<Story>>)> cb) const;
        StorySeasonsEndpoint Seasons() const { return StorySeasonsEndpoint(_c); }
    private:
        Connection* _c;
    };
}
