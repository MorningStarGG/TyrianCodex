// Gw2Ui :: GW2 list rows + trees, chips/pills/badges, progress bars, dividers, layout helpers, icon glyphs.
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiInternal.h"
#include "Shared.h"
#include "render/glyphs/Glyphs.h"
#include "util/Textures.h"
#include "ui/Effect.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Gw2Ui::detail;

// The universal GW2 list-row background: alternating striping (156044 fade over even rows, like
// Menu.PaintBeforeChildren) drawn UNDER the scrolling highlight (the real menuitem.fx shader via
// Fx::ScrollingHighlight; the roller animates 0->1 over 0.5s on hover, =1 when selected). Content-agnostic --
// any row (menu labels, story/zone/checklist rows) calls this for its bounds, hover/selected state, and row
// index (even rows get the stripe).
void Gw2Ui::RowBackground(ImVec2 a, ImVec2 b, bool hovered, bool selected, ImGuiID id, int rowIndex)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();

    if (rowIndex >= 0 && (rowIndex & 1) == 0)
        if (const T fade = Asset(156044); fade.srv)
            dl->AddImage((ImTextureID)fade.srv, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 178));

    static std::unordered_map<ImGuiID, float> s_roll;
    float &r = s_roll[id];
    const float dt = ImGui::GetIO().DeltaTime;
    if (selected)
        r = 1.f;
    else if (hovered)
        r = (r + dt / 0.5f < 1.f) ? r + dt / 0.5f : 1.f;
    else
        r = 0.f;
    Fx::ScrollingHighlight(dl, a, b, r);
    if (r <= 0.f)
        s_roll.erase(id); // drop idle rows so the per-row hover-roll map can't grow unbounded
}

Gw2Ui::RowHotspot Gw2Ui::Row(const char *strId, int rowIndex, float height, float width, bool allowOverlap, bool selected)
{
    RowHotspot h;
    h.min = ImGui::GetCursorScreenPos();
    h.width = ContentWidth(width);
    h.height = height;
    h.clicked = ImGui::InvisibleButton(strId, ImVec2(h.width, h.height));
    h.hovered = ImGui::IsItemHovered();
    if (allowOverlap)
        ImGui::SetItemAllowOverlap();
    // Key the hover-roll off strId in the caller's (per-row) id scope -> unique per row, so the highlight ramps
    // to a full-width fill instead of being reset to ~0 every frame by the other rows sharing one key.
    RowBackground(h.min, ImVec2(h.min.x + h.width, h.min.y + h.height), h.hovered, selected, ImGui::GetID(strId), rowIndex);
    return h;
}

void Gw2Ui::RowLabel(ImDrawList *dl, const RowHotspot &row, float leftOffset, float rightInset, const char *text,
                     HAlign h, VAlign v, ImU32 color, bool stroke, ImFont *font, float fontSize, float wrapWidth, float weight)
{
    if (!dl || row.width <= 0.f || row.height <= 0.f)
        return;
    const float lx = std::clamp(leftOffset, 0.f, row.width);
    const float rx = std::clamp(row.width - std::max(0.f, rightInset), lx, row.width);
    const ImVec2 a(row.min.x + lx, row.min.y);
    const ImVec2 b(row.min.x + rx, row.min.y + row.height);
    dl->PushClipRect(a, b, true);
    LabelDL(dl, a, b, text, h, v, color, stroke, font, fontSize, wrapWidth, weight);
    dl->PopClipRect();
}

ImVec2 Gw2Ui::RowRight(const RowHotspot &row, float itemWidth, float itemHeight, float rightInset)
{
    const float iw = std::max(0.f, itemWidth);
    const float ih = std::max(0.f, itemHeight);
    const float x = row.min.x + std::max(0.f, row.width - std::max(0.f, rightInset) - iw);
    const float y = (ih > 0.f) ? row.min.y + (row.height - ih) * 0.5f : row.min.y;
    return ImVec2(x, y);
}

void Gw2Ui::TextValueRow(const char *label, const char *value, float width, float height, float fontSize,
                         ImU32 labelColor, ImU32 valueColor, bool labelStroke, bool valueStroke,
                         float rightInset, float valuePad)
{
    const char *safeLabel = label ? label : "";
    const char *safeValue = value ? value : "";
    const float sc = TextScale();
    const float w = ContentWidth(width);
    const float rowH = (height > 0.f) ? std::max(height, fontSize + 4.f) * sc : (fontSize + 4.f) * sc;
    const float inset = std::max(0.f, rightInset) * sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, rowH));
    RowHotspot row{p, w, rowH, false, false};
    const float valueW = safeValue[0] ? (MeasureWidth(safeValue, fontSize) + std::max(0.f, valuePad) * sc) : 0.f;
    RowLabel(ImGui::GetWindowDrawList(), row, 0.f, valueW + inset, safeLabel,
             HAlign::Left, VAlign::Middle, labelColor, labelStroke, nullptr, fontSize);
    RowLabel(ImGui::GetWindowDrawList(), row, 0.f, inset, safeValue,
             HAlign::Right, VAlign::Middle, valueColor, valueStroke, nullptr, fontSize);
}

