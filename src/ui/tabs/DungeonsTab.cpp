#include "ui/tabs/DungeonsTab.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/Glue.h"
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include "ui/GuideViewer.h"       // TravelToLocation / OpenZoneTravelChoice (content travel)
#include "ui/tabs/MapThumbnail.h" // GroupTooltip (hover map for content rows)
#include "ui/items/ItemRender.h"  // ItemUI::DrawCopyMenu (right-click: copy name / chat link / wiki)
#include "app/AccountData.h"      // HasKey / HasScope (raid-clear tracker gate)
#include "api/core/Permissions.h" // TokenPermission::Progression
#include "render/glyphs/Glyphs.h" // Render::Glyph (scope-chip + travel icons)
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ui/tabs/SettingsCommon.h"

// A dungeon name -> its maps.pack slug (the Atlas "Dungeon" MapInfo.StaticMap). Exact name first, then a
// contains-match (handles an instance named "Arah" -> the Atlas "The Ruined City of Arah").
static std::string DungeonMapSlug(App &app, const std::string &name)
{
    auto low = [](const std::string &s)
    { std::string o; for (char c : s) o += (char)std::tolower((unsigned char)c); return o; };
    const std::string ln = low(name);
    for (const MapInfo &mi : app.mapIndex)
        if (mi.Kind == "Dungeon" && low(mi.Name) == ln)
            return mi.StaticMap;
    for (const MapInfo &mi : app.mapIndex)
        if (mi.Kind == "Dungeon" && low(mi.Name).find(ln) != std::string::npos)
            return mi.StaticMap;
    return std::string();
}

// The Dungeons-tab cards are STATIC (app.instances loads once): one card per dungeon name, merging the story +
// explorable map ids, with the explorable path names. Built ONCE; only `here` (current dungeon) is per-frame.
struct DgnCard
{
    std::string name;
    std::string mapSlug; // precomputed once (DungeonMapSlug scans app.mapIndex twice -- never per-frame per-card)
    const Instance *expl = nullptr;
    const Instance *story = nullptr;
    std::vector<std::string> paths;
};

static std::vector<DgnCard> g_dgnCards;

static size_t g_dgnCardsBuilt = (size_t)-1;

static void BuildDungeonCards(App &app)
{
    g_dgnCards.clear();
    std::map<std::string, std::vector<const Instance *>> byName;
    for (const auto &kv : app.instances)
        if (kv.second.Family == "dungeon") // the Dungeons scope is dungeons BY TYPE; story/tutorial/home -> their scopes
            byName[kv.second.Name].push_back(&kv.second);
    for (const auto &kv : byName)
    {
        DgnCard c;
        c.name = kv.first;
        c.mapSlug = DungeonMapSlug(app, c.name);
        for (const Instance *in : kv.second)
        {
            if (in->Mode == "explorable")
                c.expl = in;
            else if (in->Mode == "story")
                c.story = in;
        }
        if (c.expl)
            for (const InstanceRoute &rt : c.expl->Routes)
                c.paths.push_back(rt.Name); // OWN the names: the Instance is move-replaced when its geometry lazy-loads on entry (stub Routes would dangle a c_str)
        g_dgnCards.push_back(std::move(c));
    }
    g_dgnCardsBuilt = app.instances.size();
}

// The Dungeons SCOPE body: a status line, then the rich dungeon cards.
static void DrawDungeonsBody(App &app)
{
    if (app.instances.empty())
    {
        ImGui::Spacing();
        CenteredLabel("No dungeon data bundled.", Gw2Ui::kTextSub, 18.f, -1.f, false);
        return;
    }
    if (g_dgnCardsBuilt != app.instances.size())
        BuildDungeonCards(app); // build the STATIC cards ONCE

    char status[192];
    if (app.state.inDungeon && app.state.inst)
        std::snprintf(status, sizeof(status), "You're in %s. Pick your path - the arrow + steps follow it.", app.state.inst->Name.c_str());
    else
        std::snprintf(status, sizeof(status), "Pick your choice of path here to be followed inside the dungeon");
    CenteredLabel(status, Gw2Ui::kGold, 18.f);
    ImGui::Spacing();

    ImGui::BeginChild("##dungeonslist", ImVec2(0.f, 0.f), false);
    Gw2Ui::CardGrid((int)g_dgnCards.size(), 300.f, 10.f, [&](int i, float cardW)
                    {
        const DgnCard& c = g_dgnCards[i];
        const bool here = app.state.inst && ((c.expl && app.state.inst->MapId == c.expl->MapId) || (c.story && app.state.inst->MapId == c.story->MapId));
        const Texture_t* tex = ImageCache::GetMapImage(c.mapSlug.c_str());
        char id[48]; std::snprintf(id, sizeof(id), "##dgc%d", i);
        const std::string nm = c.name + (here ? "   (here)" : "");
        auto body = [&](float iw) {
            if (c.expl && !c.paths.empty())   // the explorable PATH dropdown at the bottom of the card (persists + live re-routes)
            {
                int sel = app.state.instRouteChoice.count(c.expl->MapId) ? app.state.instRouteChoice[c.expl->MapId] : 0;
                sel = std::clamp(sel, 0, (int)c.paths.size() - 1);
                char dd[40]; std::snprintf(dd, sizeof(dd), "##dgnpath%u", c.expl->MapId);
                float ddW = 0.f;   // Gw2Ui::Dropdown auto-fits to content (longest item @18px + 36); match it so we can centre
                for (const std::string& pth : c.paths) ddW = (std::max)(ddW, Gw2Ui::MeasureWidth(pth.c_str(), 18.f));
                ddW = (std::min)(iw, ddW + 36.f * Gw2Ui::GlobalScale());
                std::vector<const char*> pv; pv.reserve(c.paths.size());   // transient const char* view for Dropdown
                for (const std::string& s : c.paths) pv.push_back(s.c_str());
                const ImVec2 ddp = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(ddp.x + (iw - ddW) * 0.5f, ddp.y));   // centre the dropdown in the card
                if (Gw2Ui::DropdownPx(dd, pv.data(), (int)pv.size(), &sel, ddW))
                {
                    app.state.instRouteChoice[c.expl->MapId] = sel;
                    if (app.state.inst && app.state.inst->MapId == c.expl->MapId) app.zoneManager.ActivateInstanceRoute(sel);
                }
            }
            else if (c.story)
            {
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                Gw2Ui::LabelIn(sp, ImVec2(sp.x + iw, sp.y + 20.f), "Story mode (auto-route)", Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
                ImGui::Dummy(ImVec2(iw, 20.f));
            }
        };
        const bool hov = Gw2Ui::ImageCard(id, tex, nm.c_str(), nullptr, cardW, body,
                                          here ? IM_COL32(48, 74, 48, 150) : 0, here ? IM_COL32(120, 180, 120, 200) : 0);
        char mid[56]; std::snprintf(mid, sizeof(mid), "%s_m", id);
        ItemUI::DrawCopyMenu(mid, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), c.name, ""); });
    ImGui::EndChild();
}

