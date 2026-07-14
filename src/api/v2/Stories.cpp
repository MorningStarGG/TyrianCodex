#include "Stories.h"

#include <string>

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    static constexpr int kStaticTtl = 24 * 60 * 60;   // story text is static reference data

    Story ParseStory(const nlohmann::json& j)
    {
        Story s;
        s.id          = Json::Int(j, "id");
        s.season      = Json::Str(j, "season");
        s.name        = Json::Str(j, "name");
        s.description = Json::Str(j, "description");
        s.timeline    = Json::Str(j, "timeline");
        s.level       = Json::Int(j, "level");
        s.order       = Json::Int(j, "order");
        s.raw         = j;
        return s;
    }

    StorySeason ParseStorySeason(const nlohmann::json& j)
    {
        StorySeason s;
        s.id      = Json::Str(j, "id");
        s.name    = Json::Str(j, "name");
        s.order   = Json::Int(j, "order");
        s.stories = Json::IntArray(j, "stories");
        s.raw     = j;
        return s;
    }

    void StoriesEndpoint::Get(int id, std::function<void(Result<Story>)> cb) const
    {
        Request req;
        req.path        = "/v2/stories/" + std::to_string(id);
        req.cacheTtlSec = kStaticTtl;
        _c->Get<Story>(std::move(req), &ParseStory, std::move(cb));
    }

    void StoriesEndpoint::Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Story>>)> cb) const
    {
        Request req;
        req.path        = "/v2/stories";
        req.cacheTtlSec = kStaticTtl;
        for (int id : ids) req.bulkIds.push_back(std::to_string(id));
        _c->Get<std::vector<Story>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<Story> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseStory(e)); }
                return out;
            },
            std::move(cb));
    }

    void StoriesEndpoint::GetAll(std::function<void(Result<std::vector<Story>>)> cb) const
    {
        Request req;
        req.path        = "/v2/stories";
        req.cacheTtlSec = kStaticTtl;
        req.query.emplace_back("ids", "all");
        _c->Get<std::vector<Story>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<Story> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseStory(e)); }
                return out;
            },
            std::move(cb));
    }

    void StorySeasonsEndpoint::GetAll(std::function<void(Result<std::vector<StorySeason>>)> cb) const
    {
        Request req;
        req.path        = "/v2/stories/seasons";
        req.cacheTtlSec = kStaticTtl;
        req.query.emplace_back("ids", "all");
        _c->Get<std::vector<StorySeason>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<StorySeason> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseStorySeason(e)); }
                return out;
            },
            std::move(cb));
    }
}