// Immediate-mode tree row (reusable; see header). Structural chrome only - the caller paints content into
// the returned `contentMin..rowMax` band and region-routes a click via `clickX`.
Gw2Ui::TreeRowResult Gw2Ui::TreeRow(const char *id, int depth, float height, int arrowState, bool selected, int rowIndex)
{
    TreeRowResult r;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    r.rowMin = p;
    r.rowMax = ImVec2(p.x + w, p.y + height);
    const float indent = 6.f + depth * 14.f;
    r.contentMin = ImVec2(p.x + indent + (arrowState >= 0 ? 22.f : 8.f), p.y);

    r.clicked = ImGui::InvisibleButton(id, ImVec2(w, height));
    r.hovered = ImGui::IsItemHovered();
    r.clickX = r.clicked ? (ImGui::GetIO().MousePos.x - p.x) : -1.f;

    RowBackground(r.rowMin, r.rowMax, r.hovered, selected, ImGui::GetID(id), rowIndex);

    if (arrowState >= 0)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + indent + 9.f, p.y + height * 0.5f);
        const ImU32 col = IM_COL32(235, 230, 215, 255);
        Render::DrawGlyph(dl, c, 17.f, arrowState == 1 ? Render::Glyph::CaretDown : Render::Glyph::CaretRight, col, {false, false, false});
    }
    return r;
}

// Paint-only GW2 checkbox (the owning tree row handles the click). state 0 empty / 1 checked / 2 partial.
void Gw2Ui::PaintCheckbox(ImVec2 tl, float size, int state, bool hovered)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    char path[96];
    std::snprintf(path, sizeof(path), "data\\textures\\ui\\checkbox\\cb-%s%s.png",
                  state == 1 ? "checked" : "unchecked", hovered ? "-active" : "");
    if (const T cb = File(path); cb.srv)
        Img(dl, cb, tl, ImVec2(tl.x + size, tl.y + size));
    if (state == 2) // partial: a gold dash over the unchecked box
    {
        const float m = size * 0.30f, cy = tl.y + size * 0.5f;
        dl->AddRectFilled(ImVec2(tl.x + m, cy - 1.5f), ImVec2(tl.x + size - m, cy + 1.5f), IM_COL32(255, 221, 138, 255), 1.f);
    }
}

// GW2 menu/list row: RowBackground + a left-padded label (10px), vertically centered.
bool Gw2Ui::MenuItem(const char *label, bool selected, float height, int rowIndex)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height));

    RowBackground(p, ImVec2(p.x + w, p.y + height), ImGui::IsItemHovered(), selected, ImGui::GetID(label), rowIndex);
    // MenuItem text: DefaultFont16, stroked, left/middle, 10px left padding.
    LabelIn(ImVec2(p.x + 10.f, p.y), ImVec2(p.x + w, p.y + height), label,
            HAlign::Left, VAlign::Middle, IM_COL32(255, 255, 255, 255), /*stroke*/ true);
    return clicked;
}
void Gw2Ui::SmallIconGlyph(ImDrawList *dl, ImVec2 at, float size, IconBtn kind, bool on)
{
    const ImU32 col = (kind == IconBtn::Trash || kind == IconBtn::Minus) ? (on ? IM_COL32(255, 150, 140, 255) : IM_COL32(168, 150, 110, 220))
                      : (kind == IconBtn::Grip)                          ? (on ? kGold : IM_COL32(150, 140, 116, 255))
                                                                         : (on ? kGold : IM_COL32(168, 150, 110, 230));
    Render::Glyph glyph = Render::Glyph::Grip;
    switch (kind)
    {
    case IconBtn::Grip:
        glyph = Render::Glyph::Grip;
        break;
    case IconBtn::Trash:
        glyph = Render::Glyph::Trash;
        break;
    case IconBtn::ArrowUp:
        glyph = Render::Glyph::ArrowUp;
        break;
    case IconBtn::ArrowDown:
        glyph = Render::Glyph::ArrowDown;
        break;
    case IconBtn::Plus:
        glyph = Render::Glyph::Plus;
        break;
    case IconBtn::Minus:
        glyph = Render::Glyph::Minus;
        break;
    case IconBtn::Pencil:
        glyph = Render::Glyph::Pencil;
        break;
    case IconBtn::Duplicate:
        glyph = Render::Glyph::Duplicate;
        break;
    case IconBtn::ChromeBar:
        glyph = Render::Glyph::ChromeBar;
        break;
    case IconBtn::ChromeHover:
        glyph = Render::Glyph::ChromeHover;
        break;
    case IconBtn::WidthFull:
        glyph = Render::Glyph::WidthFull;
        break;
    case IconBtn::WidthHalf:
        glyph = Render::Glyph::WidthHalf;
        break;
    }
    Render::DrawGlyph(dl, ImVec2(at.x + size * 0.5f, at.y + size * 0.5f), size, glyph, col,
                      Render::GlyphStyle{});
}

bool Gw2Ui::SmallIconButton(const char *id, ImVec2 at, float size, IconBtn kind, bool activeAccent, const char *tip)
{
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton("##sib", ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    SmallIconGlyph(ImGui::GetWindowDrawList(), at, size, kind, hov || activeAccent);
    if (hov && tip && *tip)
        Tooltip(tip);
    return clicked;
}

void Gw2Ui::GuideGlyph(ImDrawList *dl, ImVec2 c, ImU32 col)
{
    Render::DrawGlyph(dl, c, 20.f, Render::Glyph::Guide, col, Render::GlyphStyle{});
}
float Gw2Ui::FillWidth(float availW, int count, float gap, float minW)
{
    const int n = (count < 1) ? 1 : count;
    return (std::max)(minW, std::floor((availW - gap * (float)(n - 1)) / (float)n));
}

float Gw2Ui::FillWidth(float availW, int count, float fixedTail, float gap, float minW)
{
    return FillWidth((std::max)(0.f, availW - fixedTail), count, gap, minW);
}

void Gw2Ui::SameLineRightCluster(float clusterW, float gap)
{
    ImGui::SameLine();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > clusterW + gap)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - clusterW));
}

