#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Result.h"

// Trading-post (commerce) endpoints. Prices + Listings are ANONYMOUS bulk (current TP buy/sell data, so a
// SHORT cache); Exchange is the anonymous gem<->coin rate; Delivery + Transactions are AUTHENTICATED
// (tradingpost scope) - the player's pickup box and their current/historical buy/sell orders. Full payloads
// (order books, item lists) stay in `raw`. Wiki: API:2/commerce/<name>.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Price       { int id = 0; bool whitelisted = false; int buyQty = 0; int buyUnit = 0; int sellQty = 0; int sellUnit = 0; nlohmann::json raw; };
    struct Listing     { int id = 0; nlohmann::json raw; };   // full buys[]/sells[] order book in raw
    struct Exchange    { int coinsPerGem = 0; int quantity = 0; nlohmann::json raw; };
    struct Delivery    { int64_t coins = 0; nlohmann::json raw; };   // `items` list in raw
    struct Transaction { int id = 0; int itemId = 0; int price = 0; int quantity = 0; std::string created; nlohmann::json raw; };

    Price       ParsePrice(const nlohmann::json& j);
    Listing     ParseListing(const nlohmann::json& j);
    Exchange    ParseExchange(const nlohmann::json& j);
    Delivery    ParseDelivery(const nlohmann::json& j);
    Transaction ParseTransaction(const nlohmann::json& j);

    class PricesEndpoint   : public BulkEndpoint<Price>   { public: explicit PricesEndpoint(Connection* c)   : BulkEndpoint(c, "/v2/commerce/prices",   &ParsePrice,   60) {} };
    class ListingsEndpoint : public BulkEndpoint<Listing> { public: explicit ListingsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/commerce/listings", &ParseListing, 60) {} };

    // /v2/commerce/exchange/{coins|gems}?quantity=N (anonymous): the current exchange rate for a quantity.
    class ExchangeEndpoint
    {
    public:
        explicit ExchangeEndpoint(Connection* c) : _c(c) {}
        void Coins(int quantity, std::function<void(Result<Exchange>)> cb) const;   // coins -> gems
        void Gems(int quantity, std::function<void(Result<Exchange>)> cb) const;    // gems -> coins
    private:
        Connection* _c;
    };

    // /v2/commerce/transactions/{current|history}/{buys|sells} (auth: tradingpost): the player's TP orders.
    class TransactionsEndpoint
    {
    public:
        explicit TransactionsEndpoint(Connection* c) : _c(c) {}
        void CurrentBuys(std::function<void(Result<std::vector<Transaction>>)> cb) const;
        void CurrentSells(std::function<void(Result<std::vector<Transaction>>)> cb) const;
        void HistoryBuys(std::function<void(Result<std::vector<Transaction>>)> cb) const;
        void HistorySells(std::function<void(Result<std::vector<Transaction>>)> cb) const;
    private:
        void Fetch(const char* sub, std::function<void(Result<std::vector<Transaction>>)> cb) const;
        Connection* _c;
    };

    // The commerce facade: .Prices()/.Listings() (anon bulk), .Exchange(), .Delivery() (auth), .Transactions().
    class CommerceEndpoint
    {
    public:
        explicit CommerceEndpoint(Connection* c) : _c(c) {}
        PricesEndpoint       Prices()       const { return PricesEndpoint(_c); }
        ListingsEndpoint     Listings()     const { return ListingsEndpoint(_c); }
        ExchangeEndpoint     Exchange()     const { return ExchangeEndpoint(_c); }
        TransactionsEndpoint Transactions() const { return TransactionsEndpoint(_c); }
        void Delivery(std::function<void(Result<V2::Delivery>)> cb) const;   // /v2/commerce/delivery (auth: tradingpost)
    private:
        Connection* _c;
    };
}
