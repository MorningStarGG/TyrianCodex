#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/backstory/{questions,answers} (anonymous, day-cached): the character-creation biography catalog.
// A question (e.g. "My Personality") lists candidate answer ids; an answer carries the `journal` line that the
// in-game Story Journal stitches into the character's biography. Resolving a character's /backstory answer ids
// against these gives the "I'm <name>." paragraph. Wiki: API:2/backstory.
namespace Api { class Connection; }
namespace Api::V2
{
    struct BackstoryQuestion { int id = 0; std::string title; std::string description; std::vector<std::string> answers; int order = 0; nlohmann::json raw; };
    struct BackstoryAnswer   { std::string id; std::string title; std::string description; std::string journal; int question = 0; nlohmann::json raw; };

    inline BackstoryQuestion ParseBackstoryQuestion(const nlohmann::json& j)
    {
        BackstoryQuestion q;
        q.id = Json::Int(j, "id"); q.title = Json::Str(j, "title"); q.description = Json::Str(j, "description");
        q.answers = Json::StrArray(j, "answers"); q.order = Json::Int(j, "order"); q.raw = j;
        return q;
    }
    inline BackstoryAnswer ParseBackstoryAnswer(const nlohmann::json& j)
    {
        BackstoryAnswer a;
        a.id = Json::Str(j, "id"); a.title = Json::Str(j, "title"); a.description = Json::Str(j, "description");
        a.journal = Json::Str(j, "journal"); a.question = Json::Int(j, "question"); a.raw = j;
        return a;
    }

    class BackstoryQuestionsEndpoint : public BulkEndpoint<BackstoryQuestion>             { public: explicit BackstoryQuestionsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/backstory/questions", &ParseBackstoryQuestion, kStaticTtlSec) {} };
    class BackstoryAnswersEndpoint   : public BulkEndpoint<BackstoryAnswer, std::string>  { public: explicit BackstoryAnswersEndpoint(Connection* c)   : BulkEndpoint(c, "/v2/backstory/answers",   &ParseBackstoryAnswer,   kStaticTtlSec) {} };

    // /v2/backstory facade: .Questions() + .Answers().
    class BackstoryEndpoint
    {
    public:
        explicit BackstoryEndpoint(Connection* c) : _c(c) {}
        BackstoryQuestionsEndpoint Questions() const { return BackstoryQuestionsEndpoint(_c); }
        BackstoryAnswersEndpoint   Answers()   const { return BackstoryAnswersEndpoint(_c); }
    private:
        Connection* _c;
    };
}