void Gw2Ui::Divider(float width, ImU32 color)
{
    const float w = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
    if (w <= 2.f)
    {
        ImGui::Dummy(ImVec2(w, 12.f));
        return;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + 5.f, half = w * 0.5f;
    const ImU32 end = color & 0x00FFFFFFu; // same RGB, alpha 0 -> fades out at both ends
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(ImVec2(p.x, y), ImVec2(p.x + half, y + 2.f), end, color, color, end);
    dl->AddRectFilledMultiColor(ImVec2(p.x + half, y), ImVec2(p.x + w, y + 2.f), color, end, end, color);
    ImGui::Dummy(ImVec2(w, 12.f));
}

void Gw2Ui::EmptyState(const char *title, const char *hint)
{
    const float sc = TextScale();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 40.f)
        return;

    constexpr float kTitleFs = 22.f, kHintFs = 16.f;
    const float lensR = 15.f * sc;                    // magnifier lens radius
    const float handle = 9.f * sc;                    // handle length past the ring
    const float glyphH = 2.f * lensR + handle * 0.7f; // glyph block height (handle drops down-right)
    const float gap1 = 12.f * sc;                     // glyph -> title
    const float gap2 = 4.f * sc;                      // title -> hint
    const float titleH = MeasureWrappedHeight(title, kTitleFs, avail.x);
    const float hintH = (hint && hint[0]) ? MeasureWrappedHeight(hint, kHintFs, avail.x) : 0.f;

    const float blockH = glyphH + gap1 + titleH + (hintH > 0.f ? gap2 + hintH : 0.f);
    const float top = origin.y + std::max(0.f, (avail.y - blockH) * 0.5f);
    const float cx = origin.x + avail.x * 0.5f;

    // Faded gold magnifier (procedural -- no art asset): a ring + a 45-degree down-right handle.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 glyphCol = Alpha(kGold, 90);
    const ImVec2 lensC(cx - 3.f * sc, top + lensR);
    dl->AddCircle(lensC, lensR, glyphCol, 30, 2.6f * sc);
    const float d = 0.70710678f;
    dl->AddLine(ImVec2(lensC.x + lensR * d, lensC.y + lensR * d),
                ImVec2(lensC.x + (lensR + handle) * d, lensC.y + (lensR + handle) * d), glyphCol, 3.0f * sc);

    float ty = top + glyphH + gap1;
    LabelIn(ImVec2(origin.x, ty), ImVec2(origin.x + avail.x, ty + titleH), title,
            HAlign::Center, VAlign::Top, kTextSub, false, nullptr, kTitleFs, avail.x, 1.4f);
    if (hintH > 0.f)
    {
        ty += titleH + gap2;
        LabelIn(ImVec2(origin.x, ty), ImVec2(origin.x + avail.x, ty + hintH), hint,
                HAlign::Center, VAlign::Top, kTextDim, false, nullptr, kHintFs, avail.x);
    }
}

void Gw2Ui::PanelChrome(ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, ImU32 border, bool ornate)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float r = 3.f; // match the window rounding so the frame's corners are rounded, not poking
    // Rounded base (the darker bottom color) gives the rounded corners; the vertical gradient is inset by r so
    // its sharp corners hide inside the rounded base -- AddRectFilledMultiColor can't itself be rounded.
    dl->AddRectFilled(a, b, bottom, r);
    if (b.x - a.x > 2.f * r && b.y - a.y > 2.f * r)
        dl->AddRectFilledMultiColor(ImVec2(a.x + r, a.y + r), ImVec2(b.x - r, b.y - r), top, top, bottom, bottom);
    if (ornate)
    {
        dl->AddRectFilledMultiColor(ImVec2(a.x + r, a.y + r), ImVec2(b.x - r, a.y + 48.f),
                                    IM_COL32(255, 220, 140, 10), IM_COL32(255, 220, 140, 4),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 30));
        dl->AddRect(ImVec2(a.x + 2.f, a.y + 2.f), ImVec2(b.x - 2.f, b.y - 2.f),
                    IM_COL32(255, 225, 160, 20), 2.f, 0, 1.f);
        dl->AddRect(ImVec2(a.x + 3.f, a.y + 3.f), ImVec2(b.x - 3.f, b.y - 3.f),
                    IM_COL32(0, 0, 0, 104), 1.f, 0, 1.f);
    }
    dl->AddRect(a, b, border, r, 0, 1.f);
}

