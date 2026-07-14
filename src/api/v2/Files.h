#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/files (anonymous, day-cached): named UI asset icons (string id -> icon url). Wiki: API:2/files.
namespace Api { class Connection; }
namespace Api::V2
{
    struct File { std::string id; std::string icon; nlohmann::json raw; };

    inline File ParseFile(const nlohmann::json& j)
    {
        File f;
        f.id   = Json::Str(j, "id");
        f.icon = Json::Str(j, "icon");
        f.raw  = j;
        return f;
    }

    class FilesEndpoint : public BulkEndpoint<File, std::string>
    { public: explicit FilesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/files", &ParseFile, kStaticTtlSec) {} };
}