// ----- the broader CONTENT scopes (Story / Fractals / Raids / Strikes / Home): a per-section progress-tracker card
// (live account data -- no guessed achievement ids, no faked rotations) + the instances/places below.
namespace
{
    std::string Lower(const std::string &s)
    {
        std::string o;
        for (char c : s)
            o += (char)std::tolower((unsigned char)c);
        return o;
    }

    std::string PrettyId(const std::string &s) // "vale_guardian" -> "Vale Guardian"
    {
        std::string o;
        bool cap = true;
        for (char c : s)
        {
            if (c == '_' || c == '-')
            {
                o += ' ';
                cap = true;
            }
            else
            {
                o += cap ? (char)std::toupper((unsigned char)c) : c;
                cap = false;
            }
        }
        return o;
    }

    // ---- the trackers read the ONE shared, disk-cached, prewarmed account model (AccountData) -- never their own
    // uncached fetches. AccountData::Get() returns last-known instantly (dashboard-cache.json) and the Content tab
    // declares its interest (ContentNeededDomains, wired into the central per-frame Tick) so its domains refresh on
    // a TTL, exactly like the dashboard widgets + Info Panel data texts. ----
    int64_t WalletVal(int cur)
    {
        for (const auto &w : AccountData::Get().wallet)
            if (w.id == cur)
                return w.value;
        return -1;
    }

    const ImU32 kGreen = Gw2Ui::kSuccessGreen;

    Gw2Ui::TileIcon GIcon(Render::Glyph g, ImU32 col = Gw2Ui::kGold)
    {
        return [g, col](ImDrawList *dl, ImVec2 c, float s)
        { Render::DrawGlyph(dl, c, s, g, col, Render::GlyphStyle{}); };
    }

    // BeginCard + a CLICKABLE collapse header (chevron + 20px gold title). Returns true when expanded (the caller
    // then builds tiles + Tiles()/EndCard); returns false when collapsed -- the card is already closed, so the
    // caller returns immediately, freeing the vertical space for the instance list below (vital at the min window
    // height, where a full completion grid otherwise leaves the list one line tall). Collapse state persists.
    std::set<std::string> g_trackerCollapsed;
    bool CardTitle(const char *id, const char *title)
    {
        Gw2Ui::BeginCard(id);
        const bool collapsed = g_trackerCollapsed.count(id) != 0;
        const float w = Gw2Ui::CardInnerWidth(), hh = 26.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        char btn[48];
        std::snprintf(btn, sizeof(btn), "%s_hdr", id);
        if (ImGui::InvisibleButton(btn, ImVec2(w, hh)))
        {
            if (collapsed)
                g_trackerCollapsed.erase(id);
            else
                g_trackerCollapsed.insert(id);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        Render::DrawGlyph(dl, ImVec2(p.x + 9.f, p.y + hh * 0.5f), 13.f,
                          collapsed ? Render::Glyph::CaretRight : Render::Glyph::CaretDown, Gw2Ui::kGold, Render::GlyphStyle{});
        Gw2Ui::LabelIn(ImVec2(p.x + 24.f, p.y), ImVec2(p.x + w, p.y + hh), title,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, 20.f);
        if (collapsed)
        {
            Gw2Ui::EndCard();
            return false;
        }
        ImGui::Spacing();
        return true;
    }
    void Tiles(std::vector<Gw2Ui::Stat> &tiles)
    {
        Gw2Ui::StatGrid(Gw2Ui::CardInnerWidth(), tiles);
        Gw2Ui::EndCard();
    }

    // The live daily/weekly Wizard's Vault objectives whose title mentions `keyword` -> a stat tile each (the system
    // that replaced the old daily-achievement endpoint). From AccountData (disk-cached + TTL-refreshed).
    std::vector<Gw2Ui::Stat> WvTiles(const char *keyword)
    {
        std::vector<Gw2Ui::Stat> out;
        const AccountData::Model &m = AccountData::Get();
        auto add = [&](const std::vector<AccountData::WvObjective> &objs, const char *track)
        {
            for (const AccountData::WvObjective &o : objs)
            {
                if (Lower(o.title).find(keyword) == std::string::npos)
                    continue;
                const bool done = (o.max > 0 && o.cur >= o.max);
                char v[24];
                std::snprintf(v, sizeof(v), "%d / %d", o.cur, o.max);
                // Drop the redundant trailing location phrase ("... in the Fractals of the Mists") so the tile
                // label stays concise; the scope (Fractals / Raids / ...) is already the tab section.
                std::string title = o.title;
                const size_t inp = Lower(title).find(" in the ");
                if (inp != std::string::npos)
                    title.erase(inp);
                char lbl[96];
                std::snprintf(lbl, sizeof(lbl), "%s (%s)", title.c_str(), track);
                out.push_back({lbl, v, done ? kGreen : Gw2Ui::kTextSelected, done ? kGreen : Gw2Ui::kGold,
                               GIcon(done ? Render::Glyph::Check : Render::Glyph::Clock, done ? kGreen : Gw2Ui::kGold)});
            }
        };
        add(m.wvDaily, "daily");
        add(m.wvWeekly, "weekly");
        return out;
    }

    void DrawDungeonTracker(App &app)
    {
        if (!CardTitle("##dgntrk", "Dungeons"))
            return;
        const AccountData::Model &m = AccountData::Get();
        std::vector<Gw2Ui::Stat> tiles;
        // Per-dungeon daily explorable-path completion (live: /v2/account/dungeons -> the path ids done today).
        if (!app.content.Dungeons().empty() && (m.haveDungeons || AccountData::HasKey()))
        {
            int doneTot = 0, total = 0;
            std::vector<Gw2Ui::Stat> dgn;
            for (const ContentData::Dungeon &d : app.content.Dungeons())
            {
                int dn = 0, dt = 0;
                for (const ContentData::DungeonPath &p : d.paths)
                {
                    if (p.type != "Explorable")
                        continue;
                    ++dt;
                    if (m.dungeonPathsToday.count(p.id))
                        ++dn;
                }
                if (dt == 0)
                    continue;
                doneTot += dn;
                total += dt;
                const bool full = (dn == dt && dn > 0);
                char v[16];
                std::snprintf(v, sizeof(v), "%d / %d", dn, dt);
                dgn.push_back({PrettyId(d.id), v, full ? kGreen : Gw2Ui::kTextSelected, full ? kGreen : Gw2Ui::kGold, GIcon(Render::Glyph::Dungeon, full ? kGreen : Gw2Ui::kGold)});
            }
            char sv[16];
            std::snprintf(sv, sizeof(sv), "%d / %d", doneTot, total);
            tiles.push_back({"Paths today", sv, (doneTot >= total && total > 0) ? kGreen : Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Check)});
            tiles.insert(tiles.end(), dgn.begin(), dgn.end());
        }
        std::vector<Gw2Ui::Stat> wv = WvTiles("dungeon");
        tiles.insert(tiles.end(), wv.begin(), wv.end());
        if (!AccountData::HasKey())
            tiles.push_back({"Daily", "Add an API key", Gw2Ui::kTextDim, Gw2Ui::kGold, GIcon(Render::Glyph::Dungeon)});
        Tiles(tiles);
    }

