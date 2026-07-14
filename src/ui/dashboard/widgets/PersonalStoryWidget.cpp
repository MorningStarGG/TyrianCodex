#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "guide/Story.h"
#include "ui/tabs/JournalTab.h"   // PersonalStoryProgress -- the real per-character personal-story progress (API)
#include <imgui.h>
#include <string>
#include <vector>

// Story progress from the bundled spine + manual-completion store (account-wide, plus the per-character
// personal story). Shows overall completion and the first release still in progress.
void DashW::PersonalStory(App& app, float w)
{
    if (app.stories.Empty())
    {
        Gw2Ui::Label("Story data unavailable.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }

    const std::string ch = app.state.currentChar;
    const PsProgress ps = PersonalStoryProgress(app);   // real per-character personal-story progress (API); ready=false w/o key
    // cache the tallies behind the manual-store version + character + the API step count (don't rescan the whole
    // spine every frame). app.stories is a static bundled spine, so it needs no version.
    static uint64_t s_ver = (uint64_t)-1; static std::string s_char; static int s_psDone = -1;
    static int s_total = 0, s_done = 0, s_curDone = 0, s_curTotal = 0; static std::string s_curRel;
    if (s_ver != app.storyStore.Version() || s_char != ch || s_psDone != ps.done)
    {
        int total = 0, done = 0; std::string curRel; int curDone = 0, curTotal = 0;
        for (const std::string& rel : StoryData::ReleaseOrder())
        {
            const std::vector<StoryEpisode>* eps = app.stories.Episodes(rel);
            if (!eps || eps->empty()) continue;
            if (StoryData::PerCharacter(rel) && ps.ready && ps.total > 0)   // personal story -> the real API step progress, not manual marks
            {
                total += ps.total; done += ps.done;
                if (curRel.empty() && ps.done < ps.total) { curRel = rel; curDone = ps.done; curTotal = ps.total; }
                continue;
            }
            const std::string scope = StoryData::PerCharacter(rel) ? ch : std::string();
            int rDone = 0;
            for (const StoryEpisode& e : *eps)
            {
                ++total;
                if (app.storyStore.IsDone(rel + ":" + e.name, scope)) { ++done; ++rDone; }
            }
            if (curRel.empty() && rDone < (int)eps->size()) { curRel = rel; curDone = rDone; curTotal = (int)eps->size(); }
        }
        s_total = total; s_done = done; s_curRel = curRel; s_curDone = curDone; s_curTotal = curTotal;
        s_ver = app.storyStore.Version(); s_char = ch; s_psDone = ps.done;
    }
    const int total = s_total, done = s_done; const int curDone = s_curDone, curTotal = s_curTotal;
    const std::string& curRel = s_curRel;

    if (total == 0) { Gw2Ui::Label("No story chapters.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    char lbl[48]; std::snprintf(lbl, sizeof(lbl), "Story  %d/%d", done, total);
    Gw2Ui::ProgressBar((float)done / (float)total, lbl, w, 16.f, IM_COL32(200, 160, 230, 255));
    ImGui::Spacing();
    if (!curRel.empty())
    {
        // For the personal story, name the CHAPTER you're on ("Order Neophyte") -- more useful than the release
        // name ("Core Tyria") in a widget already titled Personal Story. Release name for the LW/expansion rows.
        const std::string curName = (StoryData::PerCharacter(curRel) && ps.ready && !ps.nextName.empty())
                                    ? ps.nextName : StoryData::ReleaseName(curRel);
        char b[96]; std::snprintf(b, sizeof(b), "Current: %s  (%d/%d)", curName.c_str(), curDone, curTotal);
        // wrap to the card width so a long release name doesn't overflow at narrow / half width
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = Gw2Ui::MeasureWrappedHeight(b, 14.f, w);
        ImGui::Dummy(ImVec2(w, h));
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), b, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(210, 200, 230, 255), false, nullptr, 14.f, w);
    }
    else
    {
        Gw2Ui::Label("All bundled story marked complete.", IM_COL32(150, 200, 150, 255), false, nullptr, 14.f);
    }
}
