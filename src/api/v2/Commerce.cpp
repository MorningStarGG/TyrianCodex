#include "Commerce.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    Price ParsePrice(const nlohmann::json& j)
    {
        Price p;
        p.id          = Json::Int(j, "id");
        p.whitelisted = Json::Bool(j, "whitelisted");
        const nlohmann::json& buys  = Json::Node(j, "buys");
        const nlohmann::json& sells = Json::Node(j, "sells");
        if (buys.is_object())  { p.buyQty  = Json::Int(buys,  "quantity"); p.buyUnit  = Json::Int(buys,  "unit_price"); }
        if (sells.is_object()) { p.sellQty = Json::Int(sells, "quantity"); p.sellUnit = Json::Int(sells, "unit_price"); }
        p.raw = j;
        return p;
    }

    Listing ParseListing(const nlohmann::json& j)
    {
        Listing l;
        l.id  = Json::Int(j, "id");
        l.raw = j;   // the full buys[]/sells[] order book lives in raw
        return l;
    }

    Exchange ParseExchange(const nlohmann::json& j)
    {
        Exchange e;
        e.coinsPerGem = Json::Int(j, "coins_per_gem");
        e.quantity    = Json::Int(j, "quantity");
        e.raw         = j;
        return e;
    }

    Delivery ParseDelivery(const nlohmann::json& j)
    {
        Delivery d;
        d.coins = Json::Int64(j, "coins");
        d.raw   = j;   // the pending `items` list lives in raw
        return d;
    }

    Transaction ParseTransaction(const nlohmann::json& j)
    {
        Transaction t;
        t.id       = Json::Int(j, "id");
        t.itemId   = Json::Int(j, "item_id");
        t.price    = Json::Int(j, "price");
        t.quantity = Json::Int(j, "quantity");
        t.created  = Json::Str(j, "created");
        t.raw      = j;
        return t;
    }

    static std::vector<Transaction> ParseTransactions(const nlohmann::json& j)
    {
        std::vector<Transaction> out;
        if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseTransaction(e)); }
        return out;
    }

    void ExchangeEndpoint::Coins(int quantity, std::function<void(Result<Exchange>)> cb) const
    {
        Request req; req.path = "/v2/commerce/exchange/coins"; req.query = { { "quantity", std::to_string(quantity) } }; req.cacheTtlSec = 60;
        _c->Get<Exchange>(std::move(req), &ParseExchange, std::move(cb));
    }
    void ExchangeEndpoint::Gems(int quantity, std::function<void(Result<Exchange>)> cb) const
    {
        Request req; req.path = "/v2/commerce/exchange/gems"; req.query = { { "quantity", std::to_string(quantity) } }; req.cacheTtlSec = 60;
        _c->Get<Exchange>(std::move(req), &ParseExchange, std::move(cb));
    }

    void TransactionsEndpoint::Fetch(const char* sub, std::function<void(Result<std::vector<Transaction>>)> cb) const
    {
        Request req;
        req.path = std::string("/v2/commerce/transactions/") + sub;
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Tradingpost;
        req.cacheTtlSec = kCacheNever;
        _c->Get<std::vector<Transaction>>(std::move(req), &ParseTransactions, std::move(cb));
    }
    void TransactionsEndpoint::CurrentBuys(std::function<void(Result<std::vector<Transaction>>)> cb) const  { Fetch("current/buys",  std::move(cb)); }
    void TransactionsEndpoint::CurrentSells(std::function<void(Result<std::vector<Transaction>>)> cb) const { Fetch("current/sells", std::move(cb)); }
    void TransactionsEndpoint::HistoryBuys(std::function<void(Result<std::vector<Transaction>>)> cb) const  { Fetch("history/buys",  std::move(cb)); }
    void TransactionsEndpoint::HistorySells(std::function<void(Result<std::vector<Transaction>>)> cb) const { Fetch("history/sells", std::move(cb)); }

    void CommerceEndpoint::Delivery(std::function<void(Result<V2::Delivery>)> cb) const
    {
        Request req;
        req.path = "/v2/commerce/delivery";
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Tradingpost;
        req.cacheTtlSec = kCacheNever;
        _c->Get<V2::Delivery>(std::move(req), &ParseDelivery, std::move(cb));
    }
}
