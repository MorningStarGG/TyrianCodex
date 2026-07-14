#include "Widgets.h"
#include "WidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "guide/CurrentChar.h"
#include <imgui.h>
#include <set>
#include <string>

// Live character snapshot: name, level, profession/race, current map, and this map's completion %.
void DashW::Character(App& app, float w)
{
    const std::string name = CurrentCharName();
    if (name.empty()) { Gw2Ui::Label("No character loaded.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    Gw2Ui::Label(name.c_str(), IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.2f);

    char line[96];
    const char* prof = MumbleIdent ? Mumble::ProfessionName(MumbleIdent->Profession) : "";
    const char* race = MumbleIdent ? Mumble::RaceName(MumbleIdent->Race) : "";
    if (app.state.charLevel > 0) std::snprintf(line, sizeof(line), "Level %d  %s  %s", app.state.charLevel, prof, race);
    else                         std::snprintf(line, sizeof(line), "%s  %s", prof, race);
    Gw2Ui::Label(line, IM_COL32(196, 190, 172, 255), false, nullptr, 14.f);

    const Zone& z = app.state.zone;
    if (z.Loaded)
    {
        Gw2Ui::Label(z.Name.c_str(), IM_COL32(190, 220, 255, 255), false, nullptr, 14.f);
        if (!z.Steps.empty())
        {
            // cache the done count behind progress version + character + map (rebuilding the set + scanning the
            // steps every frame is the documented churn pattern; recompute only when one of those changes).
            static uint64_t s_ver = (uint64_t)-1; static std::string s_char; static uint32_t s_map = 0; static int s_done = 0;
            if (s_ver != app.progress.Version() || s_char != app.state.currentChar || s_map != z.MapId)
            {
                const std::vector<std::string>& ids = app.progress.CompletedIds(app.state.currentChar);
                const std::vector<std::string>& cfd = app.progress.ConfirmedIds(app.state.currentChar);
                std::set<std::string> done(ids.begin(), ids.end());
                done.insert(cfd.begin(), cfd.end());   // count reached waypoints (they live only in the confirmed set)
                s_done = 0; for (const Step& s : z.Steps) if (done.count(s.StepId)) ++s_done;
                s_ver = app.progress.Version(); s_char = app.state.currentChar; s_map = z.MapId;
            }
            const int d = s_done;
            char pl[40]; std::snprintf(pl, sizeof(pl), "Map completion  %d/%d", d, (int)z.Steps.size());
            Gw2Ui::ProgressBar((float)d / (float)z.Steps.size(), pl, w, 14.f);
        }
    }
}
