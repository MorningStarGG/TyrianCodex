#include "ui/tabs/MapThumbnail.h"
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
#include "render/tasks/TaskIcons.h" // DrawTaskIcon + TaskChipAccent (heart task icons in the tooltip)
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

// The tile grid covering a continent rect at a fit-for-thumbnail zoom:
// pick a zoom where the rect spans a few tiles, shrinking if the grid would exceed MAXTILES. False for
// invalid continents / zooms. The max rect edges are treated as exclusive: if the crop ends exactly on a tile
// boundary, the tile starting at that boundary does not overlap the crop and must not be requested.
struct ZoneTiles
{
    int zoom, txMin, tyMin, cols, rows;
    double scale;
};

static bool ComputeTiles(int continentId, float cl, float ct, float cR, float cb, ZoneTiles &g, int maxZoom)
{
    if (continentId < 1 || maxZoom < 2)
        return false; // continent-agnostic now (1 = Tyria, 2 = the Mists)
    const float zw = std::max(1.f, cR - cl), zh = std::max(1.f, cb - ct);
    const int TILE = 256, MAXTILES = 16;
    int zoom = std::clamp((int)std::lround(maxZoom - std::log2(std::max(zw, zh) / 512.0)), 2, maxZoom);
    int txMin = 0, txMax = 0, tyMin = 0, tyMax = 0, cols = 1, rows = 1;
    double scale = TILE;
    for (;;)
    {
        scale = TILE * std::pow(2.0, maxZoom - zoom);
        txMin = (int)std::floor(cl / scale);
        txMax = std::max(txMin, (int)std::ceil(cR / scale) - 1);
        tyMin = (int)std::floor(ct / scale);
        tyMax = std::max(tyMin, (int)std::ceil(cb / scale) - 1);
        cols = txMax - txMin + 1;
        rows = tyMax - tyMin + 1;
        if (cols * rows <= MAXTILES || zoom <= 2)
            break;
        --zoom;
    }
    g = {zoom, txMin, tyMin, cols, rows, scale};
    return true;
}

// Composite the GW2 map tiles for an arbitrary continent rect into [origin, origin+size], cropped to the rect
// so it fills the frame, clipped to the area. Live-drawn (place the grid + clip the overhang, no stitch);
// tiles come from the disk cache (ImageCache::GetTile) and pop in as they load. False = not continent 1.
// NOTE: the tile IMAGERY for continent 1 lives entirely on FLOOR 1 (the world-map floor). A map's own `floor`
// (e.g. 49 for the Crystal Desert / PoF maps) is the detailed DATA floor - it has no tile imagery (404s), so
// the thumbnail must always use floor 1, never the per-map floor.
static const int kTileFloor = 1;

