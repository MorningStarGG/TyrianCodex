// Gw2Ui :: GW2 panels, auto-sized cards, the stat-tile grid, reorder-list rows.
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

// A capped, centered "hero" screenshot atop a rich tooltip (decoration / cat / wardrobe-skin cards). Fits the image
// into a regionW x maxH box preserving aspect (so a tall/portrait shot doesn't blow up), centers it, and pins the
// region width with a Dummy so the tooltip reads as a card rather than a narrow left-hugging strip. No-op for a
// null/empty image (the caller then just renders text, or falls back to an icon).
void Gw2Ui::TooltipHeroImage(const Texture_t *img, float regionW, float maxH)
{
    if (!img || !img->Resource || img->Width <= 0 || img->Height <= 0)
        return;
    const float s = std::min(regionW / (float)img->Width, maxH / (float)img->Height);
    const float w = (float)img->Width * s, h = (float)img->Height * s;
    const float x0 = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x0 + (regionW - w) * 0.5f);
    ImGui::Image((ImTextureID)img->Resource, ImVec2(w, h));
    ImGui::SetCursorPosX(x0);
    ImGui::Dummy(ImVec2(regionW, 3.f)); // pin the hero width (true centering) + a small gap before the text
}

// As TooltipHeroImage, but `sheet` is a HORIZONTAL sprite sheet of `cols` frames (build_skin_anim.py flattened an
// animated APNG render). Draws frame `frame`'s UV slice at the FRAME aspect, so an animated wardrobe render plays
// in the tooltip -- ImGui rebuilds the hovered tooltip every frame, so cycling `frame` by a timer animates it.
void Gw2Ui::TooltipHeroImageFrame(const Texture_t *sheet, float regionW, float maxH, int cols, int frame)
{
    if (!sheet || !sheet->Resource || sheet->Width <= 0 || sheet->Height <= 0 || cols <= 0)
        return;
    const float fw = (float)sheet->Width / (float)cols, fh = (float)sheet->Height;
    const float s = std::min(regionW / fw, maxH / fh);
    const float w = fw * s, h = fh * s;
    if (frame < 0)
        frame = 0;
    if (frame >= cols)
        frame = cols - 1;
    const float u0 = (float)frame / (float)cols, u1 = (float)(frame + 1) / (float)cols;
    const float x0 = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x0 + (regionW - w) * 0.5f);
    ImGui::Image((ImTextureID)sheet->Resource, ImVec2(w, h), ImVec2(u0, 0.f), ImVec2(u1, 1.f));
    ImGui::SetCursorPosX(x0);
    ImGui::Dummy(ImVec2(regionW, 3.f));
}

// GW2 bordered panel: header bar + title, corner-accent flourishes (TL flipped
// horizontally, BR flipped vertically), a subtle Black*0.1 tint, then a content child inset by
// padding (L/R 4, T/B 7, header 36). No full rectangle border - the accents + tint give the GW2 look.
bool Gw2Ui::BeginPanel(const char *idAndTitle, float width, float height, const char *title)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float W = (width > 0.f) ? width : avail.x;
    const float H = (height > 0.f) ? height : avail.y;
    const ImVec2 b(a.x + W, a.y + H);

    const float PAD = 0.f, PADV = 7.f, HDR = 36.f;
    const bool titled = title && *title;
    const float topOff = titled ? HDR : PADV;

    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 30)); // Black*0.1 content tint

    // Corner-accent flourishes (1002144, 256px wide source). TL flipped horizontally, BR flipped vertically.
    if (const T ca = Asset(1002144); ca.srv)
    {
        const float cw = W < 256.f ? W : 256.f;
        const float u0 = (256.f - cw) / 256.f;
        const ImU32 ac = IM_COL32(255, 255, 255, 200);
        dl->AddImage((ImTextureID)ca.srv, ImVec2(a.x - 2.f, a.y + topOff - 12.f),
                     ImVec2(a.x - 2.f + cw, a.y + topOff - 12.f + ca.h), ImVec2(1.f, 0.f), ImVec2(u0, 1.f), ac); // TL flipH
        dl->AddImage((ImTextureID)ca.srv, ImVec2(b.x - cw + 2.f, b.y - 59.f),
                     ImVec2(b.x + 2.f, b.y - 59.f + ca.h), ImVec2(u0, 1.f), ImVec2(1.f, 0.f), ac); // BR flipV
    }

    // Header bar + title.
    if (titled)
    {
        if (const T hd = Asset(1032325); hd.srv)
            Img(dl, hd, ImVec2(a.x + PAD, a.y), ImVec2(b.x - PAD, a.y + HDR));
        ImFont *uf = (NexusLink && NexusLink->Font) ? (ImFont *)NexusLink->Font : nullptr;
        const ImU32 tcol = IM_COL32(255, 244, 207, 255);
        if (uf)
            dl->AddText(uf, uf->FontSize, ImVec2(a.x + PAD + 10.f, a.y + 9.f), tcol, title);
        else
            dl->AddText(ImVec2(a.x + PAD + 10.f, a.y + 9.f), tcol, title);
    }

    const ImVec2 cTL(a.x + PAD + 2.f, a.y + topOff);
    const ImVec2 cBR(b.x - PAD, b.y - PADV);
    ImGui::SetCursorScreenPos(cTL);
    ImGui::BeginChild(idAndTitle, ImVec2(cBR.x - cTL.x, cBR.y - cTL.y), false, ImGuiWindowFlags_NoBackground);
    if (titled)
        ImGui::Dummy(ImVec2(0.f, 6.f)); // breathing room under the header before the first content row
    return true;
}

