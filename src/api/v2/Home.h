#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/home/{cats,nodes} (anonymous, day-cached): the home-instance catalogs - cats (id -> hint name) and
// gathering nodes (string id). (Distinct from /v2/account/home/*, the player's unlocks.) Wiki: API:2/home.
namespace Api { class Connection; }
namespace Api::V2
{
    struct HomeCat  { int id = 0; std::string hint; nlohmann::json raw; };
    struct HomeNode { std::string id; nlohmann::json raw; };

    inline HomeCat  ParseHomeCat(const nlohmann::json& j)  { HomeCat c;  c.id = Json::Int(j, "id"); c.hint = Json::Str(j, "hint"); c.raw = j; return c; }
    inline HomeNode ParseHomeNode(const nlohmann::json& j) { HomeNode n; if (j.is_string()) n.id = j.get<std::string>(); else n.id = Json::Str(j, "id"); n.raw = j; return n; }

    class HomeCatsEndpoint  : public BulkEndpoint<HomeCat>               { public: explicit HomeCatsEndpoint(Connection* c)  : BulkEndpoint(c, "/v2/home/cats",  &ParseHomeCat,  kStaticTtlSec) {} };
    class HomeNodesEndpoint : public BulkEndpoint<HomeNode, std::string> { public: explicit HomeNodesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/home/nodes", &ParseHomeNode, kStaticTtlSec) {} };

    // /v2/home facade: .Cats() + .Nodes().
    class HomeEndpoint
    {
    public:
        explicit HomeEndpoint(Connection* c) : _c(c) {}
        HomeCatsEndpoint  Cats()  const { return HomeCatsEndpoint(_c); }
        HomeNodesEndpoint Nodes() const { return HomeNodesEndpoint(_c); }
    private:
        Connection* _c;
    };
}
