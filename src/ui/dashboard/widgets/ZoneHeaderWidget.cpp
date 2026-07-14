#include "Widgets.h"
#include "app/App.h"
#include "ui/viewer/ViewerHeader.h"   // DrawViewerHeader (the compact viewer's scenic zone banner)
#include "guide/CurrentChar.h"
#include <set>
#include <string>
#include <vector>

// The compact viewer's scenic zone banner as a widget: map art + compass + zone name + level + "X/Y done"
// (dungeon name/path or "No guide route" off-coverage, mode-aware -- reuses DrawViewerHeader). selfFramed: it
// is its own banner, so the dashboard adds no card; defaults to hover chrome (a clean banner).
void DashW::ZoneHeader(App& app, float w)
{
    int done = 0, total = 0;
    const Zone& z = app.state.zone;
    if (z.Loaded && !app.state.inDungeon && !z.Steps.empty())
    {
        // cache the done/total counts behind progress version + character + map (don't rebuild the set + rescan
        // the steps every frame). The block is skipped in a dungeon/off-coverage, so it never needs inDungeon.
        static uint64_t s_ver = (uint64_t)-1; static std::string s_char; static uint32_t s_map = 0;
        static int s_done = 0, s_total = 0;
        if (s_ver != app.progress.Version() || s_char != app.state.currentChar || s_map != z.MapId)
        {
            const std::vector<std::string>& ids = app.progress.CompletedIds(app.state.currentChar);
            const std::vector<std::string>& cfd = app.progress.ConfirmedIds(app.state.currentChar);
            std::set<std::string> doneSet(ids.begin(), ids.end());
            doneSet.insert(cfd.begin(), cfd.end());   // count reached waypoints (confirmed-set only)
            s_done = 0; s_total = 0;
            for (const Step& s : z.Steps) { ++s_total; if (doneSet.count(s.StepId)) ++s_done; }
            s_ver = app.progress.Version(); s_char = app.state.currentChar; s_map = z.MapId;
        }
        done = s_done; total = s_total;
    }
    // selfFramed widget: the banner reads GetContentRegionAvail() (which over-reports a half slot), so pass the
    // real slot width + the widget flag so the half-width reflow + responsive sizing key off the right number.
    ZoneHeaderOpts zh;
    zh.pills = app.config.zhWidgetPills; zh.bar = app.config.zhWidgetBar; zh.barText = app.config.zhWidgetBarText;
    zh.isWidget = true; zh.width = w;
    DrawViewerHeader(app, done, total, 18.f, /*compact*/ true, zh);
}