void Gw2Ui::ProgressBar(float fraction, const char *label, float width, float height, ImU32 fill,
                        uint32_t iconAssetId, BarText placement, float textFs)
{
    const float w = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
    fraction = std::clamp(fraction, 0.f, 1.f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const float sc = TextScale();
    const bool haveLabel = label && *label;
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", (int)std::lround(fraction * 100.f));
    const float capFs = (textFs > 0.f) ? textFs : 15.f;
    const float pctFs = (textFs > 0.f) ? std::max(11.f, textFs - 2.f) : 13.f;
    const float capH = (capFs + 3.f) * sc;

    // The caption row (caller's label left, % right, optional type icon) -- used by both Above and Below so the
    // text reads the same wherever it sits. Nothing overlaps the bar in these modes, so it stays clean + legible.
    auto drawCaption = [&](float y)
    {
        float labelX = p0.x;
        if (iconAssetId) // a type icon to the left of the caption (e.g. the Zone Progress objective icons)
        {
            const Texture_t *t = Tex::GetAssetTex(iconAssetId);
            if (t && t->Resource)
                dl->AddImage((ImTextureID)t->Resource, ImVec2(p0.x, y), ImVec2(p0.x + capH, y + capH));
            labelX = p0.x + capH + 6.f * sc;
        }
        if (haveLabel)
            LabelIn(ImVec2(labelX, y), ImVec2(p0.x + w - 42.f * sc, y + capH), label, HAlign::Left, VAlign::Middle,
                    IM_COL32(234, 228, 208, 255), false, nullptr, capFs, 0.f, 0.7f);
        LabelIn(ImVec2(p0.x, y), ImVec2(p0.x + w, y + capH), pct, HAlign::Right, VAlign::Middle,
                IM_COL32(206, 180, 122, 255), false, nullptr, pctFs);
    };

    float y = p0.y;
    if (placement == BarText::Above && haveLabel)
    {
        drawCaption(y);
        y += capH + 3.f * sc;
    }

    // The bar itself: a recessed dark track + a vertical-gradient fill with a top sheen + a warm rim.
    const float h = std::max(12.f, height) * sc;
    const ImVec2 p(p0.x, y), e(p0.x + w, y + h);
    const float rnd = std::min(h * 0.5f, 5.f * sc);
    const float inset = std::min(2.f * sc, h * 0.33f);
    dl->AddRectFilled(p, e, IM_COL32(6, 8, 7, 220), rnd);
    dl->AddRectFilledMultiColor(p, ImVec2(e.x, p.y + h * 0.55f),
                                IM_COL32(0, 0, 0, 80), IM_COL32(0, 0, 0, 80), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    const float fw = std::max(0.f, (w - inset * 2.f) * fraction);
    if (fw > 1.f)
    {
        const ImVec2 fa(p.x + inset, p.y + inset);
        const ImVec2 fb(p.x + inset + fw, e.y - inset);
        const ImU32 fillTop = (fill & 0x00FFFFFFu) | ((ImU32)255u << 24);
        const ImU32 fillBot = (fill & 0x00FFFFFFu) | ((ImU32)165u << 24);
        dl->AddRectFilledMultiColor(fa, fb, fillTop, fillTop, fillBot, fillBot);
        dl->AddLine(ImVec2(fa.x + 1.f * sc, fa.y + 1.f * sc), ImVec2(fb.x - 1.f * sc, fa.y + 1.f * sc),
                    IM_COL32(255, 255, 255, 90), std::max(1.f, sc));
    }
    dl->AddRect(p, e, IM_COL32(150, 124, 70, 185), rnd, 0, 1.2f * sc); // warm rim

    // OnBar: overlay the count + % centered on the fill, sized to the bar height (so it isn't tiny on a tall
    // bar), stroked so it stays readable over the bright fill.
    if (placement == BarText::OnBar)
    {
        char on[48];
        if (haveLabel)
            std::snprintf(on, sizeof(on), "%s   %s", label, pct);
        else
            std::snprintf(on, sizeof(on), "%s", pct);
        const float onFs = (textFs > 0.f) ? std::min(textFs, std::max(12.f, height) * 0.72f)
                                          : std::clamp(std::max(12.f, height) * 0.66f, 13.f, 17.f);
        LabelIn(p, e, on, HAlign::Center, VAlign::Middle, IM_COL32(245, 240, 224, 255), true, nullptr, onFs);
    }

    float bottom = e.y;
    if (placement == BarText::Below && haveLabel)
    {
        drawCaption(e.y + 3.f * sc);
        bottom = e.y + 3.f * sc + capH;
    }

    ImGui::Dummy(ImVec2(w, (bottom - p0.y) + 6.f * sc));
}

void Gw2Ui::ProgressBreakdown(const ProgressSeg *segs, int segCount,
                              const char *totalLabel, int totalHave, int totalTotal, float width)
{
    const float w = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
    const float sc = TextScale();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float fs = 16.f * sc; // clean 24px-atlas downscale (0.67); 15/13 px alias ("small/wavy")
    const float capH = fs + 3.f;
    const float barH = 10.f * sc;
    const float rowH = capH + 2.f + barH;
    const float gap = 10.f;

    // one labeled bar (caption "label  have / total" + right "P%" over a recessed track + gradient fill + rim) --
    // the same recipe as ProgressBar, drawn at an explicit x/y so the segments can sit side by side.
    auto bar = [&](float x, float y, float bw, const char *label, int have, int total, ImU32 fill)
    {
        const float frac = total > 0 ? std::clamp((float)have / (float)total, 0.f, 1.f) : 0.f;
        char cap[80];
        std::snprintf(cap, sizeof(cap), "%s  %d / %d", label ? label : "", have, total);
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%d%%", (int)std::lround(frac * 100.f));
        LabelDL(dl, ImVec2(x, y), ImVec2(x + bw - 38.f, y + capH), cap, HAlign::Left, VAlign::Middle,
                IM_COL32(234, 228, 208, 255), false, nullptr, fs);
        LabelDL(dl, ImVec2(x, y), ImVec2(x + bw, y + capH), pct, HAlign::Right, VAlign::Middle,
                IM_COL32(206, 180, 122, 255), false, nullptr, fs);
        const float by = y + capH + 2.f;
        const ImVec2 p(x, by), e(x + bw, by + barH);
        const float rnd = std::min(barH * 0.5f, 5.f);
        dl->AddRectFilled(p, e, IM_COL32(6, 8, 7, 220), rnd);
        const float fw = std::max(0.f, (bw - 4.f) * frac);
        if (fw > 1.f)
        {
            const ImVec2 fa(p.x + 2.f, p.y + 2.f), fb(p.x + 2.f + fw, e.y - 2.f);
            const ImU32 ft = (fill & 0x00FFFFFFu) | ((ImU32)255u << 24);
            const ImU32 fb2 = (fill & 0x00FFFFFFu) | ((ImU32)165u << 24);
            dl->AddRectFilledMultiColor(fa, fb, ft, ft, fb2, fb2);
            dl->AddLine(ImVec2(fa.x + 1.f, fa.y + 1.f), ImVec2(fb.x - 1.f, fa.y + 1.f), IM_COL32(255, 255, 255, 90), 1.f);
        }
        dl->AddRect(p, e, IM_COL32(150, 124, 70, 185), rnd, 0, 1.2f);
    };

    if (segCount > 0)
    {
        const float segW = (w - gap * (segCount - 1)) / (float)segCount;
        for (int i = 0; i < segCount; ++i)
            bar(o.x + i * (segW + gap), o.y, segW, segs[i].label, segs[i].have, segs[i].total, segs[i].fill);
    }
    const float ty = o.y + rowH + 8.f;
    bar(o.x, ty, w, totalLabel, totalHave, totalTotal, kGold);
    ImGui::Dummy(ImVec2(w, (ty + rowH - o.y) + 6.f));
}

float Gw2Ui::PillWidth(const char *text, float fontSize, float padX)
{
    // padX total (default 24 = 12/side) so the centered text has the same horizontal breathing room as the
    // dist/ETA Chips -- the old 18 (9/side) read noticeably tighter next to them. Scales with TextScale.
    return (text && *text) ? MeasureWidth(text, fontSize) + padX * TextScale() : 0.f;
}

float Gw2Ui::PillHeight(float fontSize, float padY, float minH)
{
    // Vertical sizing derives from the font (default fontSize+8 as Chip -> ~4px top/bottom), so every pill has
    // the same vertical padding regardless of caller -- the heights used to be hardcoded per site (20/18/24),
    // which made larger-font pills (the step-row "Lv") look cramped next to the chips. Scales with TextScale.
    return std::max(minH, fontSize + padY) * TextScale();
}

void Gw2Ui::PillAt(ImDrawList *dl, ImVec2 topLeft, const char *text, float fontSize,
                   ImU32 border, ImU32 textCol, ImU32 fill, float padX, float padY, float minH)
{
    if (!dl || !text || !*text)
        return;
    const float h = PillHeight(fontSize, padY, minH);
    const ImVec2 b(topLeft.x + PillWidth(text, fontSize, padX), topLeft.y + h);
    const float r = h * 0.5f;
    dl->AddRectFilled(topLeft, b, fill, r);
    dl->AddRect(topLeft, b, border, r);
    LabelDL(dl, topLeft, b, text, HAlign::Center, VAlign::Middle, textCol, false, nullptr, fontSize);
}

float Gw2Ui::Chip(const char *text, ImU32 textCol, ImU32 accent, float fontSize)
{
    if (!text || !*text)
        return 0.f;
    const float h = std::max(19.f, fontSize + 8.f);
    // Horizontal padding clears the fully-rounded ends (radius h/2) so the text isn't crammed against the
    // curve; the text region equals the measured width exactly (the old +17 left it 2px short -> clipped).
    const float padL = 14.f, padR = 12.f;
    const float w = MeasureWidth(text, fontSize) + padL + padR;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(10, 9, 7, 170), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), (accent & 0x00FFFFFFu) | ((ImU32)145u << 24), h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + 8.f, p.y + h * 0.5f), 2.f, accent);
    LabelIn(ImVec2(p.x + padL, p.y), ImVec2(p.x + w - padR, p.y + h), text,
            HAlign::Left, VAlign::Middle, textCol, true, nullptr, fontSize);
    ImGui::Dummy(ImVec2(w, h));
    return w;
}

