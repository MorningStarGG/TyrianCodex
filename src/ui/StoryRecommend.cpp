#include "ui/StoryRecommend.h"

#include "app/App.h"
#include "guide/Story.h"           // StoryData / StoryEpisode
#include "guide/StepStatus.h"      // ZoneComplete
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"        // OpenZoneTravelChoice
#include "ui/SettingsWindow.h"     // OpenSettingsTab + SettingsTabJournal
#include "model/Dataset.h"         // Zone

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
    // Best-effort "entry" zone for a release: the bundled zone tagged that release with the lowest (MinLevel,
    // Name) -- typically the expansion's entry map. Null when the release has no bundled open-world zone.
    const Zone* EntryZone(App& app, const std::string& rel)
    {
        const Zone* best = nullptr;
        for (const auto& kv : app.zones)
        {
            const Zone& z = kv.second;
            if (z.Release != rel) continue;
            if (!best || z.MinLevel < best->MinLevel || (z.MinLevel == best->MinLevel && z.Name < best->Name))
                best = &z;
        }
        return best;
    }

    // Cached per-release MAP-completion grouping (the expensive part -- ZoneComplete scans a zone's steps). The
    // story counts + union filter are cheap and recompute each call in Groups().
    struct RelMapData { std::vector<const Zone*> zones; int mapDone = 0, mapTotal = 0; };
    std::map<std::string, RelMapData> g_mapData;
    std::string g_char     = "\x01";          // sentinel: force first build
    uint64_t    g_ver      = (uint64_t)-1;
    size_t      g_zoneCount = (size_t)-1;

    void EnsureMapData(App& app)
    {
        const std::string& ch  = app.state.currentChar;
        const uint64_t     ver = app.progress.Version();
        const size_t       cnt = app.zones.size();
        if (ch == g_char && ver == g_ver && cnt == g_zoneCount) return;

        g_mapData.clear();
        for (const auto& kv : app.zones)
        {
            const Zone& z = kv.second;
            RelMapData& d = g_mapData[z.Release];
            d.zones.push_back(&z);
            ++d.mapTotal;
            if (ZoneComplete(app.progress, ch, z)) ++d.mapDone;
        }
        for (auto& kv : g_mapData)
            std::sort(kv.second.zones.begin(), kv.second.zones.end(), [](const Zone* a, const Zone* b) {
                if (a->MinLevel != b->MinLevel) return a->MinLevel < b->MinLevel;
                return a->Name < b->Name;
            });
        g_char = ch; g_ver = ver; g_zoneCount = cnt;
    }

    std::vector<StoryRecommend::ReleaseGroup> g_groups;   // rebuilt each call (cheap) from cached map data + story counts
}

StoryRecommend::NextStory StoryRecommend::Next(App& app)
{
    NextStory ns;
    for (const std::string& rel : StoryData::ReleaseOrder())
    {
        const std::vector<StoryEpisode>* eps = app.stories.Episodes(rel);
        if (!eps || eps->empty()) continue;
        for (const StoryEpisode& e : *eps)
        {
            if (!app.storyCompletion.EpisodeDone(rel, e))
            {
                ns.release   = &rel;          // element of the static ReleaseOrder() vector -> stable address
                ns.episode   = &e;            // element of app.stories episodes -> app-owned
                ns.entryZone = EntryZone(app, rel);
                return ns;
            }
        }
    }
    return ns;   // every bundled episode complete
}

const std::vector<StoryRecommend::ReleaseGroup>& StoryRecommend::Groups(App& app)
{
    EnsureMapData(app);
    g_groups.clear();
    for (const std::string& rel : StoryData::ReleaseOrder())
    {
        ReleaseGroup g;
        g.release = rel;

        if (const std::vector<StoryEpisode>* eps = app.stories.Episodes(rel))
        {
            g.storyTotal = (int)eps->size();
            for (const StoryEpisode& e : *eps) if (app.storyCompletion.EpisodeDone(rel, e)) ++g.storyDone;
        }
        auto it = g_mapData.find(rel);
        if (it != g_mapData.end()) { g.zones = it->second.zones; g.mapDone = it->second.mapDone; g.mapTotal = it->second.mapTotal; }

        // Union: incomplete on either axis. Skip releases with no bundled zones -- the detail card covers the
        // "next story" nudge; the LIST only recommends areas (zones) to head to.
        if (g.mapTotal == 0) continue;
        const bool storyIncomplete = g.storyTotal > 0 && g.storyDone < g.storyTotal;
        const bool mapIncomplete   = g.mapDone < g.mapTotal;
        if (storyIncomplete || mapIncomplete) g_groups.push_back(std::move(g));
    }
    return g_groups;
}