void Gw2Ui::EndPanel()
{
    ImGui::EndChild();
}

// GW2 bordered "card" that auto-sizes to its content. We can't know the height before drawing the content
// (ImGui 1.80 has no auto-resize child), so we draw the content on a SECOND draw-list channel (a group
// measures its extent), then draw the bg/border on the FIRST channel once the height is known, and merge.
namespace
{
    struct GwCard
    {
        ImVec2 p;
        float w;
        ImU32 bg, border, accent;
    };
    static std::vector<GwCard> s_cardStack;
}

bool Gw2Ui::BeginCard(const char *id, float width, ImU32 bg, ImU32 border)
{
    return BeginAccentCard(id, width, 0, bg, border);
}

bool Gw2Ui::BeginAccentCard(const char *id, float width, ImU32 accent, ImU32 bg, ImU32 border)
{
    const float sc = Gw2Ui::TextScale();
    const float PAD = 8.f * sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // Auto-width cards leave a small right margin so they never butt against the panel edge / scrollbar.
    const float W = (width > 0.f) ? width : std::max(40.f, ImGui::GetContentRegionAvail().x - 6.f);
    if (bg == 0)
        bg = IM_COL32(0, 0, 0, 45);
    if (border == 0)
        border = IM_COL32(120, 110, 88, 110);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1); // content draws on the foreground channel
    s_cardStack.push_back({p, W, bg, border, accent});

    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(ImVec2(p.x + PAD + (accent ? 8.f * sc : 0.f), p.y + PAD));
    ImGui::BeginGroup(); // measures the content extent for auto-height
    return true;
}

void Gw2Ui::EndCard()
{
    const float sc = Gw2Ui::TextScale();
    const float PAD = 8.f * sc;
    ImGui::EndGroup();
    ImGui::PopID();

    const GwCard c = s_cardStack.back();
    s_cardStack.pop_back();
    const float H = (ImGui::GetItemRectMax().y - c.p.y) + PAD; // group bottom + the bottom padding
    const ImVec2 b(c.p.x + c.w, c.p.y + H);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0); // background channel (behind the content)
    dl->AddRectFilled(c.p, b, c.bg, 3.f * sc);
    dl->AddRect(c.p, b, c.border, 3.f * sc);
    if (c.accent)
    {
        const ImU32 accentSoft = (c.accent & 0x00FFFFFFu) | ((ImU32)90u << 24);
        dl->AddRectFilled(ImVec2(c.p.x + 4.f * sc, c.p.y + 6.f * sc), ImVec2(c.p.x + 7.f * sc, b.y - 6.f * sc), c.accent, 2.f * sc);
        dl->AddLine(ImVec2(c.p.x + 10.f * sc, c.p.y + 4.f * sc), ImVec2(c.p.x + 10.f * sc, b.y - 4.f * sc),
                    accentSoft, sc);
    }
    dl->ChannelsMerge();

    ImGui::SetCursorScreenPos(c.p);
    ImGui::Dummy(ImVec2(c.w, H)); // re-register the full card as one item (clean flow + scroll extent)
}

