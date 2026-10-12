#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/search/SearchModel.h"
#include "ui/search/SearchRow.h"
#include "ui/StoryRecommend.h"   // max-level story-progression view
#include "ui/ZoneRow.h"          // DrawZoneRow (story-view rows w/ the mapped check)
#include "guide/Story.h"         // StoryData::ReleaseName
#include "guide/StepStatus.h"    // ZoneComplete
#include <imgui.h>
#include <cstdio>
#include <vector>

// "Next up": zones recommended for your level. Reuses the Atlas search index + shared result row (same as the
// Zone Waypoints widget) so it matches the rest of the UI -- the level range shows as a pill and the whole row
// click-travels via the distance-gated card. The "Recommended" pill is hidden here (every row is a
// recommendation, so it'd be redundant -- the level pill is the useful bit).
//
// The dashboard owns the shared frame in both level-based and story-based modes. Sections keep their IDs;
// the shared story content retains its accent without adding another frame or surface.
void DashW::RecommendedZones(App& app, float w)
{
    // Max level: the level band is meaningless (every map is in-band), so recommend by STORY -- a "next story
    // step", then areas grouped by release in Journal/release order (mapped zones kept, marked).
    if (app.state.charLevel >= 80)
    {
        StoryRecommend::DrawNextStoryCard(app, w, /*compact*/ true, Gw2Ui::SectionPresentation::InlineAccent);
        Gw2Ui::Divider();
        const std::vector<StoryRecommend::ReleaseGroup>& groups = StoryRecommend::Groups(app);
        Gw2Ui::BeginSection("##recZonesGroups", Gw2Ui::SectionPresentation::Inline);
        {
            bool any = false;
            for (const StoryRecommend::ReleaseGroup& g : groups)
            {
                if (g.zones.empty()) continue;
                any = true;
                StoryRecommend::DrawReleaseHeader(g, 13.f);
                int stripe = 0;
                for (const Zone* z : g.zones)
                {
                    ZoneRowStyle style;
                    style.height = 24.f; style.nameFs = 16.f; style.actionFs = 14.f;
                    style.travelViaCard   = true;
                    style.showRecommended = false;
                    style.mapped          = ZoneComplete(app.progress, app.state.currentChar, *z);
                    style.stripe          = stripe++;
                    DrawZoneRow(app, z, style);
                }
            }
            if (!any) Gw2Ui::LabelWrapped("All story and map completion done.", Gw2Ui::kTextSub, false, nullptr, 14.f);
        }
        Gw2Ui::EndSection();
        return;
    }

    Search::EnsureIndex(app);
    const std::vector<const Zone*>& rec = app.zoneManager.RecommendZones(app.state.charLevel);
    Gw2Ui::BeginSection("##recZonesList", Gw2Ui::SectionPresentation::Inline);
    if (rec.empty()) { Gw2Ui::LabelWrapped("No recommendations.", Gw2Ui::kTextSub, false, nullptr, 14.f); Gw2Ui::EndSection(); return; }

    // All zone entries (slot 3 = this widget's own cache); we render the recommended ones in RecommendZones order.
    const std::vector<int>&           zoneRes = Search::Query(app, /*slot*/ 3, "", (1u << (int)Search::Kind::Zone),
                                                              "", 0, false, 1, 0.f, 0.f);
    const std::vector<Search::Entry>& all     = Search::Entries();

    Search::RowStyle st; st.height = 22.f * Gw2Ui::TextScale(); st.compact = true; st.showCopy = false; st.hideRecommended = true; st.tipMap = app.config.wMapTipRecommended;
    int shown = 0;
    for (const Zone* z : rec)   // in-band zones for the level; all of them (no 5-cap), minus completed
    {
        if (!z || z->MapId == app.state.activeMapId) continue;
        if (ZoneComplete(app.progress, app.state.currentChar, *z)) continue;
        const Search::Entry* e = nullptr;
        for (int idx : zoneRes) if (all[idx].mapId == z->MapId) { e = &all[idx]; break; }
        if (!e) continue;
        st.stripe = shown;
        Search::DrawResultRow(app, *e, st);
        ++shown;
    }
    if (shown == 0) Gw2Ui::LabelWrapped("No incomplete zones at your level.", Gw2Ui::kTextSub, false, nullptr, 14.f);
    Gw2Ui::EndSection();
}
