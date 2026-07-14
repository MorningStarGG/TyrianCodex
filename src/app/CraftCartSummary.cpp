#include "app/CraftCartSummary.h"
#include "app/App.h"
#include "app/CraftCart.h"
#include "app/CraftingEngine.h"
#include "app/AccountData.h"

#include <algorithm>
#include <imgui.h>
#include <unordered_map>

namespace
{
    CraftCartSummary::Summary g_s;
    uint64_t g_token = ~0ull;
    double   g_nextRecalc = 0.0;

    void Recompute(App& app)
    {
        g_s = CraftCartSummary::Summary{};
        g_s.project = CraftCart::Active();
        g_s.itemCount = CraftCart::Count(g_s.project);
        if (!CraftingEngine::Ready()) { g_s.ready = false; return; }

        const std::vector<std::pair<int, long long>> items = CraftCart::Items(g_s.project);
        CraftingEngine::Opts opts{ app.config.craftUseOwnMaterials, app.config.craftSubComponents };
        std::unordered_map<int, long long> owned;
        if (app.config.craftUseOwnMaterials)
        {
            const AccountData::Model& m = AccountData::Get();
            if (m.haveMats) for (const auto& ms : m.materials) if (ms.id > 0 && ms.count > 0) owned[ms.id] += ms.count;
        }

        std::unordered_map<int, CraftCartSummary::Mat> agg;
        for (const auto& [id, qty] : items)
        {
            CraftingEngine::Plan pl = CraftingEngine::ComputePlan(id, qty, opts, owned);
            if (!pl.ok) continue;
            if (!pl.pricesComplete) g_s.pricesComplete = false;
            g_s.craftCost += pl.craftCost;
            CraftCartSummary::Group g; g.outId = id; g.outQty = qty;
            for (const CraftingEngine::ShopRow& sr : pl.shopping)
            {
                g.mats.push_back({ sr.itemId, sr.qty, sr.priced, sr.unitSell, sr.total });
                if (sr.qty > 0 && sr.priced && !CraftCart::IsGot(g_s.project, sr.itemId)) g.subtotal += sr.total;
                CraftCartSummary::Mat& a = agg[sr.itemId];
                a.itemId = sr.itemId; a.qty += sr.qty; a.unitSell = sr.unitSell;
                a.priced = a.priced || sr.priced;
                if (sr.total >= 0) a.total += sr.total;
            }
            g_s.byItem.push_back(std::move(g));
        }
        g_s.flat.reserve(agg.size());
        for (auto& kv : agg) g_s.flat.push_back(kv.second);
        std::sort(g_s.flat.begin(), g_s.flat.end(),
                  [](const CraftCartSummary::Mat& a, const CraftCartSummary::Mat& b) { return a.total > b.total; });
        for (const CraftCartSummary::Mat& m : g_s.flat)
            if (m.qty > 0 && m.priced && !CraftCart::IsGot(g_s.project, m.itemId)) g_s.stillNeed += m.total;
        g_s.ready = true;
    }
}

const CraftCartSummary::Summary& CraftCartSummary::Get(App& app)
{
    const uint64_t tok = CraftCart::Version() * 1000003ull + CraftingEngine::Version() * 7ull
                       + (app.config.craftUseOwnMaterials ? 1u : 0u) + (app.config.craftSubComponents ? 2u : 0u);
    const double now = ImGui::GetTime();
    if (g_token != tok || !g_s.ready || (!g_s.pricesComplete && now >= g_nextRecalc))
    {
        Recompute(app);
        g_token = tok;
        g_nextRecalc = now + 0.5;
    }
    return g_s;
}
