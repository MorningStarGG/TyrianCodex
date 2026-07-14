#pragma once

#include <string>
#include <vector>

namespace Wiki
{
    struct NativeRailItem
    {
        std::string text;
        std::string href;
    };

    struct NativeRailRow
    {
        std::string label;
        std::vector<NativeRailItem> values;
    };

    // A region/area "statistics" entry: a completion count + the wiki map icon next to it
    // (e.g. 86 renown hearts, 142 points of interest). Parsed from <div class="statistics">.
    struct NativeRailStat
    {
        std::string count;     // the number text ("86")
        std::string iconUrl;   // resolved wiki icon URL
        std::string label;     // the link title ("Renown Heart") -- used for the tooltip
    };

    // A non-icon infobox image (the area map from .image_wrapper, or the "Image(s)" screenshot).
    struct NativeRailImage
    {
        std::string url;       // resolved thumbnail URL (what we display)
        std::string href;      // the File: page (opens the lightbox)
        std::string caption;   // optional caption ("Map of Ascalon")
    };

    struct NativeRail
    {
        std::string title;
        std::string imageUrl;          // item-style header icon (only for plain item infoboxes)
        std::string imageHref;
        std::vector<NativeRailRow>  rows;
        std::vector<NativeRailStat> stats;    // region/area completion stats
        std::vector<NativeRailImage> images;  // area map + screenshot(s)
        int apiId = 0;
        bool isItem = false;           // the infobox is an ITEM infobox (class "infobox item") -> apiId is an item id.
                                       // For area/zone/strike/instance infoboxes apiId is a MAP id, NOT an item id.
    };

    NativeRail BuildNativeRail(const std::string& railHtml, const std::string& fallbackTitle);
}
