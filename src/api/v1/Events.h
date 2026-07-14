#pragma once
#include "Common.h"

// GET /v1/events.json (anonymous; DEPRECATED but served): live event states ({"events":[{world_id, map_id,
// event_id, state}]}), optionally filtered by ?world_id / ?map_id / ?event_id. Wiki: API:1/events.
namespace Api::V1
{
    struct EventState { int worldId = 0; int mapId = 0; std::string eventId; std::string state; nlohmann::json raw; };

    inline std::vector<EventState> ParseEventStates(const nlohmann::json& j)
    {
        std::vector<EventState> out;
        for (const auto& e : Json::Node(j, "events"))
            if (e.is_object()) { EventState s; s.worldId = Json::Int(e, "world_id"); s.mapId = Json::Int(e, "map_id"); s.eventId = Json::Str(e, "event_id"); s.state = Json::Str(e, "state"); s.raw = e; out.push_back(std::move(s)); }
        return out;
    }

    class EventsEndpoint
    {
    public:
        explicit EventsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<EventState>>)> cb) const              { Fetch<std::vector<EventState>>(_c, "/v1/events.json", kV1LiveTtl, &ParseEventStates, std::move(cb)); }
        void ByWorld(int worldId, std::function<void(Result<std::vector<EventState>>)> cb) const { FetchQ<std::vector<EventState>>(_c, "/v1/events.json", "world_id", std::to_string(worldId), kV1LiveTtl, &ParseEventStates, std::move(cb)); }
        void ByMap(int mapId, std::function<void(Result<std::vector<EventState>>)> cb) const     { FetchQ<std::vector<EventState>>(_c, "/v1/events.json", "map_id", std::to_string(mapId), kV1LiveTtl, &ParseEventStates, std::move(cb)); }
    private:
        Connection* _c;
    };
}
