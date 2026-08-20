// Gw2Ui :: GW2 text -- Label family, measurement, text-scale wrappers, tooltips, font setters.
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

void Gw2Ui::LabelIn(ImVec2 a, ImVec2 b, const char* text, HAlign h, VAlign v, ImU32 color, bool stroke, ImFont* font, float fontSize, float wrapWidth, float weight, TextShadow shadowMode, float shadowStrength)
{
    DrawLabelCore(ImGui::GetWindowDrawList(), a, b, text, font, color, stroke, h, v, fontSize, wrapWidth, weight, shadowMode, shadowStrength);
}

void Gw2Ui::LabelDL(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, HAlign h, VAlign v, ImU32 color, bool stroke, ImFont* font, float fontSize, float wrapWidth, float weight, TextShadow shadowMode, float shadowStrength)
{
    if (dl) DrawLabelCore(dl, a, b, text, font, color, stroke, h, v, fontSize, wrapWidth, weight, shadowMode, shadowStrength);
}

ImFont* Gw2Ui::UiFontResolved() { return UiFont(nullptr); }
ImFont* Gw2Ui::StockFont()      { return (NexusLink && NexusLink->Font) ? (ImFont*)NexusLink->Font : ImGui::GetFont(); }

void Gw2Ui::SetGlobalScale(float s) { g_globalScale = std::clamp(s, 0.75f, 2.0f); }
float Gw2Ui::GlobalScale()          { return g_globalScale; }
float Gw2Ui::Scaled(float px)       { return px * g_globalScale; }
float Gw2Ui::Unscaled(float px)     { return px / std::max(0.01f, g_globalScale); }
void  Gw2Ui::PushTextScale(float s) { g_textScale.push_back(s > 0.05f ? s : 1.f); }
void  Gw2Ui::PopTextScale()         { if (!g_textScale.empty()) g_textScale.pop_back(); }
float Gw2Ui::TextScale()            { return CurTextScale(); }

// ---- Tooltips: ONE styled window (Menomonia font, fixed padding, text-scale pinned to 1.0), plain OR rich. --
namespace { constexpr float kTtMaxW = 360.f; float g_ttBase = 20.f; }   // wrap width + the user-set base text px

void Gw2Ui::SetTooltipFontSize(float px) { g_ttBase = (px > 6.f) ? px : 20.f; }

bool Gw2Ui::TooltipBegin()
{
    const float sc = GlobalScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.f * sc, 7.f * sc));
    ImGui::BeginTooltip();
    PushTextScale(1.f);   // a tooltip raised from a scaled dashboard hover must not inherit that scale
    return true;
}

void Gw2Ui::TooltipEnd()
{
    PopTextScale();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

// One wrapped tooltip line in the GW2 font, advancing the ImGui cursor. The box is the text's natural width
// capped at kTtMaxW (short lines -> a tight tooltip); xIndent shifts the text right (bullets).
static void TtLine(const char* text, ImU32 col, float fs, bool bold, float xIndent)
{
    if (!text || !*text) return;
    const float sc = Gw2Ui::TextScale();
    const float maxW = kTtMaxW * sc - xIndent;
    // +4 px so a string that fits on ONE line doesn't wrap on a rounding edge (ImGui word-wrap treats
    // "fits exactly" as "wrap", which split e.g. "Copy waypoint" onto two lines).
    const float tw   = std::min(Gw2Ui::MeasureWidth(text, fs) + 4.f, maxW);
    const float th   = Gw2Ui::MeasureWrappedHeight(text, fs, tw);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(xIndent + tw, th));
    Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), ImVec2(p.x + xIndent, p.y), ImVec2(p.x + xIndent + tw, p.y + th),
                   text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, col, false, nullptr, fs, tw, bold ? 1.2f : -1.f);
}

// Rich lines keep a relative hierarchy around the user's base size: title a touch bigger + bold, muted smaller.
void Gw2Ui::TooltipTitle(const char* text) { TtLine(text, Gw2Ui::kGold, g_ttBase + 2.f, true,  0.f); }   // +2 (even) -- the 24px atlas aliases at odd px (base+1), jittering the title line height
void Gw2Ui::TooltipText(const char* text)  { TtLine(text, IM_COL32(236, 230, 212, 255), g_ttBase,       false, 0.f); }
void Gw2Ui::TooltipMuted(const char* text) { TtLine(text, IM_COL32(168, 159, 138, 255), std::max(11.f, g_ttBase - 2.f), false, 0.f); }
void Gw2Ui::TooltipColored(const char* text, ImU32 col) { TtLine(text, col, g_ttBase, false, 0.f); }

void Gw2Ui::TooltipBullet(const char* text)
{
    if (!text || !*text) return;
    const float fs = g_ttBase, sc = GlobalScale(), indent = 11.f * sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 3.f * sc, p.y + fs * TextScale() * 0.5f + sc), 1.7f * sc, IM_COL32(210, 196, 150, 255));
    TtLine(text, IM_COL32(224, 217, 197, 255), fs, false, indent);
}

void Gw2Ui::TooltipSeparator()
{
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(150, 124, 72, 110));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

