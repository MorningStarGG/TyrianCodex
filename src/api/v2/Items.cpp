#include "Items.h"

#include <string>

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    static constexpr int kStaticTtl = 24 * 60 * 60;   // the item catalog is static reference data

    Item ParseItem(const nlohmann::json& j)
    {
        Item it;
        it.id     = Json::Int(j, "id");
        it.name   = Json::Str(j, "name");
        it.icon   = Json::Str(j, "icon");
        it.rarity = Json::Str(j, "rarity");
        it.type   = Json::Str(j, "type");
        it.chatLink = Json::Str(j, "chat_link");
        it.raw    = j;
        return it;
    }

    void ItemsEndpoint::Get(int id, std::function<void(Result<Item>)> cb) const
    {
        Request req;
        req.path        = "/v2/items/" + std::to_string(id);
        req.cacheTtlSec = kStaticTtl;
        _c->Get<Item>(std::move(req), &ParseItem, std::move(cb));
    }

    void ItemsEndpoint::Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Item>>)> cb) const
    {
        Request req;
        req.path        = "/v2/items";
        req.cacheTtlSec = kStaticTtl;
        for (int id : ids) req.bulkIds.push_back(std::to_string(id));
        _c->Get<std::vector<Item>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<Item> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseItem(e)); }
                return out;
            },
            std::move(cb));
    }

    void ItemsEndpoint::GetIds(std::function<void(Result<std::vector<int>>)> cb) const
    {
        Request req;
        req.path        = "/v2/items";   // the bare index = the array of every item id
        req.cacheTtlSec = kStaticTtl;
        _c->Get<std::vector<int>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<int> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) if (e.is_number_integer()) out.push_back(e.get<int>()); }
                return out;
            },
            std::move(cb));
    }
}
