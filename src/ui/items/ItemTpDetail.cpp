#include "ui/items/ItemTpDetail.h"
#include "app/App.h"
#include "app/TpEligibility.h"
#include "app/TpPrices.h"
#include "app/PriceHistory.h"
#include "ui/Gw2Ui.h"
#include "util/Trading.h"
#include "api/Client.h"
#include "api/v2/Commerce.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>

namespace
{
    // Selected item's order book (top levels), fetched on demand + cached (never bundled, F7/F10:
    // a failed fetch is a no-op and never clears good cached data).
    int g_listId = 0;
    bool g_listInflight = false;
    std::vector<std::pair<int, int>> g_sells, g_buys; // (unit_price, quantity)

    void EnsureListings(App &app, int id)
    {
        if (id <= 0 || id == g_listId || g_listInflight)
            return;
        g_listInflight = true;
        app.api.V2().Commerce().Listings().Get(std::vector<int>{id}, [id](Api::Result<std::vector<Api::V2::Listing>> r)
                                               {
            g_listInflight = false;
            if (!r.ok || r.value.empty()) return;                 // F10: leave the prior book intact on failure
            g_listId = id; g_sells.clear(); g_buys.clear();
            const nlohmann::json& raw = r.value[0].raw;
            auto take = [](const nlohmann::json& arr, std::vector<std::pair<int, int>>& out) {
                if (!arr.is_array()) return;
                for (const auto& lvl : arr) { if ((int)out.size() >= 6) break; out.emplace_back(lvl.value("unit_price", 0), lvl.value("quantity", 0)); }
            };
            take(raw.contains("sells") ? raw["sells"] : nlohmann::json(), g_sells);   // API: sells ascending, buys descending
            take(raw.contains("buys")  ? raw["buys"]  : nlohmann::json(), g_buys); });
    }

    // The inline price-history chart card -- a thin wrapper over the SHARED ItemTp::DrawPriceHistory
    // (which the Trading Post "Charts" view also uses). Shown between the Prices + Order-book cards.
    void DrawHistoryCard(App &app, int itemId)
    {
        Gw2Ui::BeginCard("tp-history");
        Gw2Ui::SectionHeader("Price history");
        ItemTp::DrawPriceHistory(app, itemId, Gw2Ui::ContentWidth(), 220.f, 180, true); // price + volume strip
        Gw2Ui::EndCard();
    }
}

