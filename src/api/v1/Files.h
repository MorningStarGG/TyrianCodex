#pragma once
#include "Common.h"

// GET /v1/files.json (anonymous): named UI asset references ({ "map_complete": {file_id, signature}, ... }).
// Wiki: API:1/files.
namespace Api::V1
{
    struct File { std::string key; int fileId = 0; std::string signature; nlohmann::json raw; };

    inline std::vector<File> ParseFiles(const nlohmann::json& j)
    {
        std::vector<File> out;
        if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it)
        {
            File f; f.key = it.key(); f.fileId = Json::Int(it.value(), "file_id"); f.signature = Json::Str(it.value(), "signature"); f.raw = it.value();
            out.push_back(std::move(f));
        }
        return out;
    }

    class FilesEndpoint
    {
    public:
        explicit FilesEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<File>>)> cb) const { Fetch<std::vector<File>>(_c, "/v1/files.json", kV1StaticTtl, &ParseFiles, std::move(cb)); }
    private:
        Connection* _c;
    };
}
