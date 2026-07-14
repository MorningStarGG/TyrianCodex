#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/quests (anonymous, day-cached): Story Journal steps of the personal story + Living World (id ->
// name/level/story + per-step goals). The `goals[].active` / `goals[].complete` strings are the in-progress
// and completed descriptions the journal shows. Powers the per-character Personal Story view. Wiki: API:2/quests.
namespace Api { class Connection; }
namespace Api::V2
{
    struct QuestGoal { std::string active; std::string complete; };
    struct Quest     { int id = 0; std::string name; int level = 0; int story = 0; std::vector<QuestGoal> goals; nlohmann::json raw; };

    inline Quest ParseQuest(const nlohmann::json& j)
    {
        Quest q;
        q.id    = Json::Int(j, "id");
        q.name  = Json::Str(j, "name");
        q.level = Json::Int(j, "level");
        q.story = Json::Int(j, "story");
        for (const auto& g : Json::Node(j, "goals"))
            if (g.is_object()) q.goals.push_back({ Json::Str(g, "active"), Json::Str(g, "complete") });
        q.raw = j;
        return q;
    }

    class QuestsEndpoint : public BulkEndpoint<Quest>
    { public: explicit QuestsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/quests", &ParseQuest, kStaticTtlSec) {} };
}