void Gw2Ui::Tooltip(const char* text)
{
    if (!text || !*text) return;
    TooltipBegin();
    TooltipText(text);
    TooltipEnd();
}

// Wrapped pixel height of `text` at `fontSize` / `wrapWidth`, measured with the SAME font rung the draw path
// picks (ResolveFace) so a measured wrap count can never drift from the drawn one.
float Gw2Ui::MeasureWrappedHeight(const char* text, float fontSize, float wrapWidth, ImFont* font)
{
    if (!text || !*text) return 0.f;
    const float fs = RequestedPx(fontSize, font);   // SAME snapped size + rung the draw path uses
    ImFont* f = ResolveFace(font, fs);              // -> measure==draw (wrap count cannot drift)
    if (!f) return 0.f;
    return f->CalcTextSizeA(fs, FLT_MAX, wrapWidth, text).y;
}

float Gw2Ui::MeasureWidth(const char* text, float fontSize, ImFont* font)
{
    if (!text || !*text) return 0.f;
    const float fs = RequestedPx(fontSize, font);   // SAME snapped size + rung the draw path uses
    ImFont* f = ResolveFace(font, fs);              // -> measure==draw
    if (!f) return 0.f;
    return f->CalcTextSizeA(fs, FLT_MAX, 0.f, text).x;
}

std::string Gw2Ui::Ellipsize(const std::string& text, float fontSize, float maxWidth, ImFont* font)
{
    if (text.empty() || maxWidth <= 1.f) return std::string();
    if (MeasureWidth(text.c_str(), fontSize, font) <= maxWidth) return text;
    const char* ell = "...";   // ASCII -- Menomonia has no real ellipsis glyph
    if (MeasureWidth(ell, fontSize, font) > maxWidth) return std::string();
    size_t cut = text.size();
    while (cut > 0)
    {
        --cut;
        while (cut > 0 && (((unsigned char)text[cut]) & 0xC0u) == 0x80u) --cut;   // step back over UTF-8 continuation bytes
        std::string out = text.substr(0, cut) + ell;
        if (MeasureWidth(out.c_str(), fontSize, font) <= maxWidth) return out;
    }
    return ell;
}

void Gw2Ui::Label(const char* text, ImU32 color, bool stroke, ImFont* font, float fontSize, float weight, TextShadow shadowMode, float shadowStrength)
{
    // Measure + reserve at the SAME snapped size + rung DrawLabelCore will draw with (we pass `font` through;
    // DrawLabelCore re-resolves it identically) -- so the flowed box matches the glyphs under a PushTextScale
    // and measure==draw. Otherwise the reserved Dummy is wrong and the text/tooltips clip.
    const float fs = RequestedPx(fontSize, font);
    ImFont* f = ResolveFace(font, fs);
    if (!f) return;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.f, text ? text : "");
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    DrawLabelCore(ImGui::GetWindowDrawList(), p, ImVec2(p.x + ts.x, p.y + ts.y),
                  text, font, color, stroke, HAlign::Left, VAlign::Top, fontSize, 0.f, weight, shadowMode, shadowStrength);
    ImGui::Dummy(ImVec2(ts.x, ts.y));   // reserve the text box so ImGui layout flows like ::Text
}

void Gw2Ui::SetGw2Font(ImFont* regular, ImFont* italic)
{
    if (g_gw2Regular != regular || g_gw2Italic != italic) ++g_fontRevision;
    g_gw2Regular = regular;
    g_gw2Italic  = italic;
    // Single-rung ladder = back-compat + the (nullptr,nullptr) revert: the picker then hands this one face to
    // every size (the old single-bake behavior). A real native-size ladder comes from SetGw2FontLadder.
    g_fontLadder.clear();
    if (regular) g_fontLadder.push_back({ regular->FontSize, regular, italic });
}

void Gw2Ui::SetGw2FontLadder(const FontBake* bakes, int count)
{
    ++g_fontRevision;
    g_fontLadder.clear();
    for (int i = 0; i < count; ++i)
        if (bakes[i].regular) g_fontLadder.push_back(bakes[i]);
    std::sort(g_fontLadder.begin(), g_fontLadder.end(),
              [](const FontBake& a, const FontBake& b) { return a.px < b.px; });
    // g_gw2Regular/Italic must be the 24px rung (the default-size + Zone-Display fallback anchor); fall back to
    // the largest rung only if 24 isn't present. These also serve as the "GW2 font" sentinels ResolveFace keys on.
    g_gw2Regular = nullptr; g_gw2Italic = nullptr;
    for (const FontBake& fb : g_fontLadder)
        if ((int)(fb.px + 0.5f) == 24) { g_gw2Regular = fb.regular; g_gw2Italic = fb.italic; }
    if (!g_gw2Regular && !g_fontLadder.empty())
    {
        g_gw2Regular = g_fontLadder.back().regular;
        g_gw2Italic  = g_fontLadder.back().italic;
    }
}

ImFont* Gw2Ui::Gw2Italic() { return g_gw2Italic; }

int     Gw2Ui::FontRevision()  { return g_fontRevision; }
