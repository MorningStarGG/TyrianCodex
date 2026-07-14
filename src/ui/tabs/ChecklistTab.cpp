#include "ui/tabs/ChecklistTab.h"
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
#include "ui/ApiReminder.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include "render/LockIcon.h" // Render::DrawLock (shared travel padlock)
#include "util/ImageCache.h"
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
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
#include "ui/tabs/MapThumbnail.h"

// Checklist tab. A hierarchical Release > Region > Zone > Type > objective tree, built fresh from app.zones each frame on the
// reusable Gw2Ui tree primitives (expand state persists in g_chkExpanded). Every GROUP row shows a
// (done/total) count + a tri-state check-all box (clicking marks/clears every descendant); LEAF rows show a
// done box, the GW2 type icon, the name, a dim type word, and - for WAYPOINTS - a travel LOCK. Ticking or
// locking a waypoint confirms it for travel (gold lock); a confirmed waypoint can't be unchecked until its
// lock is clicked open.
static std::set<std::string> g_chkExpanded; // open node paths

static char g_chkSearch[64] = "";

// "Current zone" button: the zone node path queued to scroll to (DrawChkNode fires SetScrollHereY once the row
// is laid out, then clears this). Empty = nothing queued.
static std::string g_chkScrollToZone;

static const char *ChkTypeWord(const std::string &t)
{
    if (t == "waypoint")
        return "Waypoint";
    if (t == "poi")
        return "POI";
    if (t == "vista")
        return "Vista";
    if (t == "heart")
        return "Heart";
    if (t == "hero")
        return "Hero Point";
    return "";
}

// The per-row travel padlock now lives centrally (Render::DrawLock) so the Info Panel + anywhere else showing a
// waypoint's locked state reuses the same textures/tints instead of a private copy.
static void PaintLock(ImVec2 p, float s, bool closed) { Render::DrawLock(ImGui::GetWindowDrawList(), p, s, closed); }

// ---- Cached checklist hierarchy. The structure (Release > Region > Zone > Type > objective) is STATIC - it
// only changes when the bundled zone set changes - so it is built ONCE, not rebuilt every frame. Per frame we
// only recompute each node's (done/total) from the progress store (a cheap O(steps) pass) and render. ----
struct ChkNode
{
    std::string path, title; // path = expand-state key + the row's ImGui id
    float height = 30.f;
    bool bold = false, leaf = false;
    std::string stepId, name, type; // leaf only (copied so it never dangles on a route Activate())
    // Map-thumbnail hover tooltip (zone nodes = that zone's rect; region nodes = the UNION rect of their zones).
    bool hasMap = false;
    int mapCont = 1, mapFloor = 1, lvlLo = 0, lvlHi = 0;
    float mapRect[4] = {};       // continent left, top, right, bottom
    std::vector<MapMark> objPts; // every objective under this node (continent coord + type icon) -> hover pins all
    std::vector<ChkNode> kids;
    int done = 0, total = 0; // recomputed per frame
};

static std::vector<ChkNode> g_chkRoots;

static size_t g_chkBuiltFor = (size_t)-1;

struct ChkHit
{
    std::string stepId, name, type, zone;
};

static std::vector<ChkHit> g_chkHits;

static std::string g_chkHitsFor = "\x01"; // sentinel so the first search build always runs

// Fold a zone's continent rect into a node's map crop: a single call for a zone node, or once per zone for a
// region node (the union rect). Continent-1 only (the only continent the tile thumbnail supports).
static void ChkAccumMap(ChkNode &n, const Zone *z, bool wantPts = true)
{
    if (!z->HasRects || z->ContinentId != 1)
        return;
    // ZONE node: crop to the playable area (sectorRect); REGION node (wantPts=false): union of full continent rects.
    float zr[4];
    if (wantPts)
        ZoneMapCrop(z, zr);
    else
    {
        zr[0] = z->ContRect[0];
        zr[1] = z->ContRect[1];
        zr[2] = z->ContRect[2];
        zr[3] = z->ContRect[3];
    }
    if (!n.hasMap)
    {
        n.hasMap = true;
        n.mapCont = z->ContinentId;
        n.mapFloor = z->Floor;
        n.mapRect[0] = zr[0];
        n.mapRect[1] = zr[1];
        n.mapRect[2] = zr[2];
        n.mapRect[3] = zr[3];
        n.lvlLo = z->MinLevel;
        n.lvlHi = z->MaxLevel;
    }
    else
    {
        n.mapRect[0] = std::min(n.mapRect[0], zr[0]);
        n.mapRect[1] = std::min(n.mapRect[1], zr[1]);
        n.mapRect[2] = std::max(n.mapRect[2], zr[2]);
        n.mapRect[3] = std::max(n.mapRect[3], zr[3]);
        n.lvlLo = std::min(n.lvlLo, z->MinLevel);
        n.lvlHi = std::max(n.lvlHi, z->MaxLevel);
    }
    if (!wantPts)
        return;                                          // region nodes show the map only -- too many objectives to pin
    n.objPts.reserve(n.objPts.size() + z->Steps.size()); // ZONE nodes pin every objective (with its type icon)
    for (const Step &s : z->Steps)
    {
        uint32_t iid = 0;
        Objective::TryIconAssetId(s.Type, iid);
        n.objPts.push_back(MapMark{s.CX, s.CY, iid});
    }
}