    void DrawFractalTracker(App &)
    {
        const AccountData::Model &m = AccountData::Get();
        if (!CardTitle("##frctrk", "Fractals of the Mists"))
            return;
        std::vector<Gw2Ui::Stat> tiles;
        char lvl[16];
        std::snprintf(lvl, sizeof(lvl), "%d / 100", m.fractalLevel);
        tiles.push_back({"Personal reward level", m.haveAccount ? lvl : (AccountData::HasKey() ? "..." : "--"),
                         Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Marker)});
        std::vector<Gw2Ui::Stat> wv = WvTiles("fractal");
        tiles.insert(tiles.end(), wv.begin(), wv.end());
        Tiles(tiles);
    }

    void DrawRaidTracker(App &)
    {
        const AccountData::Model &m = AccountData::Get();
        if (!CardTitle("##raidtrk", "Raids"))
            return;
        std::vector<Gw2Ui::Stat> tiles;
        if (m.haveRaids)
        {
            char v[16];
            std::snprintf(v, sizeof(v), "%d", m.raidsCleared);
            tiles.push_back({"Cleared this week", v, Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Check)});
        }
        const int64_t li = WalletVal(70), mag = WalletVal(28);
        if (li >= 0)
        {
            char v[24];
            std::snprintf(v, sizeof(v), "%lld", (long long)li);
            tiles.push_back({"Legendary Insights", v, Gw2Ui::kTextSelected, IM_COL32(170, 140, 210, 255), {}});
        }
        if (mag >= 0)
        {
            char v[24];
            std::snprintf(v, sizeof(v), "%lld", (long long)mag);
            tiles.push_back({"Magnetite Shards", v, Gw2Ui::kTextSelected, IM_COL32(150, 160, 170, 255), {}});
        }
        std::vector<Gw2Ui::Stat> wv = WvTiles("raid");
        tiles.insert(tiles.end(), wv.begin(), wv.end());
        if (tiles.empty())
            tiles.push_back({"This week", "Add an API key", Gw2Ui::kTextDim, Gw2Ui::kGold, {}});
        Tiles(tiles);
    }

    void DrawStrikeTracker(App &app)
    {
        int n = 0;
        for (const MapInfo &mi : app.mapIndex)
            if (mi.Kind == "Strike")
                ++n;
        if (!CardTitle("##strktrk", "Strike Missions"))
            return;
        std::vector<Gw2Ui::Stat> tiles;
        char v[16];
        std::snprintf(v, sizeof(v), "%d", n);
        tiles.push_back({"Strike missions", v, Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Marker)});
        std::vector<Gw2Ui::Stat> wv = WvTiles("strike");
        tiles.insert(tiles.end(), wv.begin(), wv.end());
        Tiles(tiles);
    }

    void DrawHomeTracker(App &app)
    {
        int n = 0;
        for (const auto &kv : app.instances)
            if (kv.second.Family == "home")
                ++n;
        if (!CardTitle("##hometrk", "Home instance"))
            return;
        std::vector<Gw2Ui::Stat> tiles;
        char v[16];
        std::snprintf(v, sizeof(v), "%d", n);
        tiles.push_back({"Guided walkthroughs", v, Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Trail)});
        std::vector<Gw2Ui::Stat> wv = WvTiles("home");
        tiles.insert(tiles.end(), wv.begin(), wv.end());
        Tiles(tiles);
    }

    void DrawStoryTracker(App &app)
    {
        app.storyCompletion.RefreshAchievements(false); // throttled; scope-gated internally
        if (!CardTitle("##stytrk", "Story completion"))
            return;
        const auto &byRel = app.stories.ByRelease();
        if (byRel.empty())
        {
            Gw2Ui::Label("No story spine loaded.", Gw2Ui::kTextSub, false, nullptr, 16.f);
            Gw2Ui::EndCard();
            return;
        }
        if (!app.storyCompletion.AutoCheckScoped())
        {
            Gw2Ui::Label("Add an API key (account + progression) for auto-completion; otherwise tick episodes in the Journal.", Gw2Ui::kTextSub, false, nullptr, 14.f);
            ImGui::Spacing();
        }
        std::vector<Gw2Ui::Stat> tiles;
        int relsDone = 0, relsTotal = 0, epsDone = 0, epsTotal = 0;
        for (const std::string &rel : StoryData::ReleaseOrder())
        {
            const std::vector<StoryEpisode> *eps = app.stories.Episodes(rel);
            if (!eps || eps->empty())
                continue;
            int done = 0;
            for (const StoryEpisode &e : *eps)
                if (app.storyCompletion.EpisodeDone(rel, e))
                    ++done;
            const int tot = (int)eps->size();
            ++relsTotal;
            if (done == tot)
                ++relsDone;
            epsDone += done;
            epsTotal += tot;
            const bool full = (done == tot);
            char v[16];
            std::snprintf(v, sizeof(v), "%d / %d", done, tot);
            tiles.push_back({StoryData::ReleaseName(rel), v, full ? kGreen : Gw2Ui::kTextSelected, full ? kGreen : Gw2Ui::kGold, {}});
        }
        char rv[16];
        std::snprintf(rv, sizeof(rv), "%d / %d", relsDone, relsTotal);
        char ev[16];
        std::snprintf(ev, sizeof(ev), "%d / %d", epsDone, epsTotal);
        tiles.insert(tiles.begin(), {"Episodes done", ev, Gw2Ui::kTextSelected, Gw2Ui::kGold, GIcon(Render::Glyph::Checklist)});
        tiles.insert(tiles.begin(), {"Releases complete", rv, Gw2Ui::kGold, Gw2Ui::kGold, GIcon(Render::Glyph::Book)});
        Tiles(tiles);
    }

    // The expansion accent for a story-instance's release pill -- each campaign's identity colour, so the eye can
    // scan the Story list by expansion. Core/tutorials/unknown fall back to gold.
    ImU32 ReleasePillColor(const std::string &rel)
    {
        if (rel == "hot")
            return IM_COL32(139, 197, 74, 255); // Heart of Thorns  - Maguuma jungle green
        if (rel == "pof")
            return IM_COL32(224, 158, 70, 255); // Path of Fire     - Crystal Desert amber
        if (rel == "lws3")
            return IM_COL32(96, 178, 204, 255); // LWS3
        if (rel == "lws4")
            return IM_COL32(206, 128, 96, 255); // LWS4
        if (rel == "lws5")
            return IM_COL32(140, 190, 224, 255); // Icebrood Saga    - ice blue
        if (rel == "eod")
            return IM_COL32(78, 202, 150, 255); // End of Dragons   - Cantha jade
        if (rel == "soto")
            return IM_COL32(160, 138, 224, 255); // Secrets of the Obscure - astral indigo
        if (rel == "jw")
            return IM_COL32(206, 180, 116, 255); // Janthir Wilds    - bronze
        if (rel == "voe")
            return IM_COL32(122, 204, 200, 255); // Visions of Eternity - Mists teal
        if (rel == "lws1")
            return IM_COL32(196, 108, 108, 255);
        if (rel == "lws2")
            return IM_COL32(196, 156, 96, 255);
        return Gw2Ui::kGold; // core / unknown
    }

    // The guided story/tutorial walkthrough instances (Family typed) -- a reference list; they activate
    // automatically in-world when you enter the story step (not waypoint-travelable). Each row is tagged with a
    // coloured EXPANSION pill (the campaign it belongs to) + its region -- far more useful than a raw point count.
    struct StoryRow
    {
        std::string name, region, release;
    };
    std::vector<StoryRow> g_storyRows;
    size_t g_storyBuilt = (size_t)-1;
    void DrawStoryList(App &app)
    {
        if (g_storyBuilt != app.instances.size())
        {
            g_storyRows.clear();
            for (const auto &kv : app.instances)
            {
                const Instance &in = kv.second;
                if (in.Family != "story" && in.Family != "tutorial")
                    continue;
                g_storyRows.push_back({in.Name, in.RegionName, in.Release});
            }
            // Group by expansion (release order), then A-Z within -> the list reads campaign-by-campaign.
            std::sort(g_storyRows.begin(), g_storyRows.end(), [](const StoryRow &a, const StoryRow &b)
                      {
                auto ord = [](const std::string& r){ const auto& o = StoryData::ReleaseOrder();
                    for (size_t k = 0; k < o.size(); ++k) if (o[k] == r) return (int)k; return 99; };
                const int ra = ord(a.release), rb = ord(b.release);
                return ra != rb ? ra < rb : a.name < b.name; });
            g_storyBuilt = app.instances.size();
        }
        char h[96];
        std::snprintf(h, sizeof(h), "%d story instances have a guided walkthrough -- it activates when you enter the step:", (int)g_storyRows.size());
        Gw2Ui::Label(h, Gw2Ui::kTextSub, false, nullptr, 18.f);
        ImGui::Spacing();
        ImGui::BeginChild("##storylist", ImVec2(0.f, 0.f), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f)); // contiguous rows (full-row highlight, no gaps)
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float rowH = 44.f, pfs = 14.f, rightPad = 12.f, gap = 12.f;
        ImGuiListClipper clip;
        clip.Begin((int)g_storyRows.size(), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const StoryRow &r = g_storyRows[i];
                ImGui::PushID(i);
                const Gw2Ui::RowHotspot row = Gw2Ui::Row("##srow", i, rowH, 0.f, /*allowOverlap*/ true);
                const std::string relName = r.release.empty() ? std::string() : StoryData::ReleaseName(r.release);
                const float pillW = relName.empty() ? 0.f : Gw2Ui::PillWidth(relName.c_str(), pfs, 18.f);
                const float rInset = rightPad + pillW + (pillW > 0.f ? gap : 0.f); // where the region text stops
                Gw2Ui::RowLabel(dl, row, 14.f, rInset + 264.f, r.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, false, nullptr, 20.f);
                if (!r.region.empty()) // region: dim, right-aligned in a 260px box ending just left of the pill
                    Gw2Ui::RowLabel(dl, row, row.width - rInset - 260.f, rInset, r.region.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 16.f);
                if (pillW > 0.f) // the coloured expansion pill (outlined, campaign accent)
                {
                    const float pillH = Gw2Ui::PillHeight(pfs);
                    const ImU32 accent = ReleasePillColor(r.release);
                    const ImVec2 tl(row.min.x + row.width - rightPad - pillW, row.min.y + (row.height - pillH) * 0.5f);
                    Gw2Ui::PillAt(dl, tl, relName.c_str(), pfs, accent, accent, IM_COL32(255, 255, 255, 12), 18.f);
                }
                ItemUI::DrawCopyMenu("##storycopy", row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), r.name, ""); // right-click: copy name / open wiki (story steps have no waypoint)
                ImGui::PopID();
            }
        ImGui::PopStyleVar(); // ItemSpacing
        ImGui::EndChild();
    }

    // One hoverable, click-to-travel row per indexed place of `kind` -- the same hover map + waypoint travel the Atlas
    // uses (a clipper over the pre-collected matches, exactly like the Atlas list, so the full-row highlight matches).
    void DrawContentList(App &app, const char *kind, const char *emptyMsg)
    {
        std::vector<const MapInfo *> items;
        for (const MapInfo &mi : app.mapIndex)
            if (mi.Kind == kind)
                items.push_back(&mi);
        ImGui::BeginChild("##contentlist", ImVec2(0.f, 0.f), false);
        if (items.empty())
        {
            Gw2Ui::Label(emptyMsg, Gw2Ui::kTextDim, false, nullptr, 16.f);
            ImGui::EndChild();
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f)); // contiguous rows (full-row highlight, no gaps)
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float rowH = 44.f;
        ImGuiListClipper clip;
        clip.Begin((int)items.size(), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const MapInfo &mi = *items[i];
                ImGui::PushID(i);
                const Gw2Ui::RowHotspot row = Gw2Ui::Row("##crow", i, rowH, 0.f, /*allowOverlap*/ true);
                Gw2Ui::RowLabel(dl, row, 14.f, 240.f, mi.Name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, false, nullptr, 20.f);
                std::string sub = mi.RegionName;
                if (mi.MinLevel > 0)
                    sub += (sub.empty() ? "" : "   -   ") + std::string("Level ") + std::to_string(mi.MinLevel) +
                           (mi.MaxLevel > mi.MinLevel ? ("-" + std::to_string(mi.MaxLevel)) : "");
                if (!sub.empty())
                    Gw2Ui::RowLabel(dl, row, row.width - 360.f, 32.f, sub.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 16.f); // wide box so long regions ("Shiverpeak Mountains") don't left-clip
                if (row.hovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const bool withMap = mi.HasRect || !mi.StaticMap.empty();
                    GroupTooltip(mi.Name.c_str(), mi.MinLevel, mi.MaxLevel, 0, 0, mi.ContinentId, mi.ContRect, nullptr, 0,
                                 withMap, mi.RegionName.empty() ? nullptr : mi.RegionName.c_str(), mi.TileMaxZoom,
                                 mi.StaticMap.empty() ? nullptr : mi.StaticMap.c_str());
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        if (mi.ChatLink.empty())
                            OpenZoneTravelChoice(app, mi.MapId);
                        else
                            TravelToLocation(app, "waypoint", mi.MapId, mi.Cx, mi.Cy, mi.Name, mi.ChatLink);
                    }
                }
                ItemUI::DrawCopyMenu("##cntcopy", row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), mi.Name, mi.ChatLink); // right-click: copy name / waypoint / wiki
                ImGui::PopID();
            }
        ImGui::PopStyleVar(); // ItemSpacing
        ImGui::EndChild();
    }

    // ---- shared builders for the "today's featured" CARD GRIDS (fractals + strikes): each card shows the encounter's
    // bundled MAP render (REUSED from maps.pack via ImageCache::GetMapImage -- never re-packed) + name + a caption,
    // under a centered filigree heading. Raids keep the checklist tree (boss PORTRAITS via portraits.pack). ----

    struct CardItem
    {
        const Texture_t *tex = nullptr;
        std::string name, caption;
    };

    // Canonical portraits.pack key for a strike (lowercase, non-alnum -> single '_') -- MUST match build_content_images.slug.
    std::string Slug(const std::string &s)
    {
        std::string o;
        bool und = false;
        for (char c : s)
        {
            if (std::isalnum((unsigned char)c))
            {
                o += (char)std::tolower((unsigned char)c);
                und = false;
            }
            else if (!und && !o.empty())
            {
                o += '_';
                und = true;
            }
        }
        while (!o.empty() && o.back() == '_')
            o.pop_back();
        return o;
    }

    // A fractal/strike rotation name -> its maps.pack slug (the Atlas MapInfo.StaticMap for that Kind+Name). The
    // rotation names match MapInfo.Name exactly (both wiki-sourced). Empty if not found (the card draws a placeholder).
    std::string MapSlugFor(App &app, const char *kind, const std::string &name)
    {
        for (const MapInfo &mi : app.mapIndex)
            if (mi.Kind == kind && mi.Name == name)
                return mi.StaticMap;
        // Fallback: some fractal MAP names carry a " Fractal" suffix the daily-rotation name omits
        // (rotation "Uncategorized" -> map "Uncategorized Fractal"), so an exact match misses. Try the suffix.
        if (std::string(kind) == "Fractal")
            for (const MapInfo &mi : app.mapIndex)
                if (mi.Kind == kind && mi.Name == name + " Fractal")
                    return mi.StaticMap;
        return std::string();
    }

    // The "today's featured" grid: framework image cards (Gw2Ui::ImageCard inside Gw2Ui::CardGrid -- the card widgets
    // live in the framework now) + a per-card right-click copy menu. Used by the fractal + strike scopes; the dungeon
    // scope calls the framework helpers directly (it adds a path-dropdown body to each card).
    void ContentCardGrid(const std::vector<CardItem> &items, const char *prefix)
    {
        Gw2Ui::CardGrid((int)items.size(), 220.f, 10.f, [&](int i, float cardW)
                        {
            char id[40]; std::snprintf(id, sizeof(id), "##%s%d", prefix, i);
            const bool hov = Gw2Ui::ImageCard(id, items[i].tex, items[i].name.c_str(),
                                              items[i].caption.empty() ? nullptr : items[i].caption.c_str(), cardW);
            char mid[48]; std::snprintf(mid, sizeof(mid), "%s_m", id);
            ItemUI::DrawCopyMenu(mid, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), items[i].name, ""); });
    }

    // Hover tooltip: name (gold) + an optional sub line + a larger bundled portrait (portraits.pack). Mirrors
    // GroupTooltip but shows the entity's picture instead of a map. Null portrait -> text only (graceful).
    void PortraitTooltip(const std::string &name, const std::string &key, const std::string &sub)
    {
        const Texture_t *tex = key.empty() ? nullptr : ImageCache::GetPortrait(key.c_str());
        Gw2Ui::TooltipBegin();
        Gw2Ui::Label(name.c_str(), Gw2Ui::kGold, true, nullptr, 18.f);
        if (!sub.empty())
            Gw2Ui::Label(sub.c_str(), Gw2Ui::kTextSub, false, nullptr, 14.f);
        if (tex && tex->Resource && tex->Width > 0 && tex->Height > 0)
        {
            ImGui::Spacing();
            const float w = 260.f;
            const float h = std::min(320.f, w * (float)tex->Height / (float)tex->Width);
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)tex->Resource, c, ImVec2(c.x + w, c.y + h));
            ImGui::Dummy(ImVec2(w, h));
        }
        Gw2Ui::TooltipEnd();
    }

    std::set<std::string> g_raidExpanded; // raid groups the user COLLAPSED (absent = expanded by default)

    // The Raids scope LIST: a collapsible tree (Gw2Ui::TreeRow + PaintCheckbox, like the Checklist tab) -- a raid
    // GROUP row (group checkbox: full/partial/none from cleared-vs-total) over read-only BOSS leaves (checked =
    // cleared this week, /v2/account/raids -> AccountData.raidsClearedIds), each with a portrait + hover tooltip +
    // right-click copy. Default expanded; collapse state persists in g_raidExpanded.
    void DrawRaidBosses(App &app)
    {
        ImGui::BeginChild("##raidbosses", ImVec2(0.f, 0.f), false);
        if (app.content.Raids().empty())
        {
            Gw2Ui::Label("No raid data bundled.", Gw2Ui::kTextDim, false, nullptr, 16.f);
            ImGui::EndChild();
            return;
        }
        const AccountData::Model &m = AccountData::Get();
        if (!AccountData::HasKey())
        {
            Gw2Ui::Label("Add an API key (progression scope) to see your weekly boss clears.", Gw2Ui::kTextSub, false, nullptr, 16.f);
            ImGui::Spacing();
        }
        const std::set<std::string> cleared(m.raidsClearedIds.begin(), m.raidsClearedIds.end());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        int rowIdx = 0;
        for (const ContentData::Raid &raid : app.content.Raids())
        {
            int cl = 0, tot = 0;
            for (const ContentData::Wing &w : raid.wings)
                for (const std::string &b : w.bosses)
                {
                    ++tot;
                    if (cleared.count(b))
                        ++cl;
                }
            const bool open = g_raidExpanded.count(raid.id) == 0;
            const int gstate = (tot > 0 && cl == tot) ? 1 : (cl > 0 ? 2 : 0);
            const float gh = 40.f, cb = 28.f, cntW = 84.f;
            Gw2Ui::TreeRowResult gr = Gw2Ui::TreeRow(raid.id.c_str(), 0, gh, open ? 1 : 0, false, rowIdx++);
            Gw2Ui::PaintCheckbox(ImVec2(gr.contentMin.x, gr.rowMin.y + (gh - cb) * 0.5f), cb, gstate, false);
            Gw2Ui::LabelIn(ImVec2(gr.contentMin.x + cb + 10.f, gr.rowMin.y), ImVec2(gr.rowMax.x - cntW - 8.f, gr.rowMax.y),
                           PrettyId(raid.id).c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, 22.f);
            char cnt[24];
            std::snprintf(cnt, sizeof(cnt), "%d / %d", cl, tot);
            Gw2Ui::LabelIn(ImVec2(gr.rowMax.x - cntW - 8.f, gr.rowMin.y), ImVec2(gr.rowMax.x - 8.f, gr.rowMax.y), cnt,
                           Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, (gstate == 1) ? kGreen : Gw2Ui::kTextDim, false, nullptr, 18.f);
            ItemUI::DrawCopyMenu(("##rg_" + raid.id).c_str(), gr.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), PrettyId(raid.id), "");
            if (gr.clicked)
            {
                if (open)
                    g_raidExpanded.insert(raid.id);
                else
                    g_raidExpanded.erase(raid.id);
            }
            if (!open)
                continue;
            for (const ContentData::Wing &w : raid.wings)
                for (const std::string &b : w.bosses)
                {
                    const bool done = cleared.count(b) != 0;
                    const float lh = 48.f, lcb = 24.f, isz = 40.f;
                    Gw2Ui::TreeRowResult lr = Gw2Ui::TreeRow(b.c_str(), 1, lh, -1, false, rowIdx++);
                    Gw2Ui::PaintCheckbox(ImVec2(lr.contentMin.x, lr.rowMin.y + (lh - lcb) * 0.5f), lcb, done ? 1 : 0, false);
                    const float px = lr.contentMin.x + lcb + 10.f;
                    const Texture_t *tex = ImageCache::GetPortrait(b.c_str());
                    const bool haveTex = tex && tex->Resource && tex->Width > 0 && tex->Height > 0;
                    if (haveTex) // a bigger portrait, cover-cropped to a square (no squish; bias up to the head/torso) + rounded
                    {
                        const ImVec2 ip(px, lr.rowMin.y + (lh - isz) * 0.5f);
                        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
                        const float ia = (float)tex->Width / (float)tex->Height;
                        if (ia > 1.f)
                        {
                            const float c = (1.f - 1.f / ia) * 0.5f;
                            uv0.x = c;
                            uv1.x = 1.f - c;
                        }
                        else if (ia < 1.f)
                        {
                            uv0.y = (1.f - ia) * 0.25f;
                            uv1.y = uv0.y + ia;
                        }
                        dl->AddImageRounded((ImTextureID)tex->Resource, ip, ImVec2(ip.x + isz, ip.y + isz), uv0, uv1, IM_COL32_WHITE, 4.f);
                        dl->AddRect(ip, ImVec2(ip.x + isz, ip.y + isz), IM_COL32(120, 110, 88, 90), 4.f);
                    }
                    const std::string nm = PrettyId(b);
                    const float nameX = px + (haveTex ? isz + 12.f : 0.f), wingW = 168.f;
                    Gw2Ui::LabelIn(ImVec2(nameX, lr.rowMin.y), ImVec2(lr.rowMax.x - wingW - 8.f, lr.rowMax.y),
                                   nm.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, done ? kGreen : Gw2Ui::kTextSelected, false, nullptr, 18.f);
                    Gw2Ui::LabelIn(ImVec2(lr.rowMax.x - wingW - 8.f, lr.rowMin.y), ImVec2(lr.rowMax.x - 8.f, lr.rowMax.y),
                                   PrettyId(w.id).c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
                    if (lr.hovered)
                        PortraitTooltip(nm, b, PrettyId(raid.id) + "  -  " + PrettyId(w.id));
                    ItemUI::DrawCopyMenu(("##rb_" + b).c_str(), lr.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), nm, "");
                }
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    // The Fractals scope: today's Recommended + Daily fractals as filigree-headed CARD GRIDS, each card the fractal's
    // bundled MAP render (maps.pack). Computed locally (data/rotations.json -> ContentData day-of-year math; no wiki call).
    void DrawFractalToday(App &app)
    {
        ImGui::BeginChild("##fractoday", ImVec2(0.f, 0.f), false);
        const std::vector<ContentData::RecFractal> rec = app.content.TodaysRecommendedFractals();
        const std::vector<std::string> daily = app.content.TodaysDailyFractals();
        if (rec.empty() && daily.empty())
        {
            Gw2Ui::Label("No fractal rotation data bundled.", Gw2Ui::kTextDim, false, nullptr, 16.f);
            ImGui::EndChild();
            return;
        }
        const float availW = ImGui::GetContentRegionAvail().x;
        if (!rec.empty())
        {
            Gw2Ui::FiligreeHeading("Recommended Fractals", availW);
            std::vector<CardItem> items;
            for (const ContentData::RecFractal &rf : rec)
            {
                const std::string nm = rf.name.empty() ? ("Scale " + std::to_string(rf.scale)) : rf.name;
                items.push_back({ImageCache::GetMapImage(MapSlugFor(app, "Fractal", nm).c_str()), nm, "Recommended"});
            }
            ContentCardGrid(items, "frec");
        }
        if (!daily.empty())
        {
            Gw2Ui::FiligreeHeading("Daily Fractals", availW);
            std::vector<CardItem> items;
            for (const std::string &f : daily)
                items.push_back({ImageCache::GetMapImage(MapSlugFor(app, "Fractal", f).c_str()), f, "Daily"});
            ContentCardGrid(items, "fday");
        }
        ImGui::EndChild();
    }

    // The Strikes scope: today's 3 priority strikes (the wiki mod-6/5/2 rotation) + the weekly Bjora strike, as
    // filigree-headed CARD GRIDS, each card the strike's bundled MAP render (maps.pack). Computed locally (no wiki call).
    void DrawStrikeToday(App &app)
    {
        ImGui::BeginChild("##strktoday", ImVec2(0.f, 0.f), false);
        const std::vector<std::string> strikes = app.content.TodaysPriorityStrikes();
        const std::string bjora = app.content.ThisWeeksBjoraStrike();
        if (strikes.empty() && bjora.empty())
        {
            Gw2Ui::Label("No strike rotation data bundled.", Gw2Ui::kTextDim, false, nullptr, 16.f);
            ImGui::EndChild();
            return;
        }
        const float availW = ImGui::GetContentRegionAvail().x;
        if (!bjora.empty()) // the weekly Bjora strike sits ON TOP, a single ~half-width centred card (not full page)
        {
            Gw2Ui::FiligreeHeading("This Week in Bjora Marches", availW);
            const float bw = (std::max)(260.f, (std::min)(420.f, availW * 0.375f));
            const float indent = (availW - bw) * 0.5f;
            if (indent > 0.f)
                ImGui::Indent(indent);
            const bool bhov = Gw2Ui::ImageCard("##bjor0", ImageCache::GetPortrait(Slug(bjora).c_str()), bjora.c_str(), "Weekly rotation", bw);
            ItemUI::DrawCopyMenu("##bjor0_m", bhov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), bjora, "");
            if (indent > 0.f)
                ImGui::Unindent(indent);
        }
        if (!strikes.empty()) // the three daily priority strikes under it
        {
            Gw2Ui::FiligreeHeading("Daily Priority Strikes", availW);
            static const char *grp[] = {"Icebrood Saga", "End of Dragons", "Secrets of the Obscure"};
            std::vector<CardItem> items;
            for (size_t k = 0; k < strikes.size(); ++k)
                items.push_back({ImageCache::GetPortrait(Slug(strikes[k]).c_str()), strikes[k], k < 3 ? grp[k] : ""});
            ContentCardGrid(items, "strk");
        }
        ImGui::EndChild();
    }
}