bool DrawMapThumbnailRect(int continentId, float cl, float ct, float cR, float cb, ImVec2 origin, ImVec2 size,
                          const MapMark *marks, int markCount, int highlightIdx, bool backing,
                          int maxZoom, const char *staticMap)
{
    if (staticMap && *staticMap) // bundled static map image (instanced content whose floors have no live tiles)
    {
        const Texture_t *img = ImageCache::GetMapImage(staticMap);
        if (!img || !img->Resource)
            return false;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (backing)
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 14, 18, 255));
        float iw = (float)img->Width, ih = (float)img->Height;
        if (iw < 1.f)
            iw = size.x;
        if (ih < 1.f)
            ih = size.y;
        const float s = std::min(size.x / iw, size.y / ih); // fit, preserve aspect, centre
        const float dw = iw * s, dh = ih * s;
        const ImVec2 o(origin.x + (size.x - dw) * 0.5f, origin.y + (size.y - dh) * 0.5f);
        dl->AddImage((ImTextureID)img->Resource, o, ImVec2(o.x + dw, o.y + dh));
        dl->AddRect(o, ImVec2(o.x + dw, o.y + dh), IM_COL32(180, 165, 120, 200)); // gold frame
        return true;
    }
    ZoneTiles g;
    if (!ComputeTiles(continentId, cl, ct, cR, cb, g, maxZoom))
        return false;
    const int TILE = 256;
    const double pxPerUnit = TILE / g.scale;
    const float gx0 = (float)((cl - g.txMin * g.scale) * pxPerUnit), gy0 = (float)((ct - g.tyMin * g.scale) * pxPerUnit);
    const float gx1 = (float)((cR - g.txMin * g.scale) * pxPerUnit), gy1 = (float)((cb - g.tyMin * g.scale) * pxPerUnit);
    const float cw = std::max(1.f, gx1 - gx0), ch = std::max(1.f, gy1 - gy0);

    const float s = std::min(size.x / cw, size.y / ch); // fit the crop, preserve aspect, centre
    const float drawW = cw * s, drawH = ch * s;
    const ImVec2 o(origin.x + (size.x - drawW) * 0.5f, origin.y + (size.y - drawH) * 0.5f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (backing)
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 14, 18, 255));
    dl->PushClipRect(o, ImVec2(o.x + drawW, o.y + drawH), true);
    for (int row = 0; row < g.rows; ++row)
        for (int col = 0; col < g.cols; ++col)
        {
            const Texture_t *t = ImageCache::GetTile(continentId, kTileFloor, g.zoom, g.txMin + col, g.tyMin + row);
            if (!t || !t->Resource)
                continue;
            const ImVec2 a(o.x + (col * TILE - gx0) * s, o.y + (row * TILE - gy0) * s);
            // Tile-seam fix: sample HALF A TEXEL in from every edge so bilinear filtering lands on the border
            // texel centres instead of blending past the edge (the edge/wrap bleed that paints faint lines
            // where tiles meet - most visible when scaled up in the enlarged view). Adjacent tiles stay
            // continuous (a sub-pixel world shift, invisible at any zoom).
            const float uv = 0.5f / (float)TILE;
            dl->AddImage((ImTextureID)t->Resource, a, ImVec2(a.x + TILE * s, a.y + TILE * s),
                         ImVec2(uv, uv), ImVec2(1.0f - uv, 1.0f - uv));
        }
    // Objective markers (continent coords -> position inside the fitted crop). Drawn under the clip so they
    // never spill past the map frame; the highlighted one (the hovered objective) gets a larger gold ring.
    const float spanX = std::max(1.f, cR - cl), spanY = std::max(1.f, cb - ct);
    for (int i = 0; i < markCount; ++i)
    {
        const float mx = o.x + (marks[i].cx - cl) / spanX * drawW;
        const float my = o.y + (marks[i].cy - ct) / spanY * drawH;
        const bool hi = (i == highlightIdx);
        const float isz = hi ? 22.f : 14.f;
        const Texture_t *t = marks[i].iconId ? Tex::GetAssetTex(marks[i].iconId) : nullptr;
        if (t && t->Resource)
            dl->AddImage((ImTextureID)t->Resource, ImVec2(mx - isz * 0.5f, my - isz * 0.5f), ImVec2(mx + isz * 0.5f, my + isz * 0.5f));
        else // fallback dot while the gw2dat icon downloads (or an unknown type)
            dl->AddCircleFilled(ImVec2(mx, my), hi ? 6.f : 3.5f, hi ? IM_COL32(255, 221, 130, 255) : IM_COL32(255, 198, 110, 235), 16);
        if (hi)
            dl->AddCircle(ImVec2(mx, my), isz * 0.5f + 3.f, IM_COL32(255, 242, 188, 255), 22, 2.0f);
    }
    dl->PopClipRect();
    dl->AddRect(o, ImVec2(o.x + drawW, o.y + drawH), IM_COL32(180, 165, 120, 200)); // gold frame
    return true;
}

// Convenience: one zone's map (its continent rect).
bool DrawMapThumbnail(const Zone *z, ImVec2 origin, ImVec2 size)
{
    return z->HasRects && DrawMapThumbnailRect(z->ContinentId, z->ContRect[0], z->ContRect[1], z->ContRect[2], z->ContRect[3], origin, size);
}

