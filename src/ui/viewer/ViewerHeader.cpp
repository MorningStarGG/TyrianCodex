#include "ui/viewer/ViewerHeader.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/ZoneRow.h"
#include "app/StaticData.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/FarmCategories.h"   // Farm::CategoryMatch (farming header progress)
#include "guide/Regions.h"
#include "guide/StepStatus.h"
#include "util/Draw.h"
#include "util/Textures.h"
#include "util/ImageCache.h"   // loading-screen header art now lives in loadingscreens.pack (self-downloaded)
#include "model/ObjectiveTypes.h"
#include "Shared.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static void DrawCompassRose(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 dim)
{
    (void)col; (void)dim;
    const Texture_t* compass = Tex::GetFileTex("data\\textures\\ui\\Compass.png");
    if (!compass || !compass->Resource) return;

    const float size = r * 2.18f;
    const ImVec2 a(c.x - size * 0.5f, c.y - size * 0.5f);
    const ImVec2 b(c.x + size * 0.5f, c.y + size * 0.5f);
    dl->AddImage((ImTextureID)compass->Resource, a, b, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255, 255, 255, 245));
}

static const Texture_t* ViewerHeaderTexture(App& app, uint32_t mapId)
{
    // Loading-screen art lives in loadingscreens.pack (keyed by the loading_screens.json relPath BASENAME). The
    // pack is downloaded in the background by DataBootstrap, so it can be null until it lands -> banner fallback
    // (viewer-banner.png ships in the core data, so it's always present).
    if (const LoadingScreen* screen = app.loadingScreens.ForMap(mapId))
        if (!screen->RelPath.empty())
        {
            const std::string& rel = screen->RelPath;
            const size_t slash = rel.find_last_of("\\/");
            const std::string key = (slash == std::string::npos) ? rel : rel.substr(slash + 1);
            if (const Texture_t* t = ImageCache::GetLoadingScreen(key.c_str()); t && t->Resource) return t;
        }
    return Tex::GetFileTex("data\\textures\\ui\\viewer-banner.png");
}

// Matches DashUtil::kNarrowWidth (the dashboard's responsive breakpoint); kept local so the viewer layer does
// not depend on a dashboard header. Below this width Side-by-side pills collapse to Stacked.
static constexpr float kZhNarrow = 320.f;