void ItemTp::DrawPriceHistory(App &app, int itemId, float width, float height, int rangeDays, bool showVolume)
{
    (void)app;
    PriceHistory::Want(itemId);
    PriceHistory::Series hist;
    const bool have = PriceHistory::Get(itemId, hist);
    if (!have || hist.size() < 2)
    {
        Gw2Ui::Label(PriceHistory::IsFetching(itemId) ? "Loading price history..." : "No price history yet.",
                     Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }

    const ImU32 cSell = IM_COL32(235, 178, 110, 255); // match the order-book sell colour
    const ImU32 cBuy = IM_COL32(140, 205, 140, 255);  // ... and buy colour

    // Last `rangeDays` days; fall back to the whole series if that window is too sparse.
    const int cutoff = PriceHistory::TodayDay() - rangeDays;
    static std::vector<ImVec2> sellPts, buyPts, volPts; // reused buffers -- no per-frame alloc (UI thread only)
    for (int pass = 0; pass < 2; ++pass)
    {
        const int cut = pass == 0 ? cutoff : 0;
        sellPts.clear();
        buyPts.clear();
        volPts.clear();
        for (const auto &p : hist)
        {
            if (p.day < cut)
                continue;
            if (p.sellAvg > 0)
                sellPts.push_back(ImVec2((float)p.day, (float)p.sellAvg));
            if (p.buyAvg > 0)
                buyPts.push_back(ImVec2((float)p.day, (float)p.buyAvg));
            if (showVolume)
                volPts.push_back(ImVec2((float)p.day, (float)(p.buyQty + p.sellQty)));
        }
        if (sellPts.size() >= 2 || buyPts.size() >= 2)
            break;
    }

    Gw2Ui::ChartSeries series[2];
    series[0].points = sellPts.data();
    series[0].count = (int)sellPts.size();
    series[0].color = cSell;
    series[0].name = "Sell";
    series[1].points = buyPts.data();
    series[1].count = (int)buyPts.size();
    series[1].color = cBuy;
    series[1].name = "Buy";

    auto fmtY = [](float copper) -> std::string
    {
        const int c = (int)(copper + 0.5f);
        char b[24];
        if (c >= 10000)
            std::snprintf(b, sizeof(b), "%.1fg", c / 10000.0);
        else if (c >= 100)
            std::snprintf(b, sizeof(b), "%ds", c / 100);
        else
            std::snprintf(b, sizeof(b), "%dc", c);
        return b;
    };
    auto onHover = [&hist, cSell, cBuy](float nearestX, ImVec2)
    {
        const int day = (int)(nearestX + 0.5f);
        int buy = 0, sell = 0;
        for (const auto &p : hist)
            if (p.day == day)
            {
                buy = p.buyAvg;
                sell = p.sellAvg;
                break;
            }
        ImGui::BeginTooltip();
        Gw2Ui::Label(PriceHistory::DayLabel(day).c_str(), Gw2Ui::kGold, false, nullptr, 16.f);
        if (sell > 0)
            Gw2Ui::Label(("Sell  " + Trading::Coins(sell)).c_str(), cSell, false, nullptr, 16.f);
        if (buy > 0)
            Gw2Ui::Label(("Buy   " + Trading::Coins(buy)).c_str(), cBuy, false, nullptr, 16.f);
        ImGui::EndTooltip();
    };

    // legend (bigger on the full-size Charts view than on the compact inline card)
    const float legFs = height >= 260.f ? 16.f : 13.f;
    Gw2Ui::Label("Sell", cSell, false, nullptr, legFs);
    ImGui::SameLine();
    Gw2Ui::Label("Buy", cBuy, false, nullptr, legFs);

    const bool drawVol = showVolume && volPts.size() >= 2;
    const float volH = drawVol ? 72.f : 0.f;
    const float priceH = std::max(70.f, height - volH - (drawVol ? 22.f : 0.f));
    Gw2Ui::LineChart("##pricehist", ImVec2(width, priceH), series, 2, fmtY, onHover);

    if (drawVol)
    {
        const ImU32 cVol = IM_COL32(120, 160, 210, 255);
        Gw2Ui::Label("Volume (units/day)", Gw2Ui::kTextDim, false, nullptr, height >= 260.f ? 15.f : 12.f);
        Gw2Ui::ChartSeries vs[1];
        vs[0].points = volPts.data();
        vs[0].count = (int)volPts.size();
        vs[0].color = cVol;
        vs[0].name = "Vol";
        auto fmtVol = [](float v) -> std::string
        {
            const int n = (int)(v + 0.5f);
            char b[24];
            if (n >= 1000)
                std::snprintf(b, sizeof(b), "%.1fk", n / 1000.0);
            else
                std::snprintf(b, sizeof(b), "%d", n);
            return b;
        };
        auto volHover = [&hist, cVol](float nearestX, ImVec2)
        {
            const int day = (int)(nearestX + 0.5f);
            int vol = 0;
            for (const auto &p : hist)
                if (p.day == day)
                {
                    vol = p.buyQty + p.sellQty;
                    break;
                }
            ImGui::BeginTooltip();
            Gw2Ui::Label(PriceHistory::DayLabel(day).c_str(), Gw2Ui::kGold, false, nullptr, 16.f);
            Gw2Ui::Label(("Volume  " + std::to_string(vol)).c_str(), cVol, false, nullptr, 16.f);
            ImGui::EndTooltip();
        };
        Gw2Ui::LineChart("##pricevol", ImVec2(width, volH), vs, 1, fmtVol, volHover);
    }
}

bool ItemTp::Tradeable(int itemId)
{
    return TpEligibility::IsTradeable(itemId);
}

void ItemTp::DrawDetail(App &app, int itemId, const std::string &name, bool showHeader)
{
    if (showHeader)
    {
        Gw2Ui::Label(name.empty() ? ("Item " + std::to_string(itemId)).c_str() : name.c_str(), Gw2Ui::kGold, true, nullptr, 22.f);
        ImGui::Spacing();
    }

    if (!TpEligibility::IsReady())
    {
        Gw2Ui::Label("Loading Trading Post item data...", Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }
    if (!TpEligibility::IsTradeable(itemId))
    {
        Gw2Ui::Label("Not sellable on the Trading Post.", Gw2Ui::kTextSub, false, nullptr, 16.f);
        return;
    }

    EnsureListings(app, itemId);
    TpPrices::Want(itemId);
    TpPrices::Price tp;
    const bool haveP = TpPrices::Get(itemId, tp);

    auto stat = [&](const char *k, const std::string &v, ImU32 col)
    {
        Gw2Ui::TextValueRow(k, v.c_str(), 0.f, 25.f, 17.f, IM_COL32(200, 192, 170, 255), col);
    };

    if (!haveP)
        Gw2Ui::Label("Loading prices...", Gw2Ui::kTextDim, false, nullptr, 16.f);
    else if (!tp.tradeable)
        Gw2Ui::Label("Not sellable on the Trading Post.", Gw2Ui::kTextSub, false, nullptr, 16.f);
    else
    {
        const int profit = Trading::FlipProfit(tp.buyUnit, tp.sellUnit);
        const ImU32 pc = profit > 0 ? Gw2Ui::kSuccessGreen : Gw2Ui::kLossRed;
        Gw2Ui::BeginCard("tp-prices");
        Gw2Ui::SectionHeader("Prices");
        stat("Sell (lowest)", Trading::Coins(tp.sellUnit), IM_COL32(255, 230, 170, 255));
        stat("Buy (highest)", Trading::Coins(tp.buyUnit), IM_COL32(255, 230, 170, 255));
        stat("Spread", Trading::Coins(std::max(0, tp.sellUnit - tp.buyUnit)), IM_COL32(220, 210, 185, 255));
        stat("Flip margin (after 15% fee)", (profit < 0 ? "-" : "") + Trading::Coins(profit < 0 ? -profit : profit), pc);
        {
            char b[32];
            std::snprintf(b, sizeof(b), "%.0f%%", Trading::Roi(profit, tp.buyUnit) * 100.0);
            stat("ROI", b, pc);
        }
        stat("Supply (sell listings)", std::to_string(tp.sellQty), IM_COL32(220, 210, 185, 255));
        stat("Demand (buy orders)", std::to_string(tp.buyQty), IM_COL32(220, 210, 185, 255));
        Gw2Ui::EndCard();

        DrawHistoryCard(app, itemId);

        if (!g_sells.empty() || !g_buys.empty())
        {
            Gw2Ui::BeginCard("tp-book");
            Gw2Ui::SectionHeader("Order book");
            auto book = [&](const char *title, const std::vector<std::pair<int, int>> &lv, ImU32 accent)
            {
                Gw2Ui::Label(title, Gw2Ui::Alpha(Gw2Ui::kGold, 235), false, nullptr, 14.f);
                int maxQ = 1;
                for (const auto &l : lv)
                    maxQ = std::max(maxQ, l.second);
                ImGui::PushID(title); // namespace this book -- else "sells row N" and "buys row N" share an id (their rowNo collides), which broke hover detection
                int rowNo = 0;
                for (const auto &l : lv)
                {
                    ImGui::PushID(rowNo++);
                    const float w = Gw2Ui::ContentWidth();
                    const float rh = 21.f;
                    const Gw2Ui::RowHotspot row = Gw2Ui::Row("##bookrow", -1, rh, w);
                    const ImVec2 p = row.min;
                    ImDrawList *dl = ImGui::GetWindowDrawList();

                    dl->AddRectFilled(p, ImVec2(p.x + w * std::max(0.03f, (float)l.second / (float)maxQ), p.y + rh), Gw2Ui::Alpha(accent, 48), 2.f);
                    dl->AddRect(p, ImVec2(p.x + w, p.y + rh), IM_COL32(255, 255, 255, 10), 2.f);

                    Gw2Ui::RowLabel(dl, row, 8.f, w * 0.38f, Trading::Coins(l.first).c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::Alpha(accent, 255), false, nullptr, 16.f);
                    char q[16];
                    std::snprintf(q, sizeof(q), "x%d", l.second);
                    Gw2Ui::RowLabel(dl, row, 0.f, 8.f, q, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(196, 188, 168, 255), false, nullptr, 14.f);
                    ImGui::PopID();
                }
                ImGui::PopID(); // title
            };
            book("Lowest sells", g_sells, IM_COL32(235, 178, 110, 255));
            ImGui::Spacing();
            book("Highest buys", g_buys, IM_COL32(140, 205, 140, 255));
            Gw2Ui::EndCard();
        }
    }
}

void ItemTp::DrawSlideCard(App &app, int &selectedId, int &shownId, std::string &shownName,
                           float &anim, int &scrollItem, float areaX, float areaY, float areaW, float areaH)
{
    if (shownId > 0 && TpEligibility::IsReady() && !TpEligibility::IsTradeable(shownId))
    {
        selectedId = 0;
        shownId = 0;
        anim = 0.f;
        return;
    }

    const float dt = ImGui::GetIO().DeltaTime;
    anim += ((selectedId > 0 ? 1.f : 0.f) - anim) * std::min(1.f, dt * 12.f);
    if (selectedId == 0 && anim < 0.02f)
        shownId = 0;
    if (shownId <= 0 || anim <= 0.02f || areaW <= 80.f || areaH <= 80.f)
        return;

    const float margin = 8.f;
    const float maxH = std::max(180.f, areaH - margin * 2.f);
    const float bh = std::min(500.f, maxH);
    const float by = areaY + (areaH - bh) * 0.5f;
    const float pw = std::min(400.f, areaW * 0.6f);
    const float px = areaX + areaW - margin - pw * anim;

    ImGui::PushID(&selectedId);
    ImGui::SetCursorScreenPos(ImVec2(px, by));
    ImGui::BeginChild("##tpslide", ImVec2(pw, bh), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::SetScrollY(0.f);
    ImDrawList *sdl = ImGui::GetWindowDrawList();
    const ImVec2 wmin = ImGui::GetWindowPos();
    const ImVec2 wsz = ImGui::GetWindowSize();
    sdl->AddRectFilled(wmin, ImVec2(wmin.x + wsz.x, wmin.y + wsz.y), IM_COL32(16, 16, 14, 255));
    sdl->AddRect(wmin, ImVec2(wmin.x + wsz.x, wmin.y + wsz.y), IM_COL32(130, 112, 76, 170), 4.f, 0, 1.2f);
    sdl->AddLine(wmin, ImVec2(wmin.x, wmin.y + wsz.y), Gw2Ui::Alpha(Gw2Ui::kGold, 210), 2.f);

    const float pad = 12.f;
    const float headerH = 40.f;
    const float closeW = 16.f;
    const float closeH = 18.f;
    const ImVec2 closeAt(wmin.x + wsz.x - pad - closeW, wmin.y + (headerH - closeH) * 0.5f);
    const std::string header = shownName.empty() ? ("Item " + std::to_string(shownId)) : shownName;
    // Long names overflow the header (LabelDL doesn't clip): shrink-to-fit (floor 16px, the ViewerHeader idiom)
    // then ellipsize if it still won't fit. The 8px gap to the close button absorbs the stroke's slight overhang.
    const float headL = wmin.x + pad, headR = closeAt.x - 8.f;
    const float headAvail = std::max(20.f, headR - headL);
    float headFs = 22.f;
    const float headW = Gw2Ui::MeasureWidth(header.c_str(), headFs);
    if (headW > headAvail && headW > 1.f)
        headFs = std::max(16.f, headFs * (headAvail / headW));
    const std::string headShown = Gw2Ui::Ellipsize(header, headFs, headAvail);
    Gw2Ui::LabelDL(sdl, ImVec2(headL, wmin.y), ImVec2(headR, wmin.y + headerH),
                   headShown.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   Gw2Ui::kGold, true, nullptr, headFs);
    sdl->AddLine(ImVec2(wmin.x + pad, wmin.y + headerH), ImVec2(wmin.x + wsz.x - pad, wmin.y + headerH),
                 IM_COL32(130, 112, 76, 95), 1.f);

    ImGui::PushID("tpclose");
    ImGui::SetCursorScreenPos(closeAt);
    if (ImGui::InvisibleButton("##x", ImVec2(closeW, closeH)))
        selectedId = 0;
    ImGui::PopID();
    const bool closeHovered = ImGui::IsItemHovered();
    if (closeHovered)
        Gw2Ui::Tooltip("Close");

    const float bodyY = wmin.y + headerH + pad;
    ImGui::SetCursorScreenPos(ImVec2(wmin.x + pad, bodyY));
    ImGui::BeginChild("##tpslide_detail", ImVec2(wsz.x - pad * 2.f, std::max(40.f, wsz.y - headerH - pad * 2.f)), false);
    if (scrollItem != shownId)
    {
        ImGui::SetScrollY(0.f);
        scrollItem = shownId;
    }
    DrawDetail(app, shownId, shownName, false);
    ImGui::EndChild();

    const ImU32 xcol = closeHovered ? IM_COL32(255, 200, 200, 255) : IM_COL32(180, 168, 140, 255);
    sdl->AddLine(ImVec2(closeAt.x + 2.f, closeAt.y + 3.f), ImVec2(closeAt.x + 13.f, closeAt.y + 14.f), xcol, 1.6f);
    sdl->AddLine(ImVec2(closeAt.x + 13.f, closeAt.y + 3.f), ImVec2(closeAt.x + 2.f, closeAt.y + 14.f), xcol, 1.6f);
    ImGui::EndChild();
    ImGui::PopID();
}
