#include "Widgets.h"
#include "ApiWidgetUtil.h"        // DashApi::Freshness
#include "WidgetUtil.h"           // DashUtil::Narrow
#include "app/App.h"
#include "app/AccountData.h"
#include "app/StaticData.h"
#include "app/Config.h"
#include "ui/Gw2Ui.h"
#include "ui/WvwFormat.h"         // WvwFmt::WvwColor / WvwAbbrev
#include "ui/tabs/MapThumbnail.h" // DrawMapThumbnailRect
#include "Shared.h"               // IsInWvW, CurrentMapId
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

// WvW dashboard widgets (anonymous live match data from AccountData; NO API key required, so they guard on
// haveWvwMatch instead of DashApi::Gate). Three widgets: the matchup scoreboard, the current-map view, and a
// configurable multi-map view -- the latter two render the real WvW map (continent-2 tiles) with each
// objective drawn as a pin colored by its current owner.
using WvwFmt::WvwColor;
using WvwFmt::WvwAbbrev;

namespace
{
    // A dim status/placeholder line that WRAPS to the widget width (so it doesn't clip at half-width).
    void WvwNote(const char* text, float w)
    {
        const float h = std::max(18.f, Gw2Ui::MeasureWrappedHeight(text, 14.f, w));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, 14.f, w);
    }

    // Render a WvW map's tiles (the Mists) + each objective as an owner-colored pin. Projects objective
    // continent coords onto the SAME aspect-fitted, centered draw box DrawMapThumbnailRect uses.
    void DrawWvwMap(const AccountData::WvwMapState& map, ImVec2 origin, ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rect[4]; int contId = 2;
        if (!StaticData::WvwMapRect(map.mapId, rect, contId))
        {
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 14, 18, 255), 3.f);
            dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(120, 100, 64, 110), 3.f);
            Gw2Ui::LabelDL(dl, origin, ImVec2(origin.x + size.x, origin.y + size.y), "Loading map...",
                           Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 13.f);
            return;
        }
        DrawMapThumbnailRect(contId, rect[0], rect[1], rect[2], rect[3], origin, size, nullptr, 0, -1, /*backing*/ true);
        const float cl = rect[0], ct = rect[1], cR = rect[2], cb = rect[3];
        const float rw = std::max(1.f, cR - cl), rh = std::max(1.f, cb - ct), aspect = rw / rh;
        float drawW = size.x, drawH = size.x / aspect;
        if (drawH > size.y) { drawH = size.y; drawW = size.y * aspect; }
        const ImVec2 o(origin.x + (size.x - drawW) * 0.5f, origin.y + (size.y - drawH) * 0.5f);
        dl->PushClipRect(o, ImVec2(o.x + drawW, o.y + drawH), true);
        const double now = (double)time(nullptr);
        for (const AccountData::WvwObjState& ob : map.objectives)
        {
            float cx, cy;
            if (!StaticData::ObjectiveCoord(ob.objId, cx, cy)) continue;
            const float px = o.x + (cx - cl) / rw * drawW, py = o.y + (cy - ct) / rh * drawH;
            dl->AddCircleFilled(ImVec2(px, py), 4.5f, WvwColor(ob.owner), 14);
            dl->AddCircle(ImVec2(px, py), 4.5f, IM_COL32(0, 0, 0, 170), 14, 1.3f);
            if (ob.lastFlipped > 0.0 && now - ob.lastFlipped < 300.0)   // flipped in the last 5 min -> emphasize
                dl->AddCircle(ImVec2(px, py), 8.f, IM_COL32(255, 242, 188, 210), 18, 1.6f);
        }
        dl->PopClipRect();
    }

    // Shared GW2-card tokens (so the WvW tables match BeginCard / StatGrid chrome elsewhere).
    constexpr float kPad = 5.f;      // inner inset from the frame edge
    constexpr float kHeaderH = 16.f;
    constexpr float kRowH = 24.f;    // digits up top + a framed proportional control bar along the bottom
    inline ImU32 kCardBg()    { return IM_COL32(0, 0, 0, 45); }
    inline ImU32 kCardLine()  { return IM_COL32(120, 110, 88, 110); }
    inline ImU32 kZebra()     { return IM_COL32(255, 255, 255, 12); }

    // The dim R / B / G column header that sits above the data rows (right-aligned over each value column).
    void TriHeader(float w)
    {
        const float fs = 12.f, iw = w - kPad * 2.f, lw = iw * 0.36f, colW = (iw - lw) / 3.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, kHeaderH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ix = p.x + kPad;
        static const char* heads[3] = { "R", "B", "G" };
        for (int c = 0; c < 3; ++c)
            Gw2Ui::LabelDL(dl, ImVec2(ix + lw + colW * c, p.y), ImVec2(ix + lw + colW * (c + 1) - 7.f, p.y + kHeaderH),
                           heads[c], Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, WvwColor(c + 1), false, nullptr, fs);
    }

    // One metric row: label + R/B/G values (right-aligned, colored) over an explicit zebra fill, with a thin
    // proportional R/B/G control strip along the bottom (the iconic WvW territory bar, encoding magnitude).
    void TriRow(const char* label, long long r, long long b, long long g, float w, int rowIdx)
    {
        const float fs = 14.f, iw = w - kPad * 2.f, lw = iw * 0.36f, colW = (iw - lw) / 3.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, kRowH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ix = p.x + kPad, textBot = p.y + kRowH - 9.f;
        if ((rowIdx & 1) == 0)   // explicit zebra -- the textured RowBackground stripe is invisible over near-black
            dl->AddRectFilled(ImVec2(ix, p.y), ImVec2(ix + iw, p.y + kRowH), kZebra(), 0.f);
        Gw2Ui::LabelDL(dl, ImVec2(ix + 2.f, p.y), ImVec2(ix + lw, textBot), label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSub, false, nullptr, fs);
        const long long v[3] = { r, b, g };
        for (int c = 0; c < 3; ++c)
        {
            char vb[16]; std::snprintf(vb, sizeof(vb), "%lld", v[c]);
            Gw2Ui::LabelDL(dl, ImVec2(ix + lw + colW * c, p.y), ImVec2(ix + lw + colW * (c + 1) - 7.f, textBot),
                           vb, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, WvwColor(c + 1), false, nullptr, fs);
        }
        // Framed proportional R/B/G control bar in the row's bottom band: recessed track + per-segment
        // vertical-gradient fill + warm rim (the ProgressBar vocabulary, so the split reads as a real bar).
        const float barH = 6.f, bBot = p.y + kRowH - 1.5f, bTop = bBot - barH, bx0 = ix, bx1 = ix + iw;
        dl->AddRectFilled(ImVec2(bx0, bTop), ImVec2(bx1, bBot), IM_COL32(6, 8, 7, 220), 2.f);   // recessed track
        const float fy0 = bTop + 1.f, fy1 = bBot - 1.f, fx0 = bx0 + 1.f, fx1 = bx1 - 1.f, span = fx1 - fx0;
        const long long tot = std::max(0LL, r) + std::max(0LL, b) + std::max(0LL, g);
        if (tot <= 0)
            dl->AddRectFilled(ImVec2(fx0, fy0), ImVec2(fx1, fy1), IM_COL32(70, 66, 58, 140), 0.f);
        else
        {
            float x = fx0;
            for (int c = 0; c < 3; ++c)
            {
                const long long val = std::max(0LL, v[c]);
                float segW = (c == 2) ? (fx1 - x) : (float)((double)val / (double)tot * span);
                if (val > 0 && segW < 3.f) segW = 3.f;
                if (segW < 0.f) segW = 0.f;
                if (segW > 0.f)
                {
                    const ImU32 top = WvwColor(c + 1), bot = (WvwColor(c + 1) & 0x00FFFFFFu) | (170u << 24);
                    dl->AddRectFilledMultiColor(ImVec2(x, fy0), ImVec2(x + segW, fy1), top, top, bot, bot);
                }
                x += segW;
                if (c < 2 && x > fx0 && x < fx1) dl->AddLine(ImVec2(x, fy0), ImVec2(x, fy1), IM_COL32(0, 0, 0, 150), 1.f);
            }
        }
        dl->AddRect(ImVec2(bx0, bTop), ImVec2(bx1, bBot), IM_COL32(150, 124, 70, 170), 2.f, 0, 1.f);   // warm rim
    }

    // The per-map data rows (counts / ppt / types) per the shared Config toggles, drawn as a contained GW2
    // card (fill + tan border) with a striped R/B/G table inside -- so it reads as a panel, not text on black.
    void DrawWvwData(const AccountData::WvwMapState& map, const Config& cfg, float w)
    {
        // Collect the enabled rows up front so we can frame the table before painting content on top of it.
        struct Row { const char* label; long long r, b, g; };
        Row rows[8]; int n = 0;
        if (cfg.wvwShowCounts && n < 8)
            rows[n++] = { "Owned", map.ownedByColor[0], map.ownedByColor[1], map.ownedByColor[2] };
        if (cfg.wvwShowPpt && n < 8)
        {
            long long ppt[3] = { 0, 0, 0 };
            for (const auto& ob : map.objectives) if (ob.owner >= 1 && ob.owner <= 3) ppt[ob.owner - 1] += ob.pointsTick;
            rows[n++] = { "PPT", ppt[0], ppt[1], ppt[2] };
        }
        if (cfg.wvwShowTypes)
        {
            struct T { const char* name; const char* key; };
            static const T types[] = { {"Keeps","Keep"}, {"Towers","Tower"}, {"Camps","Camp"}, {"Castle","Castle"} };
            for (const T& t : types)
            {
                long long cnt[3] = { 0, 0, 0 };
                for (const auto& ob : map.objectives)
                    if (ob.type == t.key && ob.owner >= 1 && ob.owner <= 3) cnt[ob.owner - 1]++;
                if ((cnt[0] || cnt[1] || cnt[2]) && n < 8) rows[n++] = { t.name, cnt[0], cnt[1], cnt[2] };
            }
        }
        if (n == 0) return;

        const float H = kPad * 2.f + kHeaderH + kRowH * (float)n;
        const ImVec2 tl = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(tl, ImVec2(tl.x + w, tl.y + H), kCardBg(), 3.f);
        dl->AddRect(tl, ImVec2(tl.x + w, tl.y + H), kCardLine(), 3.f, 0, 1.f);

        // Render the header + rows on top of the frame (contiguous: zero inter-item spacing).
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.f));
        ImGui::Dummy(ImVec2(w, kPad));
        TriHeader(w);
        for (int i = 0; i < n; ++i) TriRow(rows[i].label, rows[i].r, rows[i].b, rows[i].g, w, i);
        ImGui::Dummy(ImVec2(w, kPad));
        ImGui::PopStyleVar();
    }

    // A titled header band for a WvW map: a subtle panel + a left accent bar in the map's team color
    // (Red/Blue/Green borderlands; Eternal Battlegrounds + Edge of the Mists use gold). The map you are
    // physically standing in gets a brighter fill + gold border.
    void WvwMapHeader(const char* name, int mapType, bool here, float w)
    {
        const float h = 23.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 q(p.x + w, p.y + h);
        dl->AddRectFilled(p, q, here ? IM_COL32(48, 43, 28, 205) : IM_COL32(26, 26, 23, 160), 3.f);
        dl->AddRect(p, q, here ? IM_COL32(196, 176, 128, 150) : IM_COL32(120, 100, 64, 90), 3.f, 0, 1.f);
        const ImU32 accent = mapType == 1 ? WvwColor(1) : mapType == 2 ? WvwColor(2) : mapType == 3 ? WvwColor(3) : Gw2Ui::kGold;
        dl->AddRectFilled(p, ImVec2(p.x + 3.f, p.y + h), accent, 0.f);
        Gw2Ui::LabelDL(dl, ImVec2(p.x + 10.f, p.y), ImVec2(q.x - 6.f, q.y), name, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, here ? Gw2Ui::kGold : Gw2Ui::kTextSelected, true, nullptr, 15.f);
    }

    const char* WvwMapLabel(int mapType)
    {
        switch (mapType)
        {
            case 0: return "Eternal Battlegrounds"; case 1: return "Red Borderlands"; case 2: return "Blue Borderlands";
            case 3: return "Green Borderlands";     case 4: return "Edge of the Mists"; default: return "WvW Map";
        }
    }
    int WvwMapModeFor(const Config& cfg, int mapType)
    {
        switch (mapType)
        {
            case 0: return cfg.wvwMapEb; case 1: return cfg.wvwMapRbl; case 2: return cfg.wvwMapBbl;
            case 3: return cfg.wvwMapGbl; case 4: return cfg.wvwMapEotm; default: return 0;
        }
    }

    // --- right-click config menus (Config-backed, like the Route Arrow widget) ---
    void WvwDataNodes(const Config& cfg, std::vector<Gw2Ui::MenuNode>& out, int base)
    {
        Gw2Ui::MenuNode a; a.label = "Owned counts";       a.id = base + 0; a.selected = cfg.wvwShowCounts; out.push_back(a);
        Gw2Ui::MenuNode b; b.label = "PPT";                b.id = base + 1; b.selected = cfg.wvwShowPpt;    out.push_back(b);
        Gw2Ui::MenuNode c; c.label = "Per-type breakdown"; c.id = base + 2; c.selected = cfg.wvwShowTypes;  out.push_back(c);
    }
    bool WvwDataDispatch(Config& cfg, int picked, int base)
    {
        if (picked == base + 0) { cfg.wvwShowCounts = !cfg.wvwShowCounts; return true; }
        if (picked == base + 1) { cfg.wvwShowPpt    = !cfg.wvwShowPpt;    return true; }
        if (picked == base + 2) { cfg.wvwShowTypes  = !cfg.wvwShowTypes;  return true; }
        return false;
    }
    // Right-click anywhere over [p0, (p0.x+w, endY)] opens the menu; returns the picked leaf id (-1 = none).
    int WvwMenu(const char* id, const ImVec2& p0, float w, float endY, const std::vector<Gw2Ui::MenuNode>& nodes)
    {
        const bool open = ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + w, endY)) && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        return Gw2Ui::ContextMenuTree(id, nodes, open);
    }
}

