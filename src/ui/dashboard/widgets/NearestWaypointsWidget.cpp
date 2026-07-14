#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/search/SearchModel.h"
#include "ui/search/SearchRow.h"
#include "ui/dashboard/widgets/WidgetUtil.h"   // DashUtil::PlayerContinent
#include <imgui.h>
#include <string>
#include <vector>

// "Zone Waypoints": the active zone's UNLOCKED (confirmed) waypoints, nearest-first, each click-to-travel via
// the shared distance-gated card (copy is a secondary action). Reuses the Atlas search index + result row, so
// it matches the Atlas tab / Search widget exactly. Only confirmed waypoints show -- the ones you can travel to.
void DashW::NearestWaypoints(App& app, float w)
{
    (void)w;
    const Zone& z = app.state.zone;
    if (!z.Loaded)
    {
        Gw2Ui::Label("Needs a guided zone.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }
    Search::EnsureIndex(app);

    float px = 0.f, py = 0.f; int cont = z.ContinentId;
    const bool haveP = DashUtil::PlayerContinent(z, px, py);

    // The zone's waypoints, nearest-first (slot 2 = this widget's own cache).
    const std::vector<int>& res = Search::Query(app, /*slot*/ 2, "", (1u << (int)Search::Kind::Waypoint),
                                                "", z.MapId, haveP, cont, px, py);
    const std::vector<Search::Entry>& all = Search::Entries();

    Search::RowStyle st; st.height = 22.f * Gw2Ui::TextScale(); st.compact = true; st.showCopy = true; st.showFavorite = true; st.tipMap = app.config.wMapTipZoneWp;
    int shown = 0;
    for (int idx : res)
    {
        const Search::Entry& e = all[idx];
        if (e.stepId.empty() || !app.progress.IsConfirmed(app.state.currentChar, e.stepId)) continue;   // unlocked only
        st.stripe = shown;
        Search::DrawResultRow(app, e, st);
        ++shown;
    }
    if (shown == 0)
        Gw2Ui::Label("No unlocked waypoints in this zone yet -- reach one to add it.",
                     Gw2Ui::kTextDim, false, nullptr, 14.f);
}