bool Gw2Ui::SelectableChip(const char *id, bool selected, ImVec2 size, const char *label, float fontSize,
                           const char *tooltip, const std::function<void(ImDrawList *, ImVec2, float)> &drawIcon)
{
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##chip", size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    // ONE canonical palette (the filter chips were the source of truth; the compact tabs had drifted to a
    // slightly higher alpha + radius 4 -- unified here).
    const ImU32 bg = selected ? IM_COL32(72, 52, 22, 200) : (hov ? IM_COL32(34, 31, 24, 190) : IM_COL32(5, 7, 7, 142));
    const ImU32 br = selected ? IM_COL32(210, 158, 70, 192) : IM_COL32(118, 96, 55, hov ? 128 : 71);
    dl->AddRectFilled(p, b, bg, 3.f);
    dl->AddRect(p, b, br, 3.f, 0, selected ? 1.4f : 1.f);
    if (drawIcon)
    {
        const float is = std::min(size.y - 8.f, 22.f);
        drawIcon(dl, ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f), is);
    }
    else if (label && *label)
        LabelIn(p, b, label, HAlign::Center, VAlign::Middle,
                selected ? Gw2Ui::kTextSelected : IM_COL32(220, 214, 194, 235), false, nullptr, fontSize);
    if (hov && tooltip && *tooltip)
        Tooltip(tooltip);
    ImGui::PopID();
    return clicked;
}