void DashW::WvwMatchup(App& app, float w)
{
    (void)app;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWvwMatch) { WvwNote(AccountData::WvwLoadStage(), w); return; }
    int order[3] = { 0, 1, 2 };
    std::sort(order, order + 3, [&](int a, int b) { return m.wvwSides[a].score > m.wvwSides[b].score; });
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (DashUtil::Narrow(w))   // stacked per-team blocks
    {
        for (int i = 0; i < 3; ++i)
        {
            const int c = order[i];
            const AccountData::WvwSide& s = m.wvwSides[c];
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, 34.f));
            const std::string name = s.worldName.empty() ? std::string(c == 0 ? "Red" : c == 1 ? "Blue" : "Green") : s.worldName;
            Gw2Ui::LabelDL(dl, p, ImVec2(p.x + w, p.y + 17.f), name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, WvwColor(c + 1), c + 1 == m.wvwMyColor, nullptr, 15.f);
            const double kd = s.kdDeaths > 0 ? (double)s.kdKills / (double)s.kdDeaths : (double)s.kdKills;
            char meta[112]; std::snprintf(meta, sizeof(meta), "%s   VP %d   +%lld   K/D %.2f", WvwAbbrev(s.score).c_str(), s.victoryPoints, s.ppt, kd);
            Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y + 17.f), ImVec2(p.x + w, p.y + 34.f), meta, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSub, false, nullptr, 13.f);
        }
        DashApi::Freshness(m.wvwMatchAt);
        return;
    }

    // wide table: World | Score | VP | Skm | PPT | K-D
    static const char* heads[6] = { "World", "Score", "VP", "Skm", "PPT", "K-D" };
    static const float colF[6]  = { 0.34f, 0.15f, 0.10f, 0.12f, 0.14f, 0.15f };
    const float rh = 19.f;
    auto drawRow = [&](const ImVec2& p, bool header, int c) {
        float x = p.x;
        for (int col = 0; col < 6; ++col)
        {
            const float cw = w * colF[col];
            char b[32]; const char* txt = b;
            const AccountData::WvwSide& s = m.wvwSides[c];
            const ImU32 tcol = header ? Gw2Ui::kTextSub : (col == 0 ? WvwColor(c + 1) : Gw2Ui::kTextSelected);
            const Gw2Ui::HAlign ha = col == 0 ? Gw2Ui::HAlign::Left : Gw2Ui::HAlign::Right;
            if (header) txt = heads[col];
            else
            {
                const double kd = s.kdDeaths > 0 ? (double)s.kdKills / (double)s.kdDeaths : (double)s.kdKills;
                switch (col)
                {
                    case 0: txt = s.worldName.empty() ? (c == 0 ? "Red" : c == 1 ? "Blue" : "Green") : s.worldName.c_str(); break;
                    case 1: std::snprintf(b, sizeof(b), "%s", WvwAbbrev(s.score).c_str()); break;
                    case 2: std::snprintf(b, sizeof(b), "%d", s.victoryPoints); break;
                    case 3: std::snprintf(b, sizeof(b), "%d", s.skirmishScore); break;
                    case 4: std::snprintf(b, sizeof(b), "+%lld", s.ppt); break;
                    default: std::snprintf(b, sizeof(b), "%.2f", kd); break;
                }
            }
            const bool bold = !header && col == 0 && (c + 1 == m.wvwMyColor);
            Gw2Ui::LabelDL(dl, ImVec2(x + 3.f, p.y), ImVec2(x + cw - 3.f, p.y + rh), txt, ha, Gw2Ui::VAlign::Middle, tcol, bold, nullptr, header ? 12.f : 14.f);
            x += cw;
        }
    };
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, rh)); drawRow(p, true, 0);
    for (int i = 0; i < 3; ++i)
    {
        const int c = order[i];
        p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, rh));
        if ((i & 1) == 0)   // explicit zebra (the textured RowBackground stripe is invisible over near-black)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), IM_COL32(255, 255, 255, 12), 0.f);
        Gw2Ui::RowBackground(p, ImVec2(p.x + w, p.y + rh), false, c + 1 == m.wvwMyColor, ImGui::GetID(&m.wvwSides[c]), i);
        drawRow(p, false, c);
    }
    DashApi::Freshness(m.wvwMatchAt);
}