float Gw2Ui::CardInnerWidth()
{
    if (s_cardStack.empty())
        return ImGui::GetContentRegionAvail().x;
    const GwCard &c = s_cardStack.back();
    const float sc = Gw2Ui::TextScale();
    return c.w - 16.f * sc - (c.accent ? 8.f * sc : 0.f); // minus 2*PAD(8) (+ accent inset)
}

float Gw2Ui::ContentWidth(float explicitWidth)
{
    return (explicitWidth > 0.f) ? explicitWidth : CardInnerWidth();
}

void Gw2Ui::FiligreeHeading(const char *title, float width)
{
    const float w = ContentWidth(width);
    ImGui::Dummy(ImVec2(0.f, 10.f));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    LabelIn(p, ImVec2(p.x + w, p.y + 26.f), title, HAlign::Center, VAlign::Middle, kGold, true, nullptr, 22.f);
    ImGui::Dummy(ImVec2(w, 26.f));
    const ImVec2 q = ImGui::GetCursorScreenPos();
    Render::DrawZoneRule(dl, ImVec2(p.x + w * 0.5f, q.y + 5.f), w * 0.26f, 11.f, IM_COL32(196, 176, 128, 165), 1.f, false);
    ImGui::Dummy(ImVec2(w, 16.f));
}

bool Gw2Ui::ImageCard(const char *id, const Texture_t *tex, const char *name, const char *caption, float cardW,
                      const std::function<void(float)> &body, ImU32 bg, ImU32 border)
{
    BeginCard(id, cardW, bg, border);
    const float iw = CardInnerWidth();
    const float ih = iw * 0.58f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (tex && tex->Resource)
        dl->AddImage((ImTextureID)tex->Resource, p, ImVec2(p.x + iw, p.y + ih));
    else
        dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + ih), IM_COL32(24, 22, 18, 170), 2.f);
    dl->AddRect(p, ImVec2(p.x + iw, p.y + ih), IM_COL32(120, 110, 88, 120), 2.f);
    ImGui::Dummy(ImVec2(iw, ih));
    if (name && *name)
    {
        ImGui::Spacing();
        const float nameH = MeasureWrappedHeight(name, 20.f, iw - 6.f);
        const ImVec2 np = ImGui::GetCursorScreenPos();
        LabelIn(np, ImVec2(np.x + iw, np.y + nameH), name, HAlign::Center, VAlign::Middle, kGold, true, nullptr, 20.f, iw - 6.f);
        ImGui::Dummy(ImVec2(iw, nameH));
    }
    if (caption && *caption)
    {
        ImGui::Dummy(ImVec2(iw, 2.f));
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        LabelIn(cp, ImVec2(cp.x + iw, cp.y + 18.f), caption, HAlign::Center, VAlign::Middle, kTextSub, false, nullptr, 16.f);
        ImGui::Dummy(ImVec2(iw, 18.f));
    }
    if (body)
    {
        ImGui::Spacing();
        body(iw);
    }
    EndCard();
    return ImGui::IsItemHovered();
}

void Gw2Ui::CardGrid(int count, float targetW, float gap, const std::function<void(int, float)> &drawCell)
{
    if (count <= 0 || !drawCell)
        return;
    const float availW = ImGui::GetContentRegionAvail().x;
    int cols = (std::max)(1, (int)((availW + gap) / (targetW + gap)));
    cols = (std::min)(cols, count);
    const float cardW = FillWidth(availW, cols, gap, 140.f);
    for (int i = 0; i < count; ++i)
    {
        if (i % cols != 0)
            ImGui::SameLine(0.f, gap);
        drawCell(i, cardW);
    }
}

int Gw2Ui::IconGrid(int count, float cell, float gap, const std::function<void(int, ImVec2, float)> &drawCell)
{
    if (count <= 0 || !drawCell)
        return 0;
    const float availW = ImGui::GetContentRegionAvail().x;
    const int cols = (std::max)(1, (int)((availW + gap) / (cell + gap)));
    const int rows = (count + cols - 1) / cols;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGuiListClipper clipper;
    clipper.Begin(rows, cell + gap);
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            for (int col = 0; col < cols; ++col)
            {
                const int idx = row * cols + col;
                if (idx >= count)
                    break;
                if (col)
                    ImGui::SameLine();
                const ImVec2 cmin = ImGui::GetCursorScreenPos();
                drawCell(idx, cmin, cell);
            }
    ImGui::PopStyleVar();
    return cols;
}