void DrawRichHeroHeaderArt(App& app, uint32_t mapId, const char* title, const char* regionText,
                           const char* levelText, const char* progressText, float fs,
                           const ZoneHeaderOpts& o, float frac, bool hasFrac)
{
    const float vs = ViewerScale(app);
    const float availW = (o.width > 0.f) ? o.width : ImGui::GetContentRegionAvail().x;
    const float h = ((availW >= 680.f) ? 126.f : 106.f) * vs;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 b(p.x + availW, p.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (const Texture_t* t = ViewerHeaderTexture(app, mapId); t && t->Resource)
    {
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
        if (t->Width > 0 && t->Height > 0)
        {
            const float imgAspect = (float)t->Width / (float)t->Height;
            const float dstAspect = availW / h;
            if (dstAspect > imgAspect)
            {
                const float visibleY = std::clamp(imgAspect / dstAspect, 0.05f, 1.f);
                uv0.y = (1.f - visibleY) * 0.5f;
                uv1.y = 1.f - uv0.y;
            }
            else
            {
                const float visibleX = std::clamp(dstAspect / imgAspect, 0.05f, 1.f);
                uv0.x = (1.f - visibleX) * 0.5f;
                uv1.x = 1.f - uv0.x;
            }
        }
        dl->AddImage((ImTextureID)t->Resource, p, b, uv0, uv1);
    }
    else
    {
        dl->AddRectFilledMultiColor(p, b, IM_COL32(20, 25, 22, 255), IM_COL32(48, 39, 25, 255),
                                   IM_COL32(7, 9, 8, 255), IM_COL32(14, 13, 10, 255));
    }

    dl->AddRectFilledMultiColor(p, b, IM_COL32(0, 0, 0, 178), IM_COL32(0, 0, 0, 98),
                               IM_COL32(0, 0, 0, 220), IM_COL32(0, 0, 0, 220));
    dl->AddRectFilledMultiColor(p, ImVec2(b.x, p.y + h * 0.34f),
                               IM_COL32(0, 0, 0, 70), IM_COL32(0, 0, 0, 12),
                               IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 55));
    dl->AddRect(p, b, IM_COL32(170, 132, 66, 148), 2.f, 0, 1.f);
    dl->AddRect(ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(b.x - 2.f, b.y - 2.f), IM_COL32(255, 223, 150, 38), 1.f, 0, 1.f);
    dl->AddLine(ImVec2(p.x + 1.f, b.y - 2.f), ImVec2(b.x - 1.f, b.y - 2.f), IM_COL32(214, 158, 70, 92), 1.f);

    // Tight banner (a half-width dashboard slot): shrink the compass + its offset so the text column isn't
    // squeezed off the right edge. Wider banners (compact viewer / full width) keep the original look.
    const bool  tight = availW < 260.f;
    // Base the compass size on the (scale-independent) wide/narrow predicate, then scale the layout px so the
    // branch can't flip when h is scaled. compassR is passed to DrawCompassRose already scaled.
    const float compassR = (tight ? 22.f : ((availW >= 680.f) ? 34.f : 29.f)) * vs;
    DrawCompassRose(dl, ImVec2(p.x + (tight ? 10.f : 26.f) * vs + compassR, p.y + h * 0.52f), compassR,
                    IM_COL32(224, 197, 154, 235), IM_COL32(164, 135, 92, 175));

    const float tx = p.x + (tight ? 14.f : 42.f) * vs + compassR * 2.f;

    // Integrated completion bar: a slim filled bar along the banner's bottom edge (no text). Drawn before the
    // layout branch so both the normal banner and the half-width widget reflow include it.
    if (o.bar == 1 && hasFrac)
    {
        const float barH = 4.f * vs;
        const ImVec2 ta(p.x + 2.f * vs, b.y - barH - 1.f * vs), tb(b.x - 2.f * vs, b.y - 1.f * vs);
        dl->AddRectFilled(ta, tb, IM_COL32(0, 0, 0, 150), 0.f);
        const float fwBar = (tb.x - ta.x) * std::clamp(frac, 0.f, 1.f);
        if (fwBar > 1.f)
            dl->AddRectFilledMultiColor(ta, ImVec2(ta.x + fwBar, tb.y),
                IM_COL32(255, 198, 82, 255), IM_COL32(255, 198, 82, 255),
                IM_COL32(214, 158, 70, 220), IM_COL32(214, 158, 70, 220));
    }

    const char* lvl = levelText ? levelText : "";
    const char* prg = progressText ? progressText : "";
    const float pillFs = std::max(11.f, fs - 2.f);
    const float lvlPw  = Gw2Ui::PillWidth(lvl, pillFs);
    const float prgPw  = Gw2Ui::PillWidth(prg, pillFs);
    const float pillPh = Gw2Ui::PillHeight(pillFs);

    // Half-width DASHBOARD WIDGET reflow: zone name top-left; region + level pill on the bottom row; the
    // progress pill is hidden (too cramped). The bar (integrated above, or below after return) still shows.
    if (o.isWidget && availW < kZhNarrow)
    {
        // A smaller, tighter level pill (the full-size pill reads oversized in a cramped half-width slot).
        const float rPillFs = std::max(11.f, fs - 5.f);
        const float rPadX = 14.f, rPadY = 3.f, rMinH = 16.f;
        const float rPillPh = Gw2Ui::PillHeight(rPillFs, rPadY, rMinH);
        const float rLvlPw  = Gw2Ui::PillWidth(lvl, rPillFs, rPadX);
        const float regFs   = std::max(11.f, fs - 4.f);

        float titleFs = std::max(14.f, fs + 1.f);
        const float twTop = std::max(40.f * vs, b.x - tx - (tight ? 8.f : 12.f) * vs);
        const float titleW0 = Gw2Ui::MeasureWidth(title ? title : "", titleFs);
        if (titleW0 > twTop && titleW0 > 1.f) titleFs = std::max(12.f, titleFs * (twTop / titleW0));

        // Stack the title + the (region + level pill) row as ONE tight block, vertically centered -- so there is
        // no big empty gap between the zone name and the region. Line/row heights are LAYOUT px: scale the fs
        // component (the text renders at fs*vs via PushTextScale) + the literal padding, but the fs passed to
        // LabelIn stays raw.
        const float titleLineH = titleFs * vs + 4.f * vs;
        const float rowH = std::max(rPillPh, regFs * vs + 6.f * vs);
        const float blockTop = p.y + std::max(6.f * vs, (h - (titleLineH + 4.f * vs + rowH)) * 0.5f);

        Gw2Ui::LabelIn(ImVec2(tx, blockTop), ImVec2(tx + twTop, blockTop + titleLineH), title ? title : "",
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(245, 232, 201, 255), true, nullptr, titleFs, 0.f, 1.2f);

        const float rowY = blockTop + titleLineH + 4.f * vs;
        float rxPos = tx;
        if (regionText && *regionText)
        {
            const float regMax = std::max(20.f * vs, (b.x - 10.f * vs) - rLvlPw - 8.f * vs - tx);
            const float regWc  = std::min(Gw2Ui::MeasureWidth(regionText, regFs), regMax);
            Gw2Ui::LabelIn(ImVec2(tx, rowY), ImVec2(tx + regWc, rowY + rowH), regionText,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, regFs);
            rxPos = tx + regWc + 8.f * vs;
        }
        if (lvl[0]) Gw2Ui::PillAt(dl, ImVec2(rxPos, rowY + (rowH - rPillPh) * 0.5f), lvl, rPillFs,
                                  Gw2Ui::kPillBorder, Gw2Ui::kPillText, Gw2Ui::kPillFill, rPadX, rPadY, rMinH);

        ImGui::Dummy(ImVec2(availW, h));
        ImGui::Spacing();
        return;
    }

    // Normal banner: title (+ dim region sub-line) on the left, the level + progress pills on the right.
    const float rInset = (tight ? 10.f : 16.f) * vs;
    const float lvlW = lvl[0] ? lvlPw : 0.f;
    const float prgW = prg[0] ? prgPw : 0.f;

    // Decide the pill layout. Side-by-side needs room for BOTH pills beside the title; when that would squeeze
    // the title (a big compass + two pills eat the width), auto-collapse to Stacked -- a single pill column frees
    // ~one pill width back to the title. The user wants stacking on BOTH surfaces whenever space is tight, so
    // this keys on the ACTUAL title room left next to the pills, not a fixed width breakpoint.
    int pills = o.pills;
    if (pills == 0)
    {
        const float sideW = lvlW + prgW + ((lvl[0] && prg[0]) ? 8.f * vs : 0.f) + rInset;
        const float titleFs0 = (availW >= 680.f) ? fs + 8.f : fs + 3.f;
        const float titleNatW = Gw2Ui::MeasureWidth(title ? title : "", titleFs0);
        const float sideTitleCol = (b.x - sideW) - tx - 8.f * vs;   // room left for the title beside both pills
        if (sideTitleCol < titleNatW || sideTitleCol < 64.f * vs) pills = 1;
    }

    const float cy = p.y + h * 0.5f;
    float pillsLeftX;
    if (pills == 0)   // Side-by-side: progress furthest right, level to its left, both vertically centered
    {
        float rx = b.x - rInset;
        if (prg[0]) { Gw2Ui::PillAt(dl, ImVec2(rx - prgPw, cy - pillPh * 0.5f), prg, pillFs); rx -= prgPw + 8.f * vs; }
        if (lvl[0]) { Gw2Ui::PillAt(dl, ImVec2(rx - lvlPw, cy - pillPh * 0.5f), lvl, pillFs); rx -= lvlPw + 8.f * vs; }
        pillsLeftX = rx;
    }
    else              // Stacked: level on top, progress below, right-aligned
    {
        const float gap = 4.f * vs;
        const float totalH = (lvl[0] && prg[0]) ? pillPh * 2.f + gap : pillPh;
        float yy = cy - totalH * 0.5f;
        if (lvl[0]) { Gw2Ui::PillAt(dl, ImVec2(b.x - rInset - lvlPw, yy), lvl, pillFs); yy += pillPh + gap; }
        if (prg[0]) { Gw2Ui::PillAt(dl, ImVec2(b.x - rInset - prgPw, yy), prg, pillFs); }
        pillsLeftX = b.x - rInset - std::max(lvlPw, prgPw);
    }

    // Title clamped to clear the pill column, with the dim region sub-line under it; the left text block is
    // vertically centered in the banner.
    const float tw = std::max(40.f * vs, pillsLeftX - tx - 8.f * vs);
    float titleFs = (availW >= 680.f) ? fs + 8.f : fs + 3.f;
    const float titleW0 = Gw2Ui::MeasureWidth(title ? title : "", titleFs);
    if (titleW0 > tw && titleW0 > 1.f) titleFs = std::max(13.f, titleFs * (tw / titleW0));   // shrink a long name to fit
    const bool  hasRegion = regionText && *regionText;
    const float regFs = fs - 3.f;
    // Line heights are LAYOUT px: scale the fs component (text renders at fs*vs via PushTextScale) + the literal,
    // while the fs passed to LabelIn below stays raw.
    const float titleLineH = titleFs * vs + 8.f * vs;
    const float regionLineH = hasRegion ? regFs * vs + 8.f * vs : 0.f;
    const float blockTop = p.y + std::max(8.f * vs, (h - (titleLineH + regionLineH)) * 0.5f);
    const float titleWc = std::min(Gw2Ui::MeasureWidth(title ? title : "", titleFs) + 8.f * vs, tw);
    Gw2Ui::LabelIn(ImVec2(tx, blockTop), ImVec2(tx + titleWc, blockTop + titleLineH), title ? title : "",
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(245, 232, 201, 255), true, nullptr, titleFs, 0.f, 1.2f);
    if (hasRegion)
        Gw2Ui::LabelIn(ImVec2(tx, blockTop + titleLineH), ImVec2(tx + tw, blockTop + titleLineH + regionLineH),
                       regionText, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, regFs);

    ImGui::Dummy(ImVec2(availW, h));
    ImGui::Spacing();
}