void DashW::WvwCurrentMap(App& app, float w)
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const AccountData::Model& m = AccountData::Get();
    const AccountData::WvwMapState* cur = nullptr;
    if (m.haveWvwMatch) { const uint32_t mid = CurrentMapId(); for (const auto& ms : m.wvwMaps) if ((uint32_t)ms.mapId == mid) { cur = &ms; break; } }

    if (!m.haveWvwMatch)      WvwNote(AccountData::WvwLoadStage(), w);
    else if (!cur)           WvwNote(IsInWvW() ? "Map data loading..." : "Not in a WvW map.", w);
    else
    {
        WvwMapHeader(WvwMapLabel(cur->mapType), cur->mapType, true, w);
        ImGui::Spacing();
        const int mode = std::clamp(app.config.wvwCurMode, 0, 2);   // 0 Map+data, 1 Map only, 2 Data only
        if (mode != 2) { const float mapH = std::min(w * 0.62f, DashUtil::Narrow(w) ? 120.f : 180.f); const ImVec2 p = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(w, mapH)); DrawWvwMap(*cur, p, ImVec2(w, mapH)); }
        if (mode != 1) { ImGui::Spacing(); DrawWvwData(*cur, app.config, w); }
    }

    // Right-click anywhere on the widget -> display mode + data fields.
    const float endY = ImGui::GetCursorScreenPos().y;
    std::vector<Gw2Ui::MenuNode> nodes;
    { const Config& cfg = app.config;
      Gw2Ui::MenuNode a; a.label = "Map + data"; a.id = 0; a.selected = cfg.wvwCurMode == 0; nodes.push_back(a);
      Gw2Ui::MenuNode b; b.label = "Map only";   b.id = 1; b.selected = cfg.wvwCurMode == 1; nodes.push_back(b);
      Gw2Ui::MenuNode c; c.label = "Data only";  c.id = 2; c.selected = cfg.wvwCurMode == 2; nodes.push_back(c);
      Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep);
      WvwDataNodes(cfg, nodes, 10); }
    const int picked = WvwMenu("##wvwCurMenu", p0, w, std::max(endY, p0.y + 20.f), nodes);
    if (picked >= 0) { bool d = false; if (picked <= 2) { app.config.wvwCurMode = picked; d = true; } else d = WvwDataDispatch(app.config, picked, 10); if (d) app.settingsDirty = true; }
    if (m.haveWvwMatch) DashApi::Freshness(m.wvwMatchAt);
}