static int g_contentDrawnFrame = -1000; // last frame the Content tab drew -> gates ContentNeededDomains' interest

// The Content tab's scope-chip selection (0 Dungeons / 1 Story / 2 Fractals / 3 Raids / 4 Strikes / 5 Home).
// File-scope (was a Draw-local static) so OpenDungeonsTab can jump straight to a scope from the tray menu.
namespace
{
    int g_dgnScope = 0;
}

// Share the pane between a tracker and its instance list: the tracker is sized to its OWN content but CAPPED so
// the list always keeps a usable minimum -- if the tracker is taller than the cap (small window), it scrolls
// instead of crushing the list to one line. Immediate-mode shrink-to-fit-then-cap: we draw the tracker in a child
// whose height is LAST frame's measured natural height (clamped to the cap), and re-measure it each frame. Stable
// after one frame; collapsing/expanding or data changes just re-settle on the next frame. `scope` keys the cache.
static void TrackerThenList(int scope, App &app, void (*tracker)(App &), void (*list)(App &))
{
    static float s_trkH[6] = {0, 0, 0, 0, 0, 0};
    const int s = (scope >= 0 && scope < 6) ? scope : 0;
    const float avail = ImGui::GetContentRegionAvail().y;
    const float listMin = 200.f; // always leave room for ~4-5 list rows + header
    const float cap = (std::max)(80.f, avail - listMin - 8.f);
    const float boxH = (s_trkH[s] > 1.f) ? (std::min)(s_trkH[s], cap) : cap;
    char cid[16];
    std::snprintf(cid, sizeof(cid), "##trkbox%d", s);
    ImGui::BeginChild(cid, ImVec2(0.f, boxH), false);
    const float y0 = ImGui::GetCursorScreenPos().y;
    tracker(app);
    s_trkH[s] = ImGui::GetCursorScreenPos().y - y0; // natural height -> next frame sizes the box to it (capped)
    ImGui::EndChild();
    ImGui::Spacing();
    list(app);
}

