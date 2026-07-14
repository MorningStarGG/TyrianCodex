#pragma once
#include <string>
#include <vector>
class App;

// Shared live-Trading-Post price detail for a single item: the Prices / Order book / Gem-exchange cards, used by
// the Items tab's slide-out price panel. Fetches /v2/commerce listings + exchange on demand (cached); current
// buy/sell come from the shared TpPrices cache. Draws into the CURRENT window/child.
namespace ItemTp
{
    // True if the item is in the cached official Trading Post id set. Drives the Items-tab "Tradeable only"
    // filter and the inventory/card click gate.
    bool Tradeable(int itemId);

    // Draw the price detail for itemId (name = its display name, for the header).
    void DrawDetail(App& app, int itemId, const std::string& name, bool showHeader = true);

    // Draw an item's datawars2 price-history chart (buy + sell avg over the last `rangeDays`) into a width x height
    // box, with an optional volume sub-chart. Handles the fetch + loading/empty states + the hover tooltip. SHARED
    // by the inline card and the Trading Post "Charts" view, so the chart logic lives in one place.
    void DrawPriceHistory(App& app, int itemId, float width, float height, int rangeDays, bool showVolume);

    // Shared slide-out TP price card. `selectedId` is the requested open item (0 = closing); `shownId/name`
    // are retained during the close animation. The area rectangle is the surface the card should slide over.
    void DrawSlideCard(App& app, int& selectedId, int& shownId, std::string& shownName,
                       float& anim, int& scrollItem, float areaX, float areaY, float areaW, float areaH);
}