static void BuildChecklistTree(App &app)
{
    g_chkRoots.clear();

    // A region's hover map = its FULL geographic extent (ALL zones with that regionId, across every release),
    // so a header named "Kryta" always shows the whole Kryta region. Without this, the LWS1 "Kryta" subgroup -
    // which contains only Southsun Cove (an API-Kryta zone from a later release) - would show just Southsun's
    // map under the "Kryta" name. Built once here, keyed by regionId.
    std::map<int, ChkNode> regionFull;
    for (const auto &kv : app.zones)
        if (!kv.second.Steps.empty())
            ChkAccumMap(regionFull[kv.second.RegionId], &kv.second, /*wantPts*/ false); // region = map only, no pins

    std::map<std::string, std::vector<const Zone *>> byRel;
    for (const auto &kv : app.zones)
        if (!kv.second.Steps.empty())
            byRel[kv.second.Release.empty() ? std::string("core") : kv.second.Release].push_back(&kv.second);
    std::vector<std::string> relOrder = StoryData::ReleaseOrder();
    for (const auto &kv : byRel)
        if (std::find(relOrder.begin(), relOrder.end(), kv.first) == relOrder.end())
            relOrder.push_back(kv.first);
    static const char *kTypeOrder[] = {"heart", "vista", "poi", "hero", "waypoint"};

    for (const std::string &rel : relOrder)
    {
        auto rit = byRel.find(rel);
        if (rit == byRel.end())
            continue;
        ChkNode relN;
        relN.path = "rel|" + rel;
        relN.title = StoryData::ReleaseName(rel);
        relN.height = 42.f;
        relN.bold = true;

        std::map<int, std::vector<const Zone *>> byReg;
        for (const Zone *z : rit->second)
            byReg[z->RegionId].push_back(z);
        std::vector<std::pair<int, std::vector<const Zone *>>> regs(byReg.begin(), byReg.end());
        std::sort(regs.begin(), regs.end(), [&](const auto &a, const auto &b)
                  { const int am = Regions::MinLevel(a.second), bm = Regions::MinLevel(b.second); return am != bm ? am < bm : Regions::Label(a.second) < Regions::Label(b.second); });

        for (auto &reg : regs)
        {
            ChkNode regN;
            regN.path = relN.path + "|reg|" + std::to_string(reg.first);
            regN.title = Regions::Label(reg.second);
            regN.height = 38.f;
            { // region map = the FULL region (all releases), so "Kryta" always shows Kryta - not just this group's zones
                const ChkNode &rf = regionFull[reg.first];
                regN.hasMap = rf.hasMap;
                regN.mapCont = rf.mapCont;
                regN.mapFloor = rf.mapFloor;
                regN.lvlLo = rf.lvlLo;
                regN.lvlHi = rf.lvlHi;
                for (int i = 0; i < 4; ++i)
                    regN.mapRect[i] = rf.mapRect[i]; // region node: map only (regN.objPts stays empty -> no pins)
            }
            std::vector<const Zone *> zs = reg.second;
            std::sort(zs.begin(), zs.end(), [](const Zone *a, const Zone *b)
                      { return a->MinLevel != b->MinLevel ? a->MinLevel < b->MinLevel : a->Name < b->Name; });
            for (const Zone *z : zs)
            {
                ChkNode zN;
                zN.path = regN.path + "|z|" + std::to_string(z->MapId);
                zN.title = z->Name;
                zN.height = 38.f;
                ChkAccumMap(zN, z);
                for (const char *ty : kTypeOrder)
                {
                    std::vector<const Step *> leaves;
                    for (const Step &s : z->Steps)
                        if (s.Type == ty)
                            leaves.push_back(&s);
                    if (leaves.empty())
                        continue;
                    std::sort(leaves.begin(), leaves.end(), [](const Step *a, const Step *b)
                              { return a->Order < b->Order; });
                    const Objective::Info *oi = Objective::Get(ty);
                    ChkNode tN;
                    tN.path = zN.path + "|t|" + ty;
                    tN.title = oi ? oi->GroupName : ty;
                    tN.height = 36.f;
                    for (const Step *s : leaves)
                    {
                        ChkNode ln;
                        ln.leaf = true;
                        ln.height = 28.f;
                        ln.path = s->StepId;
                        ln.stepId = s->StepId;
                        ln.name = s->Name.empty() ? s->Type : s->Name;
                        ln.type = s->Type;
                        tN.kids.push_back(std::move(ln));
                    }
                    zN.kids.push_back(std::move(tN));
                }
                if (!zN.kids.empty())
                    regN.kids.push_back(std::move(zN));
            }
            if (!regN.kids.empty())
                relN.kids.push_back(std::move(regN));
        }
        if (!relN.kids.empty())
            g_chkRoots.push_back(std::move(relN));
    }
    g_chkBuiltFor = app.zones.size();
}

