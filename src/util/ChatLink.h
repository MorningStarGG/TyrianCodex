#pragma once
#include <cstddef>
#include <string>

// Small GW2 chat-link encoder (the "[&...]" base64 link types). Centralized so any surface can build a link without
// re-deriving the byte layout. A type-byte link = [type][4-byte little-endian id], base64-encoded, wrapped in "[&...]"
// -- e.g. Recipe(14338) == "[&CQI4AAA=]" (type 0x09 + id 14338). (The Homestead browser uses Recipe() for the
// decoration recipe chat link; glyphs reuse the item catalog's precomputed chatLink; cats use the builder's
// waypoint chat link, so they need no encoder here.)
namespace ChatLink
{
    inline std::string Base64(const unsigned char* data, std::size_t len)
    {
        static const char* kT = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < len; i += 3)
        {
            unsigned v = (unsigned)data[i] << 16;
            if (i + 1 < len) v |= (unsigned)data[i + 1] << 8;
            if (i + 2 < len) v |= (unsigned)data[i + 2];
            out += kT[(v >> 18) & 0x3F];
            out += kT[(v >> 12) & 0x3F];
            out += (i + 1 < len) ? kT[(v >> 6) & 0x3F] : '=';
            out += (i + 2 < len) ? kT[v & 0x3F] : '=';
        }
        return out;
    }

    // A [type byte][4-byte little-endian id] link (the item/recipe family). type 0x02 = item, 0x09 = recipe.
    inline std::string Encode(unsigned char type, int id)
    {
        if (id <= 0) return {};
        const unsigned u = (unsigned)id;
        const unsigned char b[5] = {
            type, (unsigned char)(u & 0xFF), (unsigned char)((u >> 8) & 0xFF),
            (unsigned char)((u >> 16) & 0xFF), (unsigned char)((u >> 24) & 0xFF)
        };
        return "[&" + Base64(b, 5) + "]";
    }

    inline std::string Recipe(int recipeId) { return Encode(0x09, recipeId); }   // a crafting recipe (Handiwork/scribe/...)
    inline std::string Item(int itemId)     { return Encode(0x02, itemId); }     // an item (for reference)
    inline std::string Skin(int skinId)     { return Encode(0x0A, skinId); }     // a wardrobe skin (type 0x0A)
}
