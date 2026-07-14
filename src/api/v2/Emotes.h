#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/emotes (anonymous, day-cached): the emote catalog (string id -> chat commands + unlock item ids).
// Wiki: API:2/emotes.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Emote { std::string id; std::vector<std::string> commands; std::vector<int> unlockItems; nlohmann::json raw; };

    inline Emote ParseEmote(const nlohmann::json& j)
    {
        Emote e;
        e.id          = Json::Str(j, "id");
        e.commands    = Json::StrArray(j, "commands");
        e.unlockItems = Json::IntArray(j, "unlock_items");
        e.raw         = j;
        return e;
    }

    class EmotesEndpoint : public BulkEndpoint<Emote, std::string>
    { public: explicit EmotesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/emotes", &ParseEmote, kStaticTtlSec) {} };
}