// One O(steps) bottom-up pass: fill each node's (done/total) from the completed-id set.
static void ComputeChkCounts(ChkNode &n, const std::set<std::string> &done)
{
    if (n.leaf)
    {
        n.total = 1;
        n.done = done.count(n.stepId) ? 1 : 0;
        return;
    }
    n.done = 0;
    n.total = 0;
    for (ChkNode &k : n.kids)
    {
        ComputeChkCounts(k, done);
        n.done += k.done;
        n.total += k.total;
    }
}

// Group "check all" (on click only): mark/clear every descendant leaf; clearing skips confirmed waypoints.
static void ChkApply(App &app, const ChkNode &n, bool on, const std::string &ch)
{
    if (n.leaf)
    {
        if (on)
        {
            app.progress.SetComplete(ch, n.stepId, true);
            if (n.type == "waypoint")
                app.progress.SetConfirmed(ch, n.stepId, true);
        }
        else if (!(n.type == "waypoint" && app.progress.IsConfirmed(ch, n.stepId)))
            app.progress.SetComplete(ch, n.stepId, false);
        return;
    }
    for (const ChkNode &k : n.kids)
        ChkApply(app, k, on, ch);
}

// One leaf objective row (a tree leaf or a search hit).
static void DrawChkLeaf(App &app, const std::string &stepId, const std::string &name, const std::string &type, int depth,
                        int &rowIdx, const std::string &ch, const std::set<std::string> &done, const std::string &caption)
{
    const float h = 38.f;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (!ImGui::IsRectVisible(ImVec2(availW, h))) // off-screen row: reserve its height, skip the (shader+label) draw
    {
        ImGui::Dummy(ImVec2(availW, h));
        ++rowIdx;
        return;
    }
    const bool comp = done.count(stepId) != 0;
    const bool isWp = (type == "waypoint");
    const bool conf = isWp && app.progress.IsConfirmed(ch, stepId);
    Gw2Ui::TreeRowResult r = Gw2Ui::TreeRow(stepId.c_str(), depth, h, -1, stepId == app.state.manualTarget, rowIdx++);
    const float cb = 30.f;
    Gw2Ui::PaintCheckbox(ImVec2(r.contentMin.x, r.rowMin.y + (h - cb) * 0.5f), cb, comp ? 1 : 0, false);
    float x = r.contentMin.x + cb + 8.f;
    if (uint32_t iid; Objective::TryIconAssetId(type, iid))
        if (const Texture_t *t = Tex::GetAssetTex(iid); t && t->Resource)
        {
            const float is = 28.f;
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)t->Resource, ImVec2(x, r.rowMin.y + (h - is) * 0.5f), ImVec2(x + is, r.rowMin.y + (h + is) * 0.5f));
        }
    x += 38.f;
    const float lockW = isWp ? 32.f : 6.f, typeW = 100.f;
    const float typeX = r.rowMax.x - lockW - typeW;
    std::string disp = name;
    if (!caption.empty())
        disp += "   -   " + caption;
    Gw2Ui::LabelIn(ImVec2(x, r.rowMin.y), ImVec2(typeX - 6.f, r.rowMax.y), disp.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   comp ? IM_COL32(140, 140, 130, 255) : Objective::ColorOf(type, 255), true, nullptr, 20.f);
    Gw2Ui::LabelIn(ImVec2(typeX, r.rowMin.y), ImVec2(typeX + typeW, r.rowMax.y), ChkTypeWord(type), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                   IM_COL32(175, 175, 175, 255), true, nullptr, 18.f);
    if (isWp)
        PaintLock(ImVec2(r.rowMax.x - 30.f, r.rowMin.y + (h - 24.f) * 0.5f), 24.f, conf);
    if (r.hovered) // checklist always shows details + the zone map with this objective circled
    {
        const Zone *z = nullptr;
        if (const Step *st = FindStepById(app, stepId, &z))
            ObjectiveTooltip(name.c_str(), type.c_str(), st, z, st->CX, st->CY, /*withMap*/ true);
    }
    if (r.clicked)
    {
        const float cbX = r.contentMin.x - r.rowMin.x;
        const float lkX = (r.rowMax.x - 32.f) - r.rowMin.x;
        if (r.clickX >= cbX && r.clickX <= cbX + cb)
        {
            if (!comp)
            {
                app.progress.SetComplete(ch, stepId, true);
                if (isWp)
                    app.progress.SetConfirmed(ch, stepId, true);
            }
            else if (!conf)
                app.progress.SetComplete(ch, stepId, false); // a confirmed waypoint is protected
        }
        else if (isWp && r.clickX >= lkX)
            app.progress.SetConfirmed(ch, stepId, !conf);
        else if (!comp)
            app.state.manualTarget = stepId;
    }
}

