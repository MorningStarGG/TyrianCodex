#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/search/SearchModel.h"
#include "ui/search/SearchRow.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

// "Search": a compact universal location search on the dashboard -- find any waypoint / POI / vista / hero
// point / heart / zone and click to travel (the same distance-gated card). Shares the index + the result row
// with the Atlas tab (no duplication). Shows a handful of top results; the Atlas tab is the full database.
void DashW::Search(App& app, float w)
{
    (void)w;
    static Search::Filters f;
    Search::EnsureIndex(app);

    Search::DrawFilterBar(app, f, /*compact*/ true);   // just the search box (the dashboard is tight)

    if (f.text[0] == '\0')
    {
        // wrap the hint to the widget width so it doesn't clip at half width
        const char* hint = "Type to find a waypoint, POI, vista, hero point, heart or zone.";
        const float ww = ImGui::GetContentRegionAvail().x;
        const float hh = Gw2Ui::MeasureWrappedHeight(hint, 14.f, ww);
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(ww, hh));
        Gw2Ui::LabelIn(hp, ImVec2(hp.x + ww, hp.y + hh), hint, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, 14.f, ww);
        return;
    }

    const std::vector<int>&           res = Search::RunQuery(app, /*slot*/ 1, f);
    const std::vector<Search::Entry>& all = Search::Entries();
    if (res.empty()) { Gw2Ui::Label("No matches.", Gw2Ui::kTextSub, false, nullptr, 14.f); return; }

    Search::RowStyle st; st.height = 22.f * Gw2Ui::TextScale(); st.compact = true; st.showCopy = true; st.showFavorite = true; st.tipMap = app.config.wMapTipSearch;
    const int n = std::min((int)res.size(), 8);   // top matches; the Atlas tab shows them all
    for (int i = 0; i < n; ++i) { st.stripe = i; Search::DrawResultRow(app, all[res[i]], st); }
    if ((int)res.size() > n)
    {
        char more[48]; std::snprintf(more, sizeof(more), "+%d more in the Atlas tab", (int)res.size() - n);
        Gw2Ui::Label(more, Gw2Ui::kTextDim, false, nullptr, 14.f);
    }
}