bool Gw2Ui::ChipRow(const char *id, const ChipItem *items, int count, int *selected,
                    float height, float fontSize, float gap, float minShareW, bool wrap, int minPerRow, int maxPerRow)
{
    if (!items || count <= 0 || !selected)
        return false;
    const float avail = ImGui::GetContentRegionAvail().x;

    if (wrap) // multi-row: fit ~minShareW-wide chips per row (equal-share, clamped to [minPerRow,maxPerRow]), reflow
    {
        int perRow = (int)((avail + gap) / (minShareW + gap));
        if (maxPerRow > 0)
            perRow = std::min(perRow, maxPerRow);
        if (minPerRow > 0)
            perRow = std::max(perRow, minPerRow);
        perRow = std::max(1, std::min(perRow, count));
        const float w = FillWidth(avail, perRow, gap, 56.f); // small floor so a minPerRow-forced narrow row can squish
        ImGui::PushID(id);
        bool changed = false;
        for (int i = 0; i < count; ++i)
        {
            if (i % perRow != 0)
                ImGui::SameLine(0.f, gap);
            ImGui::PushID(i);
            if (SelectableChip("##c", *selected == i, ImVec2(w, height), items[i].label, fontSize, items[i].tooltip, items[i].drawIcon))
                if (*selected != i)
                {
                    *selected = i;
                    changed = true;
                }
            ImGui::PopID();
        }
        ImGui::PopID();
        return changed;
    }

    float fixedTotal = 0.f;
    int flexCount = 0;
    for (int i = 0; i < count; ++i)
    {
        if (items[i].width > 0.f)
            fixedTotal += items[i].width;
        else
            ++flexCount;
    }
    float shareW = minShareW;
    if (flexCount > 0)
    {
        if (fixedTotal <= 0.f)
            shareW = FillWidth(avail, count, gap, minShareW); // all-flex: exact parity with the old hand-rolled rows
        else
        {
            const float rem = avail - fixedTotal - gap * (float)(count - 1);
            shareW = std::max(minShareW, rem / (float)flexCount);
        }
    }
    ImGui::PushID(id);
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        if (i)
            ImGui::SameLine(0.f, gap);
        ImGui::PushID(i);
        const float w = items[i].width > 0.f ? items[i].width : shareW;
        if (SelectableChip("##c", *selected == i, ImVec2(w, height), items[i].label, fontSize, items[i].tooltip, items[i].drawIcon))
            if (!items[i].disabled && *selected != i)
            {
                *selected = i;
                changed = true;
            }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool Gw2Ui::ChipToggle(const char *id, const char *label, bool on, ImVec2 size, float fontSize, const char *tooltip)
{
    return SelectableChip(id, on, size, label, fontSize, tooltip); // a switch: selected == on; the caller flips on click
}

// content-width chip width: an explicit ChipItem.width, else the measured label + padding, else a height-square icon chip.
static float ChipFlowWidth(const Gw2Ui::ChipItem &c, float height, float fontSize)
{
    if (c.width > 0.f)
        return c.width;
    if (c.label && *c.label)
        return Gw2Ui::MeasureWidth(c.label, fontSize) + 16.f;
    return height;
}

bool Gw2Ui::ChipFlow(const char *id, const ChipItem *items, int count, int *selected,
                     float height, float fontSize, float gap)
{
    if (!items || count <= 0 || !selected)
        return false;
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    bool changed = false;
    float x = 0.f;
    bool lineStart = true;
    for (int i = 0; i < count; ++i)
    {
        const float w = ChipFlowWidth(items[i], height, fontSize);
        if (!lineStart)
        {
            if (x + gap + w > availW)
            {
                x = 0.f;
                lineStart = true;
            }
            else
            {
                ImGui::SameLine(0.f, gap);
                x += gap;
            }
        }
        ImGui::PushID(i);
        if (SelectableChip("##c", *selected == i, ImVec2(w, height), items[i].label, fontSize, items[i].tooltip, items[i].drawIcon))
            if (!items[i].disabled && *selected != i)
            {
                *selected = i;
                changed = true;
            }
        ImGui::PopID();
        x += w;
        lineStart = false;
    }
    ImGui::PopID();
    return changed;
}

bool Gw2Ui::ChipFlowMulti(const char *id, const ChipItem *items, int count,
                          const std::function<bool(int)> &isOn, const std::function<void(int)> &onToggle,
                          float height, float fontSize, float gap)
{
    if (!items || count <= 0)
        return false;
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    bool changed = false;
    float x = 0.f;
    bool lineStart = true;
    for (int i = 0; i < count; ++i)
    {
        const float w = ChipFlowWidth(items[i], height, fontSize);
        if (!lineStart)
        {
            if (x + gap + w > availW)
            {
                x = 0.f;
                lineStart = true;
            }
            else
            {
                ImGui::SameLine(0.f, gap);
                x += gap;
            }
        }
        ImGui::PushID(i);
        if (SelectableChip("##c", isOn && isOn(i), ImVec2(w, height), items[i].label, fontSize, items[i].tooltip, items[i].drawIcon))
            if (!items[i].disabled)
            {
                if (onToggle)
                    onToggle(i);
                changed = true;
            }
        ImGui::PopID();
        x += w;
        lineStart = false;
    }
    ImGui::PopID();
    return changed;
}

bool Gw2Ui::CheckPill(const char *id, const char *text, float fontSize, const char *tip)
{
    const float h = std::max(19.f, fontSize + 8.f);
    const bool hasTx = text && *text;
    const float lead = 19.f; // checkmark column when there's text
    const float w = hasTx ? lead + MeasureWidth(text, fontSize) + 8.f : std::max(h, 24.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 green = hov ? IM_COL32(160, 235, 150, 255) : IM_COL32(120, 200, 120, 235);
    const ImU32 bg = hov ? IM_COL32(28, 52, 26, 225) : IM_COL32(14, 26, 12, 180);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), (green & 0x00FFFFFFu) | ((ImU32)165u << 24), h * 0.5f);
    // checkmark
    const float cx = hasTx ? p.x + 10.f : p.x + w * 0.5f;
    const ImVec2 cc(cx, p.y + h * 0.5f + 1.f);
    dl->AddLine(ImVec2(cc.x - 3.6f, cc.y - 0.5f), ImVec2(cc.x - 1.f, cc.y + 2.6f), green, 2.0f);
    dl->AddLine(ImVec2(cc.x - 1.f, cc.y + 2.6f), ImVec2(cc.x + 4.2f, cc.y - 3.6f), green, 2.0f);
    if (hasTx)
        LabelIn(ImVec2(p.x + lead, p.y), ImVec2(p.x + w - 5.f, p.y + h), text, HAlign::Left, VAlign::Middle,
                hov ? IM_COL32(224, 246, 220, 255) : IM_COL32(198, 226, 196, 255), true, nullptr, fontSize);
    if (hov && tip && *tip)
        Tooltip(tip);
    return clicked;
}

