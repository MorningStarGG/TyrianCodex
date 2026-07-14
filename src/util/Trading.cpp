#include "util/Trading.h"
#include <algorithm>   // std::max
#include <cstdio>      // std::snprintf

// The two TP cuts on a sale, each floored (integer divide) with a 1-copper floor. Matches the in-game
// "you receive" line and the community tools (gw2tp / Mystic-Margin): a seller nets sell - 5% - 10%.
namespace Trading
{
    std::string Coins(long long c)
    {
        if (c <= 0) return "0c";
        const long long g = c / 10000, s = (c / 100) % 100, cp = c % 100;
        char b[64];
        if (g > 0)      std::snprintf(b, sizeof(b), "%lldg %02llds %02lldc", g, s, cp);
        else if (s > 0) std::snprintf(b, sizeof(b), "%llds %02lldc", s, cp);
        else            std::snprintf(b, sizeof(b), "%lldc", cp);
        return b;
    }

    int ListingFee(int p) { return p <= 0 ? 0 : (int)std::max(1LL, (long long)p * 5  / 100); }   // 64-bit multiply: p*5 / p*10 overflow int at high unit prices
    int SaleTax(int p)    { return p <= 0 ? 0 : (int)std::max(1LL, (long long)p * 10 / 100); }

    int TpNet(int sell)
    {
        if (sell <= 0) return 0;
        const int n = sell - ListingFee(sell) - SaleTax(sell);
        return n < 0 ? 0 : n;
    }

    int    FlipProfit(int buy, int sell) { return TpNet(sell) - buy; }
    double Roi(int profit, int cost)     { return cost > 0 ? (double)profit / (double)cost : 0.0; }
}
