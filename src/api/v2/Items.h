#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Result.h"

// GET /v2/items (by id)  (anonymous; static). The item catalog: name, icon (a render.guildwars2.com URL),
// rarity, type. The Journal tab resolves an achievement's Item reward (the story reward box / "chest" the
// game shows under Achievements and Rewards) to its icon + name. Wiki: API:2/items.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Item
    {
        int            id = 0;
        std::string    name;
        std::string    icon;     // render.guildwars2.com URL
        std::string    rarity;   // Junk/Basic/Fine/Masterwork/Rare/Exotic/Ascended/Legendary
        std::string    type;
        std::string    chatLink;
        nlohmann::json raw;
    };

    Item ParseItem(const nlohmann::json& j);

    class ItemsEndpoint
    {
    public:
        explicit ItemsEndpoint(Connection* c) : _c(c) {}
        void Get(int id, std::function<void(Result<Item>)> cb) const;
        void Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Item>>)> cb) const;
        void GetIds(std::function<void(Result<std::vector<int>>)> cb) const;   // GET /v2/items -> every item id
    private:
        Connection* _c;
    };
}
