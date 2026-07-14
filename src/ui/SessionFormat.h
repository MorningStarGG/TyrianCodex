#pragma once
#include "app/StaticData.h"
#include <cstdio>
#include <string>

// Shared currency formatting for the Session Tracker surfaces (tab + dashboard widget + Info Panel datatext), so
// the gold g/s/c + signed-delta formatting + the "gold (not the API's 'Coin')" label live in ONE place.
namespace SessionFmt
{
    // Copper -> "Ng Ms Kc", trimming leading zero parts (pass an ABSOLUTE value; see Signed()).
    inline std::string Coins(long long copper)
    {
        const long long g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
        char b[48];
        if (g > 0)      std::snprintf(b, sizeof(b), "%lldg %llds %lldc", g, s, c);
        else if (s > 0) std::snprintf(b, sizeof(b), "%llds %lldc", s, c);
        else            std::snprintf(b, sizeof(b), "%lldc", c);
        return b;
    }
    // Display label for a currency id -- gold shows as "Gold", not the API's "Coin"; others from the catalog.
    inline const char* Label(int id)
    {
        if (id == 1) return "Gold";
        const char* n = StaticData::CurrencyName(id);
        return (n && n[0]) ? n : "Currency";
    }
    // Signed delta string: gold formatted g/s/c, others a plain integer; always a leading "+"/"-".
    inline std::string Signed(int id, long long v)
    {
        const char* sign = v < 0 ? "-" : "+";
        const long long a = v < 0 ? -v : v;
        return std::string(sign) + (id == 1 ? Coins(a) : std::to_string(a));
    }
    // Absolute amount string (no sign): gold g/s/c, others plain.
    inline std::string Amount(int id, long long v)
    {
        const long long a = v < 0 ? -v : v;
        return id == 1 ? Coins(a) : std::to_string(a);
    }
    // "H:MM:SS" from a second count.
    inline std::string Clock(int sec)
    {
        if (sec < 0) sec = 0;
        char b[24]; std::snprintf(b, sizeof(b), "%d:%02d:%02d", sec / 3600, (sec % 3600) / 60, sec % 60);
        return b;
    }
}
