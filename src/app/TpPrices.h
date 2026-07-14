#pragma once

namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// TpPrices: a small, bounded, session price cache for the live Trading Post (anonymous /v2/commerce/prices).
// Decoupled from App& so the shared item renderers (ItemRender) can show buy/sell + flip margin without an
// App reference: a draw call does Want(id) to request a price and Get(id, out) to read whatever is cached.
// Pump() (once per frame, after api.Pump()) batches the wanted ids into bulk requests on the worker thread;
// callbacks land on the render thread and fill the cache. Prices are LIVE (never bundled); a 60s TTL matches
// the PricesEndpoint server cache, so re-Want refetches only when stale. NOT account data -> not in AccountData.
// -----------------------------------------------------------------------------------------------------
namespace TpPrices
{
    struct Price
    {
        int  buyUnit = 0, sellUnit = 0;   // highest buy order / lowest sell listing, in copper (0 = none)
        int  buyQty  = 0, sellQty  = 0;   // order-book depth (supply/demand)
        bool known = false;               // we have a definitive answer for this id (fetched at least once)
        bool tradeable = false;           // the item is listable on the TP (false = account-bound / no listing)
    };

    void Init(Api::Client* api);
    void Shutdown();

    void Want(int id);              // request id's price (coalesced; no-op when fresh-cached or in flight)
    bool Get(int id, Price& out);   // cached price for id; returns out.known (false => not available yet)
    void Pump();                    // per-frame: batch-fetch the wanted ids (TTL + in-flight + bounded cache)
}