// Draw a group node + recurse its children when expanded.
static void DrawChkNode(App &app, ChkNode &n, int depth, int &rowIdx, const std::string &ch, const std::set<std::string> &done)
{
    if (n.leaf)
    {
        DrawChkLeaf(app, n.stepId, n.name, n.type, depth, rowIdx, ch, done, std::string());
        return;
    }
    const bool open = g_chkExpanded.count(n.path) != 0;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (!ImGui::IsRectVisible(ImVec2(availW, n.height))) // off-screen row: reserve its height, skip the draw...
    {
        ImGui::Dummy(ImVec2(availW, n.height));
        ++rowIdx;
    }
    else
    {
        const int state = (n.total > 0 && n.done == n.total) ? 1 : (n.done > 0 ? 2 : 0);
        Gw2Ui::TreeRowResult r = Gw2Ui::TreeRow(n.path.c_str(), depth, n.height, open ? 1 : 0, false, rowIdx++);
        const float cb = 30.f;
        Gw2Ui::PaintCheckbox(ImVec2(r.contentMin.x, r.rowMin.y + (n.height - cb) * 0.5f), cb, state, false);
        char cnt[24];
        std::snprintf(cnt, sizeof(cnt), "(%d/%d)", n.done, n.total);
        const float countW = 104.f;
        Gw2Ui::LabelIn(ImVec2(r.contentMin.x + cb + 8.f, r.rowMin.y), ImVec2(r.rowMax.x - countW - 8.f, r.rowMax.y), n.title.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(255, 255, 255, 255), true, nullptr, n.bold ? 24.f : 21.f, 0.f, n.bold ? 1.5f : -1.f);
        Gw2Ui::LabelIn(ImVec2(r.rowMax.x - countW - 8.f, r.rowMin.y), ImVec2(r.rowMax.x - 8.f, r.rowMax.y), cnt,
                       Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(195, 195, 195, 255), true, nullptr, 20.f);
        if (r.clicked)
        {
            const float cbX = r.contentMin.x - r.rowMin.x;
            if (r.clickX >= cbX && r.clickX <= cbX + cb)
                ChkApply(app, n, state != 1, ch);
            else if (open)
                g_chkExpanded.erase(n.path);
            else
                g_chkExpanded.insert(n.path);
        }
        // Region + zone rows show a map on hover: the zone's map, or a region's whole-area map (union of its zones).
        if (n.hasMap && r.hovered)
            GroupTooltip(n.title.c_str(), n.lvlLo, n.lvlHi, n.done, n.total, n.mapCont, n.mapRect,
                         n.objPts.data(), (int)n.objPts.size(), /*withMap*/ true);
    }
    // "Current zone" jump: when this is the queued zone row (its ancestors were just expanded), scroll it into
    // view. Works whether the row drew for real or was culled to a height-only Dummy -- both submit an item, so
    // SetScrollHereY has a valid rect. Fire once.
    if (!g_chkScrollToZone.empty() && n.path == g_chkScrollToZone)
    {
        ImGui::SetScrollHereY(0.2f);
        g_chkScrollToZone.clear();
    }
    // ...but always recurse: a node can be scrolled off-screen while its children are on-screen.
    if (open)
        for (ChkNode &k : n.kids)
            DrawChkNode(app, k, depth + 1, rowIdx, ch, done);
}