void Gw2Ui::StatGrid(float width, const std::vector<Stat> &tiles, float tileH)
{
    if (tiles.empty())
        return;
    PushTextScale(1.f); // a stat tile renders at ONE fixed size EVERYWHERE -- never the caller's ambient text scale
    const float w = (width > 0.f) ? width : ContentWidth(0.f);
    const float gap = 10.f;
    const int cols = w > 620.f ? 3 : (w > 360.f ? 2 : 1);
    const float tileW = (w - gap * (cols - 1)) / (float)cols;
    const int rows = ((int)tiles.size() + cols - 1) / cols;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, rows * tileH + (rows - 1) * gap));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float kLabel = 16.f, kValue = 24.f; // the ONE size for every stat tile (clean 24px-atlas ratios)
    for (int i = 0; i < (int)tiles.size(); ++i)
    {
        const Stat &t = tiles[i];
        const int r = i / cols, c = i % cols;
        const float x = p.x + c * (tileW + gap), y = p.y + r * (tileH + gap);
        const ImVec2 a(x, y), b(x + tileW, y + tileH);
        dl->AddRectFilled(a, b, IM_COL32(26, 28, 24, 205), 5.f);
        dl->AddRect(a, b, IM_COL32(120, 104, 70, 95), 5.f, 0, 1.f);
        dl->AddRectFilled(ImVec2(x + 1.f, y + 2.f), ImVec2(x + 4.f, b.y - 2.f), t.accent); // colored left accent
        float textX = x + 14.f;
        if (t.icon)
        {
            t.icon(dl, ImVec2(x + 26.f, y + tileH * 0.5f), 26.f);
            textX = x + 46.f;
        }
        // A long label wraps to ~2 lines; the label + value block is vertically CENTRED so single-line tiles look
        // balanced (and their values line up across a row) and long labels still fit -- no clipping, no bottom gap.
        const float wrapW = (b.x - 10.f) - textX;
        const float labelH = (std::min)(MeasureWrappedHeight(t.label.c_str(), kLabel, wrapW), kLabel * 2.4f);
        const float blockH = labelH + 4.f + kValue * 1.18f;
        const float top = y + (std::max)(5.f, (tileH - blockH) * 0.5f);
        dl->PushClipRect(a, ImVec2(b.x - 4.f, b.y), true);
        LabelIn(ImVec2(textX, top), ImVec2(b.x - 10.f, top + labelH + 2.f), t.label.c_str(),
                HAlign::Left, VAlign::Top, kTextDim, false, nullptr, kLabel, wrapW);
        LabelIn(ImVec2(textX, top + labelH + 4.f), ImVec2(b.x - 10.f, b.y - 3.f), t.value.c_str(),
                HAlign::Left, VAlign::Top, t.col, false, nullptr, kValue, 0.f, 1.3f);
        dl->PopClipRect();
    }
    PopTextScale();
}