void DrawRichHeroHeader(App& app, int done, int total, float fs, const ZoneHeaderOpts& o)
{
    char lvl[32];  std::snprintf(lvl, sizeof(lvl), "Lv %d-%d", app.state.zone.MinLevel, app.state.zone.MaxLevel);
    char prog[48]; std::snprintf(prog, sizeof(prog), "%d/%d", done, total);
    const std::string region = Regions::Label(app.state.zone);
    const float frac = total > 0 ? (float)done / (float)total : 0.f;
    DrawRichHeroHeaderArt(app, app.state.zone.MapId, app.state.zone.Name.c_str(), region.c_str(),
                          lvl, prog, fs, o, frac, total > 0);
}

// Map the per-surface barText setting (0 On, 1 Above, 2 Below, 3 None) to the Gw2Ui::ProgressBar placement.
static Gw2Ui::BarText ZhBarText(int v)
{
    switch (v)
    {
        case 0:  return Gw2Ui::BarText::OnBar;
        case 2:  return Gw2Ui::BarText::Below;
        case 3:  return Gw2Ui::BarText::None;
        default: return Gw2Ui::BarText::Above;
    }
}

void DrawViewerHeader(App& app, int done, int total, float fs, bool compact, const ZoneHeaderOpts& o)
{
    // The below-bar shows its text at a size proportional to the header font (the fixed 15/13 px caption read
    // tiny next to the banner's larger title + pills); On-bar also gets a taller bar so its centered text fits.
    // barH is the height arg to Gw2Ui::ProgressBar, which already multiplies height by the ambient TextScale()
    // (= the viewer's PushTextScale(vs)) internally -- so do NOT pre-scale it by vs here, or it double-scales.
    const float barTextFs = std::max(13.f, fs - 3.f);
    const float barH = (o.barText == 0 /*On bar*/) ? (!compact ? 26.f : 22.f) : (!compact ? 20.f : 16.f);

    // A gathering run: the map name + gathered/total (this session, in the selected category) + a GREEN bar,
    // instead of the leveling objective progress. Covers both the viewer header AND the dashboard Zone Header widget.
    if (FarmingRunActive(app))
    {
        // gathered/total in the selected category -- cached behind the SAME change token ViewerFarming uses
        // (map / category / enabled-material set / gathered set) so this doesn't rescan activeGather (up to
        // thousands of nodes) every frame for the viewer header AND the dashboard Zone Header widget.
        static uint32_t s_mid = 0xFFFFFFFFu;
        static int      s_cat = -1;
        static size_t   s_hidden = (size_t)-1, s_gathered = (size_t)-1;
        static int      s_fd = 0, s_ft = 0;
        size_t hiddenN = 0;
        if (auto hit = app.config.gatherHidden.find(app.state.activeGatherMapId); hit != app.config.gatherHidden.end())
            hiddenN = hit->second.size();
        if (app.state.activeGatherMapId != s_mid || app.config.gatherCategory != s_cat ||
            hiddenN != s_hidden || app.state.farmGathered.size() != s_gathered)
        {
            int fd = 0, ft = 0;
            for (const auto& g : app.state.activeGather)
            {
                const FarmNode* nd = g.Node; if (!nd) continue;
                if (!app.config.GatherShown(app.state.activeGatherMapId, nd->Material)) continue;
                if (!Farm::CategoryMatch(app.config.gatherCategory, nd->Category)) continue;
                ++ft; if (app.state.farmGathered.count(nd)) ++fd;
            }
            s_fd = fd; s_ft = ft;
            s_mid = app.state.activeGatherMapId; s_cat = app.config.gatherCategory;
            s_hidden = hiddenN; s_gathered = app.state.farmGathered.size();
        }
        const int fd = s_fd, ft = s_ft;
        const uint32_t mid = CurrentMapId();
        const bool loaded = app.state.zone.Loaded;
        const char* name = loaded ? app.state.zone.Name.c_str() : StaticData::MapNameOr(mid, "Current map");
        const std::string region = loaded ? Regions::Label(app.state.zone) : std::string();
        char lvl[32]; if (loaded) std::snprintf(lvl, sizeof(lvl), "Lv %d-%d", app.state.zone.MinLevel, app.state.zone.MaxLevel); else lvl[0] = '\0';
        char prog[48]; std::snprintf(prog, sizeof(prog), "%d/%d", fd, ft);
        const float frac = ft > 0 ? (float)fd / (float)ft : 0.f;
        DrawRichHeroHeaderArt(app, loaded ? app.state.zone.MapId : mid, name, region.c_str(), lvl, prog, fs, o, frac, ft > 0);
        if (o.bar == 2 && ft > 0)
            Gw2Ui::ProgressBar(frac, prog, 0.f, barH, IM_COL32(150, 200, 120, 255), 0, ZhBarText(o.barText), barTextFs);
        return;
    }

    // Dungeon: dungeon name + active path (level-pill slot) + Step n/total progress pill + optional bar.
    if (app.state.viewerMode == ViewerMode::Dungeon && app.state.inst)
    {
        const int t = (int)app.state.instSteps.size();
        const char* rn = (app.state.instRouteIdx < (int)app.state.inst->Routes.size())
                         ? app.state.inst->Routes[app.state.instRouteIdx].Name.c_str() : "Dungeon";
        const int   cur = std::min(app.state.instStep + 1, t);
        char prg[48]; if (t > 0) std::snprintf(prg, sizeof(prg), "Step %d/%d", cur, t); else prg[0] = '\0';
        const float frac = t > 0 ? (float)cur / (float)t : 0.f;
        DrawRichHeroHeaderArt(app, app.state.inst->MapId, app.state.inst->Name.c_str(), "",
                              (rn && rn[0]) ? rn : "Dungeon", prg, fs, o, frac, t > 0);
        if (o.bar == 2 && t > 0)
        {
            char pct[24]; std::snprintf(pct, sizeof(pct), "%d/%d", cur, t);
            Gw2Ui::ProgressBar(frac, pct, 0.f, barH, IM_COL32(255, 198, 82, 255), 0, ZhBarText(o.barText), barTextFs);
        }
        return;
    }
    // Off-coverage (Recommend): the real map name + a "Level N" pill + "No guide route" sub-line; never a bar.
    if (!app.state.zone.Loaded)
    {
        const uint32_t mid = CurrentMapId();
        const char* mapName = StaticData::MapNameOr(mid, "Current map");
        char lvl[32] = "";
        if (app.state.charLevel > 0) std::snprintf(lvl, sizeof(lvl), "Level %d", app.state.charLevel);
        DrawRichHeroHeaderArt(app, mid, mapName, "No guide route", lvl, "", fs, o, 0.f, false);
        return;
    }
    // Step / Travel / Complete: the normal zone header (progress reads total/total when complete).
    DrawRichHeroHeader(app, done, total, fs, o);
    if (o.bar == 2 && total > 0)
    {
        char prg[48]; std::snprintf(prg, sizeof(prg), "%d/%d", done, total);
        Gw2Ui::ProgressBar((float)done / (float)total, prg, 0.f, barH,
                           IM_COL32(255, 198, 82, 255), 0, ZhBarText(o.barText), barTextFs);
    }
}