void Gw2Ui::SectionHeader(const char *title, const char *rightText, float fontSize, ImU32 titleCol, bool banded)
{
    const float w = CardInnerWidth(); // card-aware (== GetContentRegionAvail outside a card) so the right chip
                                      // + band stay inside a card's border, not at the window edge
    const float h = std::max(22.f, fontSize + 7.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (banded)
        dl->AddRectFilledMultiColor(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + h),
                                    IM_COL32(35, 29, 17, 120), IM_COL32(12, 10, 7, 30),
                                    IM_COL32(12, 10, 7, 0), IM_COL32(35, 29, 17, 40));
    const float rw = (rightText && *rightText) ? MeasureWidth(rightText, fontSize - 2.f) + 18.f : 0.f;
    const float titleR = p.x + std::max(30.f, w - rw - 8.f);
    dl->PushClipRect(ImVec2(p.x, p.y), ImVec2(titleR, p.y + h), true); // a long title clips here instead of bleeding under the right chip
    LabelIn(ImVec2(p.x + 2.f, p.y), ImVec2(titleR, p.y + h), title ? title : "",
            HAlign::Left, VAlign::Middle, titleCol, true, nullptr, fontSize, 0.f, 1.2f);
    dl->PopClipRect();
    if (rightText && *rightText)
    {
        const ImVec2 rp(p.x + w - rw - 8.f, p.y + 2.f); // 8px right margin: accent cards inset left only, so a chip at w would kiss the border
        dl->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + h - 4.f), IM_COL32(0, 0, 0, 110), (h - 4.f) * 0.5f);
        dl->AddRect(rp, ImVec2(rp.x + rw, rp.y + h - 4.f), IM_COL32(179, 145, 74, 120), (h - 4.f) * 0.5f);
        LabelIn(rp, ImVec2(rp.x + rw, rp.y + h - 4.f), rightText, HAlign::Center, VAlign::Middle,
                IM_COL32(238, 229, 205, 255), true, nullptr, fontSize - 2.f);
    }
    ImGui::Dummy(ImVec2(w, h));
    Divider(w, IM_COL32(196, 176, 128, 75));
}

void Gw2Ui::TypeMedallion(uint32_t iconAssetId, ImU32 accent, float size)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, size * 0.5f, IM_COL32(0, 0, 0, 115), 32);
    dl->AddCircle(c, size * 0.5f - 1.f, (accent & 0x00FFFFFFu) | ((ImU32)210u << 24), 32, 2.f);
    dl->AddCircle(c, size * 0.5f - 4.f, IM_COL32(255, 238, 190, 70), 32, 1.f);
    if (iconAssetId)
        if (void *tx = Tex::GetTextureFromAssetId(iconAssetId))
        {
            const float is = size * 0.58f;
            dl->AddImage((ImTextureID)tx, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), ImVec2(c.x + is * 0.5f, c.y + is * 0.5f));
        }
    ImGui::Dummy(ImVec2(size, size));
}