Gw2Ui::ReorderResult Gw2Ui::ReorderRow(const char *id, float x, float width, float height, const ReorderRowDesc &d, float rowTop)
{
    ReorderResult res;
    const float isz = 18.f, sp = 6.f;
    const bool ownRow = rowTop < 0.f; // cell mode (>=0): caller reserves the row + advances
    if (ownRow)
    {
        rowTop = ImGui::GetCursorScreenPos().y;
        ImGui::Dummy(ImVec2(width, height));
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, rowTop), ImVec2(x + width, rowTop + height), IM_COL32(255, 255, 255, 10), 3.f);
    if (d.border)
        dl->AddRect(ImVec2(x, rowTop), ImVec2(x + width, rowTop + height), IM_COL32(120, 108, 82, 90), 3.f);
    const float yC = rowTop + (height - isz) * 0.5f;

    float rx = x + width - 4.f - isz;
    ImGui::PushID(id);
    if (SmallIconButton("##rr_hide", ImVec2(rx, yC), isz, d.disableIcon, false, d.disableTip ? d.disableTip : "Disable"))
        res.act = ReorderResult::Disable;
    rx -= isz + sp;
    if (SmallIconButton("##rr_dn", ImVec2(rx, yC), isz, IconBtn::ArrowDown, false, "Move later") && d.canDown)
        res.act = ReorderResult::Down;
    rx -= isz + sp;
    if (SmallIconButton("##rr_up", ImVec2(rx, yC), isz, IconBtn::ArrowUp, false, "Move earlier") && d.canUp)
        res.act = ReorderResult::Up;
    rx -= isz + sp;

    float midLeft = rx + isz + sp;     // up button's left edge (label ends here when there's no middle control)
    if (d.letters && d.groupCount > 0) // letter group toggle (HUD L/R, Info L/C/R)
    {
        const float per = 22.f, zW = per * d.groupCount, zX = rx - zW;
        midLeft = zX;
        ImGui::SetCursorScreenPos(ImVec2(zX, yC));
        const bool zClick = ImGui::InvisibleButton("##rr_grp", ImVec2(zW, isz));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            Tooltip("Click a letter to move this here");
        }
        if (zClick)
        {
            int nz = (int)((ImGui::GetIO().MousePos.x - zX) / per);
            nz = nz < 0 ? 0 : (nz >= d.groupCount ? d.groupCount - 1 : nz);
            if (nz != d.group)
            {
                res.act = ReorderResult::SetGroup;
                res.toGroup = nz;
            }
        }
        dl->AddRect(ImVec2(zX, yC), ImVec2(zX + zW, yC + isz), IM_COL32(120, 108, 82, 150), 3.f);
        for (int k = 1; k < d.groupCount; ++k)
            dl->AddLine(ImVec2(zX + per * k, yC), ImVec2(zX + per * k, yC + isz), IM_COL32(120, 108, 82, 150));
        for (int k = 0; k < d.groupCount; ++k)
            LabelDL(dl, ImVec2(zX + per * k, yC), ImVec2(zX + per * (k + 1), yC + isz), d.letters[k],
                    HAlign::Center, VAlign::Middle, k == d.group ? kGold : kTextDim, false, nullptr, 14.f);
    }
    else // trailing icon toggles (Dashboard chrome + width)
    {
        for (int e = 0; e < 2; ++e)
        {
            if (!d.extra[e].show)
                continue;
            char eid[16];
            std::snprintf(eid, sizeof(eid), "##rr_x%d", e);
            if (SmallIconButton(eid, ImVec2(rx, yC), isz, d.extra[e].icon, d.extra[e].on, d.extra[e].tip))
                res.act = (e == 0) ? ReorderResult::Extra0 : ReorderResult::Extra1;
            midLeft = rx;
            rx -= isz + sp;
        }
    }
    ImGui::PopID();

    const float labelLeft = x + (d.glyph >= 0 ? 30.f : 10.f);
    float labelRight = midLeft - 6.f;
    if (d.note && *d.note)
    {
        const float noteFs = d.fontSize - 2.f;
        const float noteW = MeasureWidth(d.note, noteFs) + 8.f;
        LabelDL(dl, ImVec2(midLeft - 6.f - noteW, rowTop), ImVec2(midLeft - 6.f, rowTop + height), d.note,
                HAlign::Right, VAlign::Middle, kTextDim, false, nullptr, noteFs);
        labelRight = midLeft - 6.f - noteW - 8.f;
    }
    if (d.glyph >= 0)
        Render::DrawGlyph(dl, ImVec2(x + 16.f, rowTop + height * 0.5f), 16.f, (Render::Glyph)d.glyph, kGold);
    LabelDL(dl, ImVec2(labelLeft, rowTop), ImVec2(labelRight, rowTop + height), d.label,
            HAlign::Left, VAlign::Middle, IM_COL32(220, 212, 190, 255), false, nullptr, d.fontSize);

    if (ownRow)
    {
        ImGui::SetCursorScreenPos(ImVec2(x, rowTop + height));
        ImGui::Spacing();
    }
    return res;
}

bool Gw2Ui::EnableRow(const char *id, float x, float width, float height, const char *label, int glyph, float fontSize)
{
    const float rowTop = ImGui::GetCursorScreenPos().y;
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    const bool add = SmallIconButton("##er_add", ImVec2(x + width - 22.f, rowTop + (height - 18.f) * 0.5f), 18.f, IconBtn::Plus, false, "Enable");
    ImGui::PopID();
    const float labelLeft = x + (glyph >= 0 ? 28.f : 22.f);
    if (glyph >= 0)
        Render::DrawGlyph(dl, ImVec2(x + 14.f, rowTop + height * 0.5f), 15.f, (Render::Glyph)glyph, kTextDim);
    LabelDL(dl, ImVec2(labelLeft, rowTop), ImVec2(x + width - 28.f, rowTop + height), label,
            HAlign::Left, VAlign::Middle, IM_COL32(200, 192, 172, 255), false, nullptr, fontSize);
    ImGui::SetCursorScreenPos(ImVec2(x, rowTop + height));
    ImGui::Spacing();
    return add;
}