// Warm the on-disk tile cache for every map the Checklist can show on hover. REGIONS first: a region row's
// map is the union of its zones' rects, which ComputeTiles resolves at a lower zoom (whole region in <=16
// tiles) -> a DIFFERENT tile grid than any single zone, so the Zones-tab per-zone prefetch never covers it.
// Then the zone rows (so checklist hovers are instant even if the Zones tab was never opened). Tree shape is
// release > region > zone; release nodes carry no map. Idempotent (ImageCache::PrefetchTile dedupes known tiles).
static void PrefetchChkMaps()
{
    for (const ChkNode &rel : g_chkRoots) // pass 1: regions (the cold case)
        for (const ChkNode &reg : rel.kids)
            if (reg.hasMap)
                PrefetchRectTiles(reg.mapCont, reg.mapRect[0], reg.mapRect[1], reg.mapRect[2], reg.mapRect[3]);
    for (const ChkNode &rel : g_chkRoots) // pass 2: zones
        for (const ChkNode &reg : rel.kids)
            for (const ChkNode &z : reg.kids)
                if (z.hasMap)
                    PrefetchRectTiles(z.mapCont, z.mapRect[0], z.mapRect[1], z.mapRect[2], z.mapRect[3]);
}

// Find the guided zone for `mapId` in the built tree (release > region > zone) and return its ancestor path
// chain. Returns false when the map has no guided zone (a city / story instance / dungeon) -- the caller then
// disables the "Current zone" jump. Cheap: the tree is small and this runs only while the tab is open.
static bool ChkFindZonePaths(uint32_t mapId, std::string &relPath, std::string &regPath, std::string &zonePath)
{
    if (!mapId)
        return false;
    const std::string suffix = "|z|" + std::to_string(mapId);
    for (const ChkNode &rel : g_chkRoots)
        for (const ChkNode &reg : rel.kids)
            for (const ChkNode &z : reg.kids)
                if (z.path.size() >= suffix.size() &&
                    z.path.compare(z.path.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    relPath = rel.path;
                    regPath = reg.path;
                    zonePath = z.path;
                    return true;
                }
    return false;
}

void DrawChecklistContent(App &app)
{
    UpdateCurrentChar(app.state);
    const std::string ch = app.state.currentChar;

    // No key -> objectives still show, but level-aware zone recommendations are off; nudge (dismissible).
    if (!app.api.HasKey())
        ApiReminder::Card(app, "checklist", "level-based zone recommendations", /*gated*/ false);

    // The hierarchy is STATIC (built once). The completed-id set + every node's (done/total) are cached and
    // recomputed ONLY when progress actually changes (ProgressStore::Version) or the character / zone set
    // changes - so an idle open tab does no per-frame grouping, sorting, or counting, only drawing.
    static std::set<std::string> s_chkDone;
    static uint64_t s_chkDoneVer = (uint64_t)-1;
    static std::string s_chkDoneChar;
    if (g_chkBuiltFor != app.zones.size())
    {
        BuildChecklistTree(app);
        s_chkDoneVer = (uint64_t)-1;
    }
    // (Region + zone thumbnail tiles are warmed centrally in WarmCaches at login, not per-tab.)
    if (s_chkDoneVer != app.progress.Version() || s_chkDoneChar != ch)
    {
        s_chkDone.clear();
        const std::vector<std::string> &v = app.progress.CompletedIds(ch);
        s_chkDone.insert(v.begin(), v.end());
        // Confirmed (physically-reached / locked) waypoints count as DONE too - so saved-data waypoints show
        // faded + ticked on load, not green until you unlock/uncheck/recheck them.
        const std::vector<std::string> &cf = app.progress.ConfirmedIds(ch);
        s_chkDone.insert(cf.begin(), cf.end());
        for (ChkNode &n : g_chkRoots)
            ComputeChkCounts(n, s_chkDone);
        s_chkDoneVer = app.progress.Version();
        s_chkDoneChar = ch;
    }

    // Help paragraph + search box.
    const float fullW = ImGui::GetContentRegionAvail().x;
    const char *help = "Tick off what you've already done to sync the guide. Reaching (or ticking) a waypoint "
                       "locks it in for travel - a locked waypoint can't be unchecked or cleared; click its gold "
                       "lock to release it.";
    {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        const float hh = Gw2Ui::MeasureWrappedHeight(help, 16.f, fullW);
        Gw2Ui::LabelIn(hp, ImVec2(hp.x + fullW, hp.y + hh), help, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       Gw2Ui::kTextSub, false, nullptr, 16.f, fullW);
        ImGui::Dummy(ImVec2(fullW, hh + 4.f));
    }
    // Search box + a "Current zone" jump: expand to & scroll the tree to the guided zone you're standing in.
    // Disabled off-coverage (a city / story instance / dungeon has no zone node to jump to). SearchBox leaves the
    // cursor BELOW its box (not after it), so the button is positioned absolutely on the row, not via SameLine.
    {
        std::string relP, regP, zoneP;
        const bool haveZone = ChkFindZonePaths(CurrentMapId(), relP, regP, zoneP);
        const ImVec2 rowP = ImGui::GetCursorScreenPos();
        const float btnW = 132.f, gap = 8.f, rh = Gw2Ui::InputBoxHeight();
        const float searchW = std::max(120.f, fullW - btnW - gap);
        Gw2Ui::SearchBox("##chksearch", g_chkSearch, sizeof(g_chkSearch), searchW, "Search objectives...");
        ImGui::SetCursorScreenPos(ImVec2(rowP.x + searchW + gap, rowP.y));
        const bool jump = Gw2Ui::ActionButton("Current zone", btnW, rh, Gw2Ui::ActionButtonVariant::Normal,
                                              haveZone ? "Expand and scroll to the zone you're in" : "Enter a guided zone first",
                                              /*disabled*/ !haveZone);
        ImGui::SetCursorScreenPos(ImVec2(rowP.x, rowP.y + rh)); // next element starts below the whole row
        if (jump && haveZone)
        {
            g_chkSearch[0] = '\0';
            g_chkHitsFor = "\x01"; // leave search mode so the tree renders
            g_chkExpanded.insert(relP);
            g_chkExpanded.insert(regP);
            g_chkExpanded.insert(zoneP);
            g_chkScrollToZone = zoneP; // DrawChkNode scrolls to it once it is laid out
        }
    }
    ImGui::Spacing();

    ImGui::BeginChild("##checklist", ImVec2(0.f, 0.f), false);
    int rowIdx = 0;

    if (g_chkSearch[0])
    {
        // Flat match list (name / zone / region), rebuilt ONLY when the query changes - not every frame.
        if (g_chkHitsFor != g_chkSearch)
        {
            g_chkHits.clear();
            auto lower = [](std::string s)
            { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; };
            const std::string ql = lower(g_chkSearch);
            for (const auto &kv : app.zones)
            {
                if (g_chkHits.size() >= 150)
                    break;
                for (const Step &s : kv.second.Steps)
                {
                    const std::string nm = s.Name.empty() ? s.Type : s.Name;
                    if (lower(nm).find(ql) == std::string::npos && lower(kv.second.Name).find(ql) == std::string::npos && lower(kv.second.RegionName).find(ql) == std::string::npos)
                        continue;
                    if (g_chkHits.size() >= 150)
                        break;
                    g_chkHits.push_back({s.StepId, nm, s.Type, kv.second.Name});
                }
            }
            g_chkHitsFor = g_chkSearch;
        }
        for (const ChkHit &hit : g_chkHits)
            DrawChkLeaf(app, hit.stepId, hit.name, hit.type, 0, rowIdx, ch, s_chkDone, hit.zone);
        if (g_chkHits.empty())
            Gw2Ui::EmptyState("No matches.", "Try a different search.");
        ImGui::EndChild();
        return;
    }

    // Tree mode: render the cached structure (counts were refreshed above only if progress changed).
    for (ChkNode &n : g_chkRoots)
        DrawChkNode(app, n, 0, rowIdx, ch, s_chkDone);
    ImGui::EndChild();
}

// Login warm: build the STATIC checklist tree once + disk-prefetch every zone + region-union map (shared by
// the Zones tab and Checklist). Wraps the formerly-inline WarmCaches body so the shell stays agnostic of the
// checklist's private statics.
void WarmChecklist(App &app)
{
    if (g_chkBuiltFor != app.zones.size())
        BuildChecklistTree(app);
    PrefetchChkMaps();
}