bool Gw2Ui::DataTable(const DataTableDesc &d)
{
    if (d.colCount <= 0 || d.colCount > 32)
        return false;

    // Resolve column widths: each fixed column clamps to its persisted store; the flex column (cols[0],
    // widthStore == null) fills the remainder. Lay out left-to-right from leftPad.
    float cx[32], cw[32];
    float fixed = 0.f;
    for (int i = 0; i < d.colCount; ++i)
        if (d.cols[i].widthStore)
        {
            const float w = std::clamp(*d.cols[i].widthStore, d.cols[i].minW, d.cols[i].maxW);
            *d.cols[i].widthStore = w;
            cw[i] = w;
            fixed += w;
        }
    const float flexW = std::max(d.flexMin, d.availW - d.leftPad - fixed - d.rightReserve);
    {
        float x = d.leftPad;
        for (int i = 0; i < d.colCount; ++i)
        {
            if (!d.cols[i].widthStore)
                cw[i] = flexW;
            cx[i] = x;
            x += cw[i];
        }
    }

    const ImVec2 hp = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    bool changed = false;

    // Draggable borders FIRST: each ~8px handle must claim hover BEFORE the sort buttons (ImGui gives hover to the
    // first-submitted overlapping item), else a resize drag on the edge would fire the sort. The flex column
    // absorbs at the cols[0]|cols[1] edge -- growing its neighbour is capped so flex can't shrink below flexMin.
    for (int i = 0; i + 1 < d.colCount; ++i)
    {
        float *L = d.cols[i].widthStore;     // null when the left side is the flex column
        float *R = d.cols[i + 1].widthStore; // flex is only ever cols[0], so R here is always a fixed column
        if (!R)
            continue; // can't resize INTO a flex column on the right
        char bid[32];
        std::snprintf(bid, sizeof bid, "%s_cb%d", d.id, i);
        const float minL = L ? d.cols[i].minW : 0.f;
        const float maxL = L ? d.cols[i].maxW : 0.f;
        const float maxR = L ? d.cols[i + 1].maxW : (*R + std::max(0.f, flexW - d.flexMin));
        if (ColumnBorder(bid, hp.x + cx[i + 1], hp.y, d.headerH, L, minL, maxL, R, d.cols[i + 1].minW, maxR))
            changed = true;
    }

    // Header row: sortable columns get an invisible button (^/v indicator + hover highlight); the click is handed
    // to the caller's onSort, which owns asc/desc + the actual re-sort.
    for (int i = 0; i < d.colCount; ++i)
    {
        const TableColumn &c = d.cols[i];
        const ImVec2 cp(hp.x + cx[i], hp.y);
        if (c.sortId >= 0)
        {
            ImGui::SetCursorScreenPos(cp);
            char hid[32];
            std::snprintf(hid, sizeof hid, "%s_h%d", d.id, i);
            ImGui::InvisibleButton(hid, ImVec2(cw[i], d.headerH));
            const bool clicked = ImGui::IsItemClicked();
            const bool hov = ImGui::IsItemHovered();
            if (hov)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked && d.onSort)
                d.onSort(c.sortId);
            const bool act = (d.activeSort == c.sortId);
            if (hov)
                dl->AddRectFilled(ImVec2(cp.x + 1.f, cp.y + 2.f), ImVec2(cp.x + cw[i] - 1.f, cp.y + d.headerH - 2.f), IM_COL32(58, 45, 24, 88));
            std::string t = c.header;
            if (act)
                t += d.sortAsc ? "  ^" : "  v";
            LabelDL(dl, ImVec2(cp.x + 2.f, cp.y), ImVec2(cp.x + cw[i] - 2.f, cp.y + d.headerH), t.c_str(),
                    c.align, VAlign::Middle, act ? kGold : (hov ? IM_COL32(220, 210, 186, 255) : kTextSub), false, nullptr, d.headerFs);
        }
        else
            LabelDL(dl, ImVec2(cp.x + 2.f, cp.y), ImVec2(cp.x + cw[i] - 2.f, cp.y + d.headerH), c.header,
                    c.align, VAlign::Middle, kTextDim, false, nullptr, d.headerFs);
    }
    ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + d.headerH));
    dl->AddLine(ImVec2(hp.x, hp.y + d.headerH), ImVec2(hp.x + d.availW, hp.y + d.headerH), IM_COL32(150, 130, 80, 70));

    // Clipped body. The caller renders each row's cells in drawRow from the per-column rects (rc.x/rc.w), and is
    // responsible for its own PushID(stableId) around any per-row widgets/tooltips.
    if (d.bodyChild)
    {
        const std::string cid = std::string(d.id) + "_rows";
        ImGui::BeginChild(cid.c_str(), ImVec2(0.f, 0.f), false);
    }
    const TableRowCols rc{cx, cw, d.colCount, d.availW};
    ImGuiListClipper clipper;
    clipper.Begin(d.rowCount, d.rowH);
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            // Per-row UNIQUE id: Row() keys its InvisibleButton + the hover-roll highlight off ImGui::GetID(strId),
            // which MUST differ per row or the highlight resets every frame (partial fill) + hover/click collide.
            char rid[40];
            std::snprintf(rid, sizeof rid, "%s_r%d", d.id, i);
            const RowHotspot row = Row(rid, i, d.rowH, d.availW);
            if (d.drawRow)
                d.drawRow(i, row, rc);
        }
    if (d.bodyChild)
        ImGui::EndChild();
    return changed;
}
