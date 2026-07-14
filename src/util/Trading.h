#pragma once

// -----------------------------------------------------------------------------------------------------
// util/Trading: GW2 Trading Post fee math, centralized so the Items-tab price pills, the Trading Post tab,
// and the crafting calculator all use ONE copy. The TP takes two cuts off a SALE: a 5% listing fee and a
// 10% sale tax, each rounded down with a 1-copper minimum (so a seller nets ~85% of the list price). Pure --
// integer copper in, integer copper out; no globals, no allocation.
// -----------------------------------------------------------------------------------------------------
#include <string>

namespace Trading
{
    std::string Coins(long long copper);          // "Xg Ys Zc" (pads s/c; "0c" for <= 0) -- shared coin label
    int    ListingFee(int unitPrice);             // 5% of the list price, min 1c (0 for a 0/neg price)
    int    SaleTax(int unitPrice);                // 10% of the list price, min 1c
    int    TpNet(int sellUnit);                   // what the seller actually receives: sell - listing - tax (>= 0)
    int    FlipProfit(int buyUnit, int sellUnit); // buy-order -> sell-listing margin after fees: TpNet(sell) - buy
    double Roi(int profit, int cost);             // profit / cost as a fraction (0 when cost <= 0)
}
