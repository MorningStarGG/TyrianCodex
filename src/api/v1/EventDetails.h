#pragma once
#include "Common.h"

// GET /v1/event_details.json (anonymous, locale): the dynamic-event catalog ({"events":{guid:{...}}}) WITH
// world locations - the one capability v1 has that v2 does not (/v2/events is disabled). Each event carries a
// geometry: type (sphere/cylinder/poly) + center [x,y,z] + radius/height/rotation. Wiki: API:1/event_details.
namespace Api::V1
{
    struct EventLocation { std::string type; std::vector<double> center; double radius = 0; double height = 0; double rotation = 0; std::vector<double> zRange; nlohmann::json points; nlohmann::json raw; };
    struct EventDetail   { std::string id; std::string name; int level = 0; int mapId = 0; std::vector<std::string> flags; EventLocation location; nlohmann::json raw; };

    inline EventLocation ParseEventLocation(const nlohmann::json& j)
    {
        EventLocation l;
        if (!j.is_object()) return l;
        l.type = Json::Str(j, "type"); l.center = NumArray(j, "center"); l.radius = DblS(j, "radius");
        l.height = DblS(j, "height"); l.rotation = DblS(j, "rotation"); l.zRange = NumArray(j, "z_range");
        l.points = Json::Node(j, "points"); l.raw = j;
        return l;
    }
    inline std::vector<EventDetail> ParseEventDetails(const nlohmann::json& j)
    {
        std::vector<EventDetail> out;
        const nlohmann::json& evs = Json::Node(j, "events");
        if (evs.is_object()) for (auto it = evs.begin(); it != evs.end(); ++it)
        {
            const nlohmann::json& e = it.value();
            EventDetail d; d.id = it.key(); d.name = Json::Str(e, "name"); d.level = IntS(e, "level"); d.mapId = IntS(e, "map_id");
            d.flags = Json::StrArray(e, "flags"); d.location = ParseEventLocation(Json::Node(e, "location")); d.raw = e;
            out.push_back(std::move(d));
        }
        return out;
    }

    class EventDetailsEndpoint
    {
    public:
        explicit EventDetailsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<EventDetail>>)> cb) const                  { Fetch<std::vector<EventDetail>>(_c, "/v1/event_details.json", kV1StaticTtl, &ParseEventDetails, std::move(cb)); }
        void ById(const std::string& eventId, std::function<void(Result<std::vector<EventDetail>>)> cb) const { FetchQ<std::vector<EventDetail>>(_c, "/v1/event_details.json", "event_id", eventId, kV1StaticTtl, &ParseEventDetails, std::move(cb)); }
    private:
        Connection* _c;
    };
}