// A hover tooltip: a title + level range + a 300x200 map thumbnail of a continent rect. Shared by the
// Zones-tab region headers and the Checklist region/zone rows (one implementation, identical look).
void MapHoverTooltip(const char *title, int lvlLo, int lvlHi, int contId, const float rect[4])
{
    Gw2Ui::TooltipBegin();
    Gw2Ui::Label(title, Gw2Ui::kGold, true, nullptr, 18.f);
    char sub[48];
    std::snprintf(sub, sizeof(sub), "Level %d-%d", lvlLo, lvlHi);
    Gw2Ui::Label(sub, Gw2Ui::kTextSub, false, nullptr, 16.f);
    ImGui::Spacing();
    const ImVec2 sz(300.f, 200.f);
    DrawMapThumbnailRect(contId, rect[0], rect[1], rect[2], rect[3], ImGui::GetCursorScreenPos(), sz);
    ImGui::Dummy(sz);
    Gw2Ui::TooltipEnd();
}

const Step *FindStepById(App &app, const std::string &stepId, const Zone **outZone)
{
    if (outZone)
        *outZone = nullptr;
    const size_t colon = stepId.find(':');
    if (colon == std::string::npos)
        return nullptr;
    uint32_t mapId = 0;
    try
    {
        mapId = (uint32_t)std::stoul(stepId.substr(0, colon));
    }
    catch (...)
    {
        return nullptr;
    }
    auto it = app.zones.find(mapId);
    if (it == app.zones.end())
        return nullptr;
    if (outZone)
        *outZone = &it->second;
    for (const Step &s : it->second.Steps)
        if (s.StepId == stepId)
            return &s;
    return nullptr;
}

void ZoneMapCrop(const Zone *z, float out[4])
{
    const float *r = (z && z->HasSectorRect) ? z->SectorRect : (z ? z->ContRect : nullptr);
    if (!r)
    {
        out[0] = out[1] = out[2] = out[3] = 0.f;
        return;
    }
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    out[3] = r[3];
}

// One objective's hover tooltip: type-colored title + wiki detail; + a zone thumbnail with it circled (withMap).
void ObjectiveTooltip(const char *name, const char *type, const Step *step,
                      const Zone *zone, float cx, float cy, bool withMap, const char *subtitle, const char *footer,
                      bool withTasks)
{
    const std::string t = type ? type : "";
    Gw2Ui::TooltipBegin();
    Gw2Ui::Label((name && *name) ? name : (type ? type : "(objective)"), Objective::ColorOf(t, 255), true, nullptr, 18.f);
    if (subtitle && *subtitle)
        Gw2Ui::Label(subtitle, Gw2Ui::kTextSub, false, nullptr, 14.f);
    if (step)
    {
        const std::string detail = DetailText(*step);
        if (!detail.empty())
        {
            Gw2Ui::TooltipSeparator();
            Gw2Ui::TooltipText(detail.c_str());
        }
    }
    // A heart's task icons + labels (opt-in; the Info Panel Current/Upcoming texts pass withTasks=true). Each
    // task = the procedural category icon + its label, one per row -- reserve the row with a Dummy (sizes the
    // tooltip), then draw the icon + label at absolute coords inside it.
    if (step && withTasks && !step->TaskChips.empty())
    {
        Gw2Ui::TooltipSeparator();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float sz = 16.f, gap = 7.f, fsT = 15.f;
        for (const TaskChip &c : step->TaskChips)
        {
            const float lw = Gw2Ui::MeasureWidth(c.Label.c_str(), fsT);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(sz + gap + lw, sz + 3.f));
            const ImU32 accent = TaskChipAccent(c.Category, IM_COL32(210, 180, 120, 255));
            DrawTaskIcon(dl, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), sz, c, accent, false);
            Gw2Ui::LabelIn(ImVec2(p.x + sz + gap, p.y), ImVec2(p.x + sz + gap + lw, p.y + sz), c.Label.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(224, 220, 208, 255), false, nullptr, fsT);
        }
    }
    if (withMap && zone && zone->HasRects && zone->ContinentId == 1)
    {
        ImGui::Spacing();
        uint32_t iid = 0;
        Objective::TryIconAssetId(t, iid);
        float zr[4];
        ZoneMapCrop(zone, zr);
        const ImVec2 sz(280.f, 188.f);
        const float availW = ImGui::GetContentRegionAvail().x;
        const float offX = std::max(0.f, (availW - sz.x) * 0.5f); // centre under the (often wider) text
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const MapMark mark{cx, cy, iid};
        DrawMapThumbnailRect(zone->ContinentId, zr[0], zr[1], zr[2], zr[3],
                             ImVec2(cur.x + offX, cur.y), sz, &mark, 1, 0, /*backing*/ false);
        ImGui::Dummy(ImVec2(std::max(availW, sz.x), sz.y));
    }
    if (footer && *footer)
    {
        ImGui::Spacing();
        // footer may carry several muted lines separated by '\n' (TtLine only word-wraps ONE string), so split
        // here to keep distinct notes -- e.g. a "Then: ..." list + a click hint -- on their own lines.
        std::string f(footer);
        size_t start = 0;
        for (;;)
        {
            size_t nl = f.find('\n', start);
            std::string line = f.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            if (!line.empty())
                Gw2Ui::TooltipMuted(line.c_str());
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
    }
    Gw2Ui::TooltipEnd();
}