void DashW::WvwMaps(App& app, float w)
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWvwMatch) WvwNote(AccountData::WvwLoadStage(), w);
    else
    {
        const uint32_t mid = CurrentMapId();
        bool anyShown = false;
        for (const auto& ms : m.wvwMaps)
        {
            const int mode = WvwMapModeFor(app.config, ms.mapType);   // 0 Off / 1 Data / 2 Map / 3 Both
            if (mode == 0) continue;
            if (anyShown) { ImGui::Spacing(); Gw2Ui::Divider(w); ImGui::Spacing(); }
            anyShown = true;
            const bool here = (uint32_t)ms.mapId == mid;
            WvwMapHeader(WvwMapLabel(ms.mapType), ms.mapType, here, w);
            ImGui::Spacing();
            if (mode == 2 || mode == 3) { const float mapH = std::min(w * 0.55f, DashUtil::Narrow(w) ? 100.f : 150.f); const ImVec2 p = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(w, mapH)); DrawWvwMap(ms, p, ImVec2(w, mapH)); if (mode == 3) ImGui::Spacing(); }
            if (mode == 1 || mode == 3) DrawWvwData(ms, app.config, w);
        }
        if (!anyShown) WvwNote("No maps enabled (right-click to configure).", w);
    }

    // Right-click anywhere -> per-map mode submenus + data fields.
    const float endY = ImGui::GetCursorScreenPos().y;
    Config& cfg = app.config;
    int* modes[5] = { &cfg.wvwMapEb, &cfg.wvwMapRbl, &cfg.wvwMapBbl, &cfg.wvwMapGbl, &cfg.wvwMapEotm };
    static const char* mapNames[5]  = { "Eternal Battlegrounds", "Red Borderlands", "Blue Borderlands", "Green Borderlands", "Edge of the Mists" };
    static const char* modeNames[4] = { "Off", "Data only", "Map only", "Map + data" };
    std::vector<Gw2Ui::MenuNode> nodes;
    for (int mi = 0; mi < 5; ++mi)
    {
        Gw2Ui::MenuNode sub; sub.label = mapNames[mi];
        for (int md = 0; md < 4; ++md) { Gw2Ui::MenuNode leaf; leaf.label = modeNames[md]; leaf.id = mi * 10 + md; leaf.selected = *modes[mi] == md; sub.children.push_back(leaf); }
        nodes.push_back(sub);
    }
    Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep);
    WvwDataNodes(cfg, nodes, 100);
    const int picked = WvwMenu("##wvwMapsMenu", p0, w, std::max(endY, p0.y + 20.f), nodes);
    if (picked >= 0) { bool d = false; if (picked < 100) { const int mi = picked / 10, md = picked % 10; if (mi < 5 && md < 4) { *modes[mi] = md; d = true; } } else d = WvwDataDispatch(cfg, picked, 100); if (d) app.settingsDirty = true; }
    if (m.haveWvwMatch) DashApi::Freshness(m.wvwMatchAt);
}