// The Content tab (grown from the Dungeons tab): scope chips -> each section's live progress-tracker card + its
// instances/places. Dungeons keep the rich path picker + guide; Story lists the guided walkthroughs (by Family
// type); Fractals/Raids/Strikes/Home are reference + travel. Trackers read live account data (no faked rotations).
void DrawDungeonsContent(App &app)
{
    g_contentDrawnFrame = ImGui::GetFrameCount(); // mark visible -> ContentNeededDomains refreshes our account domains
    static const Gw2Ui::ChipItem kScopes[] = {
        {"Dungeons"}, {"Story"}, {"Fractals"}, {"Raids"}, {"Strikes"}, {"Home"}};
    Gw2Ui::ChipRow("##cscope", kScopes, 6, &g_dgnScope, 28.f, 16.f, 6.f, 70.f, true);
    ImGui::Spacing();
    switch (g_dgnScope)
    {
    case 0:
        TrackerThenList(0, app, DrawDungeonTracker, DrawDungeonsBody);
        break;
    case 1:
        TrackerThenList(1, app, DrawStoryTracker, DrawStoryList);
        break;
    case 2:
        TrackerThenList(2, app, DrawFractalTracker, DrawFractalToday);
        break;
    case 3:
        TrackerThenList(3, app, DrawRaidTracker, DrawRaidBosses);
        break;
    case 4:
        TrackerThenList(4, app, DrawStrikeTracker, DrawStrikeToday);
        break;
    case 5:
        TrackerThenList(5, app, DrawHomeTracker, [](App &a)
                        { DrawContentList(a, "Home", "No home-instance data."); });
        break;
    }
}

// Open the Content (Dungeons) tab, optionally jumping straight to a scope (scope < 0 leaves the current one).
void OpenDungeonsTab(App &app, int scope)
{
    if (scope >= 0)
        g_dgnScope = scope;
    OpenSettingsTab(app, SettingsTabDungeons);
}

// The account domains the Content tab's trackers read -- folded into the central per-frame AccountData::Tick mask
// (entry.cpp) only while the tab is on screen, so raids / Wizard's Vault / wallet / account refresh on their TTL
// exactly like the dashboard widgets. (DomWallet/DomAccount are warmed/ticked elsewhere too; harmless to include.)
unsigned ContentNeededDomains(App &)
{
    if (ImGui::GetFrameCount() - g_contentDrawnFrame > 2)
        return 0u; // not currently visible
    return AccountData::DomRaids | AccountData::DomDungeons | AccountData::DomWizardVault | AccountData::DomWallet | AccountData::DomAccount;
}