// A group's hover tooltip (region/zone node, Atlas zone row): title + level/(count) + thumbnail with all pins.
void GroupTooltip(const char *title, int lvlLo, int lvlHi, int done, int total,
                  int contId, const float rect[4], const MapMark *objPts, int objCount, bool withMap,
                  const char *footer, int maxZoom, const char *staticMap)
{
    Gw2Ui::TooltipBegin();
    Gw2Ui::Label((title && *title) ? title : "", Gw2Ui::kGold, true, nullptr, 18.f);
    char sub[64];
    if (total > 0)
        std::snprintf(sub, sizeof(sub), "Level %d-%d    %d/%d", lvlLo, lvlHi, done, total);
    else
        std::snprintf(sub, sizeof(sub), "Level %d-%d", lvlLo, lvlHi);
    Gw2Ui::Label(sub, Gw2Ui::kTextSub, false, nullptr, 16.f);
    if (withMap) // tiles for cont 1/2 world maps; a bundled static image when `staticMap` is set
    {
        ImGui::Spacing();
        const ImVec2 sz(300.f, 200.f);
        const float availW = ImGui::GetContentRegionAvail().x;
        const float offX = std::max(0.f, (availW - sz.x) * 0.5f); // centre under the (often wider) text
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        if (DrawMapThumbnailRect(contId, rect[0], rect[1], rect[2], rect[3],
                                 ImVec2(cur.x + offX, cur.y), sz, objPts, objCount, -1, /*backing*/ false, maxZoom, staticMap))
            ImGui::Dummy(ImVec2(std::max(availW, sz.x), sz.y)); // reserve only when something actually drew (no blank gap)
    }
    if (footer && *footer)
    {
        ImGui::Spacing();
        // footer may carry several muted lines separated by '\n' (TtLine only word-wraps ONE string), so split
        // here to keep distinct notes -- e.g. a "Then: ..." list + a click hint -- on their own lines.
        std::string f(footer);
        size_t start = 0;
        for (;;)
        {
            size_t nl = f.find('\n', start);
            std::string line = f.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            if (!line.empty())
                Gw2Ui::TooltipMuted(line.c_str());
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
    }
    Gw2Ui::TooltipEnd();
}

// Queue background downloads (to the on-disk cache) for a continent rect's thumbnail tiles, so a later hover
// renders instantly instead of waiting on the network. Cheap + idempotent (already-known tiles are skipped).
void PrefetchRectTiles(int continentId, float cl, float ct, float cR, float cb)
{
    ZoneTiles g;
    if (!ComputeTiles(continentId, cl, ct, cR, cb, g, /*maxZoom*/ 7))
        return;
    for (int row = 0; row < g.rows; ++row)
        for (int col = 0; col < g.cols; ++col)
            ImageCache::PrefetchTile(continentId, kTileFloor, g.zoom, g.txMin + col, g.tyMin + row); // imagery floor (1)
}