void StoryRecommend::DrawReleaseHeader(const ReleaseGroup& g, float fontSize)
{
    const std::string rn = StoryData::ReleaseName(g.release);
    char counts[64];
    std::snprintf(counts, sizeof(counts), "Story %d/%d  -  Maps %d/%d", g.storyDone, g.storyTotal, g.mapDone, g.mapTotal);
    const float cw = Gw2Ui::CardInnerWidth();   // card-aware (== content width when not in a card, e.g. the viewer)

    char one[160];
    std::snprintf(one, sizeof(one), "%s   %s", rn.c_str(), counts);
    if (Gw2Ui::MeasureWidth(one, fontSize) <= cw)   // fits on one line (full width)
    {
        Gw2Ui::Label(one, Gw2Ui::kTextSelected, false, nullptr, fontSize);
        return;
    }
    // Narrow (half width): release name wrapped on top, the counts on a smaller line beneath -> never clips.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float  nh = Gw2Ui::MeasureWrappedHeight(rn.c_str(), fontSize, cw);
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(cw, nh));
    Gw2Ui::LabelDL(dl, p, ImVec2(p.x + cw, p.y + nh), rn.c_str(),
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextSelected, false, nullptr, fontSize, cw);
    Gw2Ui::Label(counts, Gw2Ui::kTextDim, false, nullptr, fontSize - 2.f);
}

void StoryRecommend::DrawNextStoryCard(App& app, float w, bool compact)
{
    const NextStory ns = Next(app);
    const float fs = compact ? 15.f : 18.f;

    Gw2Ui::BeginAccentCard("story-next-detail", w, Gw2Ui::kGold,
                           compact ? IM_COL32(0, 0, 0, 86)     : IM_COL32(3, 5, 5, 188),
                           compact ? IM_COL32(126, 110, 78, 96) : IM_COL32(150, 116, 58, 128));

    if (!ns.episode || !ns.release)
    {
        Gw2Ui::Label("All story content complete.", IM_COL32(160, 235, 170, 255), false, nullptr, fs);
        Gw2Ui::EndCard();
        return;
    }

    Gw2Ui::Label("Next story step", Gw2Ui::kTextDim, false, nullptr, fs - 4.f);
    Gw2Ui::Label(StoryData::ReleaseName(*ns.release).c_str(), Gw2Ui::kGold, false, nullptr, fs - 1.f);
    Gw2Ui::Label(ns.episode->name.c_str(), IM_COL32(235, 230, 215, 255), true, nullptr, fs + 1.f);

    if (!ns.episode->description.empty())
    {
        Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 62));
        const float  cw = Gw2Ui::CardInnerWidth();
        const float  h  = Gw2Ui::MeasureWrappedHeight(ns.episode->description.c_str(), fs - 2.f, cw);
        const ImVec2 p  = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cw, h));
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + cw, p.y + h), ns.episode->description.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(218, 212, 196, 255), false,
                       Gw2Ui::Gw2Italic(), fs - 2.f, cw);
    }

    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));
    const float cw       = Gw2Ui::CardInnerWidth();
    const bool  hasTravel = ns.entryZone != nullptr;
    const float gap      = Gw2Ui::Scaled(6.f);
    const float bw       = hasTravel ? (cw - gap) * 0.5f : cw;
    if (Gw2Ui::ActionButtonPx("Open Journal", bw, Gw2Ui::Scaled(26.f), Gw2Ui::ActionButtonVariant::Primary, "Open the Story Journal"))
        OpenSettingsTab(app, SettingsTabJournal);
    if (hasTravel)
    {
        ImGui::SameLine(0.f, gap);
        char tip[128]; std::snprintf(tip, sizeof(tip), "Travel toward %s", ns.entryZone->Name.c_str());
        if (Gw2Ui::ActionButtonPx("Travel", bw, Gw2Ui::Scaled(26.f), Gw2Ui::ActionButtonVariant::Normal, tip))
            OpenZoneTravelChoice(app, ns.entryZone->MapId);
    }
    Gw2Ui::EndCard();
}
