#include "ui/zonedisplay/ZoneDisplay.h"
#include "app/App.h"
#include "app/SectorData.h"
#include "Shared.h"
#include "ui/Gw2Ui.h"
#include "ui/Effect.h"
#include "ui/dashboard/Notify.h"
#include "render/glyphs/Glyphs.h"
#include "util/Ease.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <imgui.h>
#include <string>

namespace
{
    // UTF-8: byte length of the glyph beginning at a lead byte (zone names can carry accents/apostrophes).
    inline int Utf8Len(unsigned char ch)
    {
        if (ch < 0x80)
            return 1;
        if ((ch >> 5) == 0x6)
            return 2;
        if ((ch >> 4) == 0xE)
            return 3;
        if ((ch >> 3) == 0x1E)
            return 4;
        return 1;
    }
    inline ImU32 WithA(ImU32 col, float a)
    {
        const int base = (int)((col >> 24) & 0xFF);
        const int ai = (int)(base * Ease::Clamp01(a));
        return (col & 0x00FFFFFFu) | ((ImU32)ai << 24);
    }
    // Deterministic 0..1 hash (stable per id, so burn sparks don't jitter frame to frame).
    inline float Hash01(int n)
    {
        n = (n << 13) ^ n;
        const int m = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
        return (float)m / 2147483647.0f;
    }
    inline ImU32 LerpCol(ImU32 a, ImU32 b, float t)
    {
        t = Ease::Clamp01(t);
        const int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
        const int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
        return IM_COL32(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t),
                        ab + (int)((bb - ab) * t), aa + (int)((ba - aa) * t));
    }
}

void ZoneDisplay::Announce(App &app, Kind kind, const std::string &region, const std::string &zone,
                           const std::string &sector)
{
    kind_ = kind;
    region_ = region;
    zone_ = zone;
    sector_ = sector;
    sectorBig_ = (app.config.zdAreaStyle == 1);
    t0_ = ImGui::GetTime();
    active_ = true;

    if (app.config.zdMirrorToast)
    {
        if (kind == Kind::MapEntry)
            Notify::Push(Notify::Kind::Zone, zone, region);
        else if (!sector.empty())
            Notify::Push(Notify::Kind::Zone, sector, zone);
    }
}

void ZoneDisplay::Update(App &app)
{
    const Config &c = app.config;
    if (!c.zdEnabled)
    {
        active_ = false;
        return;
    }
    if (!MumbleLink || !GameIsLive())
        return; // loading / cutscene / char-select: stale state, don't detect

    const uint32_t mid = CurrentMapId();
    if (mid == 0)
        return;

    // Suppression gates (still TRACK the map/sector so we don't fire a stale popup once the gate clears).
    const bool gated =
        (c.zdHideCompetitive && IsInCompetitiveMode()) ||
        (c.zdHideCombat && IsInCombat()) ||
        (c.zdHideMapOpen && IsMapOpen()) ||
        (c.zdRequireFocus && !GameHasFocus());

    const float px = MumbleLink->AvatarPosition.X;
    const float pz = MumbleLink->AvatarPosition.Z;

    // Map change -> the big three-tier banner.
    if (mid != lastMapId_)
    {
        lastMapId_ = mid;
        lastSector_.clear();
        nextSectorCheck_ = 0.0;
        SectorData::EnsureMap(mid);
        const std::string zone = SectorData::MapName(mid);
        const std::string sector = SectorData::SectorAt(mid, px, pz);
        lastSector_ = sector;
        if (c.zdOnMapChange && !gated && !zone.empty())
            Announce(app, Kind::MapEntry, SectorData::RegionName(mid), zone, sector);
        return;
    }

    // Sector crossing (throttled point-in-polygon test).
    if (!c.zdOnAreaChange)
        return;
    const double now = ImGui::GetTime();
    if (now < nextSectorCheck_)
        return;
    nextSectorCheck_ = now + 0.1;

    const std::string sec = SectorData::SectorAt(mid, px, pz);
    if (sec.empty() || sec == lastSector_)
    {
        if (!sec.empty())
            lastSector_ = sec;
        return;
    }
    lastSector_ = sec;
    if (gated)
        return;
    Announce(app, Kind::Sector, SectorData::RegionName(mid), SectorData::MapName(mid), sec);
}

void ZoneDisplay::Reannounce(App &app)
{
    const uint32_t mid = CurrentMapId();
    std::string region, zone, sector;
    if (mid != 0)
    {
        SectorData::EnsureMap(mid);
        region = SectorData::RegionName(mid);
        zone = SectorData::MapName(mid);
        if (MumbleLink)
            sector = SectorData::SectorAt(mid, MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);
    }
    if (zone.empty())
        zone = "Tyria";
    Announce(app, Kind::MapEntry, region, zone, sector);
}

void ZoneDisplay::ReannounceSector(App &app)
{
    const uint32_t mid = CurrentMapId();
    std::string region, zone, sector;
    if (mid != 0)
    {
        SectorData::EnsureMap(mid);
        region = SectorData::RegionName(mid);
        zone = SectorData::MapName(mid);
        if (MumbleLink)
            sector = SectorData::SectorAt(mid, MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);
    }
    if (sector.empty())
        sector = "Area"; // so the preview still shows when not standing in a named area
    if (zone.empty())
        zone = "Tyria";
    Announce(app, Kind::Sector, region, zone, sector);
}

void ZoneDisplay::Draw(App &app)
{
    if (!active_)
        return;
    const Config &c = app.config;
    if (!c.zdEnabled)
    {
        active_ = false;
        return;
    }

    // The one-line area (zdAreaStyle = One line) is a single-line banner with its OWN Entrance/Exit selectors;
    // the map-entry / big-banner uses the main Entrance + Exit. Everything below renders from entStyle/exitStyle
    // (not the raw settings), so both modes share the same machinery.
    const bool full = (kind_ == Kind::MapEntry) || sectorBig_;
    const int entStyle = full ? c.zdAnimStyle : c.zdAreaEntrance;
    const int exitStyle = full ? c.zdExitStyle : c.zdAreaExit;
    const int shineStyle = full ? c.zdShineStyle : c.zdAreaShine; // one-line area has its own shine

    const int exitMode = exitStyle;                         // 0 Fade / 1 Letter fade / 2 Dissolve / 3 Crumble / 4 Burn / 5 Slide
    const bool burnExit = (exitMode == 4 || exitMode == 5); // the whole-banner RTT erosions -- in place, ~2s

    const double now = ImGui::GetTime();
    float enter = (std::max)(0.05f, c.zdEnterMs / 1000.f);
    if (entStyle == 2 || entStyle == 4)
        enter = (std::max)(enter, 1.9f); // Decode/Twist: fade-in (~1.0s) + the burn-swap (~0.9s)
    const float hold = (std::max)(0.0f, c.zdHoldMs / 1000.f);
    // Burn/Slide need ~2s to read; floor them to 1.6s while honouring a longer Exit-time setting.
    const float exit = burnExit ? (std::max)(1.95f, c.zdExitMs / 1000.f) : (std::max)(0.05f, c.zdExitMs / 1000.f); // Fade-out burn ~1.95s
    const float age = (float)(now - t0_);
    if (age >= enter + hold + exit)
    {
        active_ = false;
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 10.f || H < 10.f)
        return;

    // Prefer the large-baked banner font (96px) so the big hero text stays crisp when scaled; fall back while it loads.
    ImFont *font = app.zdFont ? app.zdFont : (app.menoFont ? app.menoFont : Gw2Ui::UiFontResolved());
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    // Timeline: fade-in, hold, exit. The aux fade is snappy (OutQuad) so the banner leaves promptly. The hero's
    // exit is either that fade, a per-letter effect (drawRow), or -- for Burn/Slide -- a whole-banner RTT erosion.
    const float holdEnd = enter + hold;
    const float inT = Ease::Clamp01(age / enter);
    const float outT = Ease::Clamp01((age - holdEnd) / exit);
    const float inA = Ease::SmoothStep(inT);
    const float auxFade = 1.f - Ease::OutQuad(outT); // region / rule / sector / backdrop
    const float baseA = inA * auxFade;
    const float driftY = (burnExit || exitMode == 2) ? 0.f : -Ease::InCubic(outT) * (H * 0.045f); // Burn/Slide/Dissolve stay in place; others lift
    const int rowExit = (exitMode <= 3) ? exitMode : 0;                                           // per-letter exit in drawRow; Burn/Slide are whole-banner
    const bool perGlyphExit = (rowExit >= 1 && rowExit <= 3);                                     // letter fade / glyph dissolve / crumble exit per-letter
    const bool strokeNow = true;                                                                  // the 1px black stroke is always on (off can break the dissolve animations); LabelDL fades it with the fill

    const float ui = Gw2Ui::GlobalScale();
    Gw2Ui::PushTextScale(1.f / std::max(0.001f, ui));
    const float scale = ui * (std::max)(40, (std::min)(220, c.zdScale)) / 100.f;
    const char *heroStr = full ? zone_.c_str() : sector_.c_str(); // the one-line sector IS the single hero line

    const float heroPx = (full ? 54.f : 34.f) * scale; // the one-line sector hero is a touch smaller
    const float regionPx = 19.f * scale;
    const float sectorPx = 27.f * scale;
    // Width = Wide: extra per-letter spacing (proportional to each line's font size). region keeps its 0.30 eyebrow gap.
    const float wideT = (c.zdWidth == 1) ? 0.5f : 0.f;
    const float heroTrack = heroPx * wideT;
    const float regionTrack = regionPx * 0.30f + regionPx * wideT;
    const float sectorTrack = sectorPx * wideT;
    const bool cascade = (entStyle == 0); // Heralded always cascades its hero letters (full banner or one-line sector)
    const float weight = 1.3f;            // the hero zone name is always faux-bold

    const ImU32 heroBase = IM_COL32(c.zdColR, c.zdColG, c.zdColB, 255);
    const ImU32 headBase = IM_COL32((int)(c.zdColR * 0.80f), (int)(c.zdColG * 0.825f), (int)(c.zdColB * 0.973f), 255);
    // Sector follows the configured colour too -- a slightly LIGHTENED tint of it (region is a dimmed variant, hero
    // is full), so the three tiers keep their hierarchy while all honouring zdCol instead of a pinned cream.
    const ImU32 sectorBase = IM_COL32(c.zdColR, c.zdColG, c.zdColB, 255);
    const ImU32 emberCol = IM_COL32(255, 240, 205, 255); // brief flash as a letter burns away (Dissolve)

    // The shared font atlas (every glyph is rasterized here) -- the source texture for the dissolve/crumble burns.
    ImTextureID atlasTex = ImGui::GetIO().Fonts ? ImGui::GetIO().Fonts->TexID : (ImTextureID)0;
    auto decodeCp = [](const char *b, int len) -> unsigned int
    {
        const unsigned int c0 = (unsigned char)b[0];
        if (len >= 4 && c0 >= 0xF0)
            return ((c0 & 0x07) << 18) | (((unsigned char)b[1] & 0x3F) << 12) | (((unsigned char)b[2] & 0x3F) << 6) | ((unsigned char)b[3] & 0x3F);
        if (len >= 3 && c0 >= 0xE0)
            return ((c0 & 0x0F) << 12) | (((unsigned char)b[1] & 0x3F) << 6) | ((unsigned char)b[2] & 0x3F);
        if (len >= 2 && c0 >= 0xC0)
            return ((c0 & 0x1F) << 6) | ((unsigned char)b[1] & 0x3F);
        return c0;
    };

    // -- per-glyph row helpers -------------------------------------------------
    auto rowWidth = [&](const char *s, float fs, float tracking) -> float
    {
        float w = 0.f;
        for (const char *p = s; *p;)
        {
            const int n = Utf8Len((unsigned char)*p);
            char buf[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                buf[k] = p[k];
            buf[k] = '\0';
            w += Gw2Ui::MeasureWidth(buf, fs, font) + tracking;
            p += k;
        }
        return (w > tracking) ? w - tracking : w; // drop the trailing tracking gap
    };
    // A line's ACTUAL glyph extent as top/bottom offsets RELATIVE to its centre (so callers can seat lines + rules
    // by real visual whitespace instead of the looser font cell). Shared by the full banner + the one-line sector.
    auto rowVOff = [&](const char *s, float fs, float &topOff, float &botOff)
    {
        const float gsc = fs / font->FontSize;
        float lo = 1e9f, hi = -1e9f;
        for (const char *p = s; *p;)
        {
            const int n = Utf8Len((unsigned char)*p);
            char b[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                b[k] = p[k];
            b[k] = '\0';
            const ImFontGlyph *g = font->FindGlyph((ImWchar)decodeCp(b, k));
            if (g && g->Visible)
            {
                lo = (std::min)(lo, -fs * 0.5f + g->Y0 * gsc);
                hi = (std::max)(hi, -fs * 0.5f + g->Y1 * gsc);
            }
            p += k;
        }
        if (hi < lo)
        {
            lo = -fs * 0.40f;
            hi = fs * 0.40f;
        }
        topOff = lo;
        botOff = hi;
    };
    // Draw a centered row of glyphs. cascadeAge>=0 staggers them IN. exitMode drives the OUT, staggered L->R by
    // exitT (0..1): 1 = Letter fade (lift + ember flash + fade); 2 = Burn (the letter erodes bottom-up behind a
    // glowing ember front with rising sparks). `stroke` toggles the outline; it's auto-gated off while a glyph
    // is still faint (no black-letter look on the way in).
    auto drawRow = [&](float cx, float yc, const char *s, float fs, ImU32 colBase, float wgt, float tracking,
                       float rowA, float cascadeAge, int exMode, float exitT, bool stroke, float *outL, float *outR)
    {
        const float total = rowWidth(s, fs, tracking);
        float x = cx - total * 0.5f;
        if (outL)
            *outL = x;
        if (outR)
            *outR = x + total;
        // Burn (dissolve) needs the shader + the atlas; fall back to Letter fade if either is unavailable.
        const int em = (exMode == 2 && !(atlasTex && Fx::DissolveReady())) ? 1
                       : (exMode == 3 && !atlasTex)                        ? 1
                                                                           : exMode;
        const float stagger = 0.035f, glyphDur = 0.30f, risePx = fs * 0.5f;
        int i = 0;
        for (const char *p = s; *p; ++i)
        {
            const int n = Utf8Len((unsigned char)*p);
            char buf[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                buf[k] = p[k];
            buf[k] = '\0';
            const float gw = Gw2Ui::MeasureWidth(buf, fs, font);
            float ga = 1.f, gy = 0.f;
            ImU32 gcol = colBase;
            if (cascadeAge >= 0.f)
            {
                const float gp = Ease::Clamp01((cascadeAge - i * stagger) / glyphDur);
                ga = gp;
                gy = (1.f - Ease::OutCubic(gp)) * risePx;
            }
            const float ep = (em > 0 && exitT >= 0.f) ? Ease::Clamp01((exitT - i * 0.05f) / 0.5f) : 0.f;

            if ((em == 2 || em == 3) && ep > 0.001f) // Burn (glyph dissolve) / Crumble (cells)
            {
                if (ep < 0.999f && font)
                {
                    const ImFontGlyph *g = font->FindGlyph((ImWchar)decodeCp(buf, k));
                    if (g && g->Visible)
                    {
                        const float gsc = fs / font->FontSize;
                        const float lineTop = std::floor((yc + driftY) - fs * 0.5f);
                        const ImVec2 q0(x + g->X0 * gsc, lineTop + g->Y0 * gsc);
                        const ImVec2 q1(x + g->X1 * gsc, lineTop + g->Y1 * gsc);
                        if (em == 2) // shader: erode the glyph shape by screen-space noise (flows across the word)
                        {
                            Fx::Dissolve(dl, atlasTex, q0, q1, ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1),
                                         ep, colBase, emberCol, 0.05f, rowA);
                        }
                        else // crumble: clip the glyph into a grid of cells that pop off as embers
                        {
                            const int GX = 4, GY = 5;
                            const float cw = (q1.x - q0.x) / GX, chh = (q1.y - q0.y) / GY;
                            const float uw = (g->U1 - g->U0) / GX, vh = (g->V1 - g->V0) / GY;
                            for (int cyi = 0; cyi < GY; ++cyi)
                                for (int cxi = 0; cxi < GX; ++cxi)
                                {
                                    const float tv = Hash01(i * 53 + (cyi * GX + cxi) * 7 + 1); // this cell's vanish time
                                    const ImVec2 c0(q0.x + cxi * cw, q0.y + cyi * chh);
                                    const ImVec2 c1(c0.x + cw, c0.y + chh);
                                    const ImVec2 cu0(g->U0 + cxi * uw, g->V0 + cyi * vh);
                                    const ImVec2 cu1(cu0.x + uw, cu0.y + vh);
                                    if (ep < tv)
                                        dl->AddImage(atlasTex, c0, c1, cu0, cu1, WithA(colBase, rowA));
                                    else
                                    {
                                        const float a2 = ep - tv;
                                        if (a2 < 0.4f)
                                        {
                                            const float dy = a2 * fs * 1.4f;
                                            dl->AddImage(atlasTex, ImVec2(c0.x, c0.y - dy), ImVec2(c1.x, c1.y - dy),
                                                         cu0, cu1, WithA(emberCol, rowA * (1.f - a2 / 0.4f)));
                                        }
                                    }
                                }
                        }
                    }
                }
                x += gw + tracking;
                p += k;
                continue;
            }

            if (em == 1 && ep > 0.f) // Letter fade: lift + ember flash + fade
            {
                ga *= (1.f - ep);
                gy -= ep * fs * 0.7f;
                const float flare = (ep < 0.5f) ? ep * 2.f : (1.f - ep) * 2.f;
                gcol = LerpCol(colBase, emberCol, flare * 0.6f);
            }

            const ImU32 col = WithA(gcol, rowA * ga);
            const float yy = yc + gy + driftY;
            Gw2Ui::LabelDL(dl, ImVec2(x, yy - fs), ImVec2(x + gw + 2.f, yy + fs), buf,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, col, stroke, font, fs, 0.f, wgt);
            x += gw + tracking;
            p += k; // advance to the next glyph (the loop header's ++i only counts; p must move)
        }
    };
    // A moving per-letter highlight: the "Letter glint" shine (and the draw-list fallback for the GPU sweeps).
    // Only the letters near the sweep front brighten, so it reads as a glint ON the metal, not a rectangle wash.
    auto glint = [&](const char *s, float yc, float fs, float left, float sweepX, float sigma, float peak, float track)
    {
        float x = left;
        for (const char *p = s; *p;)
        {
            const int n = Utf8Len((unsigned char)*p);
            char buf[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                buf[k] = p[k];
            buf[k] = '\0';
            const float gw = Gw2Ui::MeasureWidth(buf, fs, font);
            const float d = (x + gw * 0.5f) - sweepX;
            const float a = peak * std::exp(-(d * d) / (2.f * sigma * sigma));
            if (a > 0.012f)
                Gw2Ui::LabelDL(dl, ImVec2(x, yc - fs + driftY), ImVec2(x + gw + 2.f, yc + fs + driftY), buf,
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, WithA(IM_COL32(255, 250, 235, 255), a),
                               false, font, fs, 0.f, weight);
            x += gw + track; // match the row's letter spacing (Wide)
            p += k;
        }
    };
    // Backdrop primitives: an L/R-feathered band (hard top/bottom -- the original look), and an all-sides
    // feathered vignette blob (peaks dark at the centre, fades to transparent on every edge -- four bilinear quads).
    auto softBandLR = [&](float left, float right, float top, float bottom, float a)
    {
        const ImU32 mid = WithA(IM_COL32(8, 8, 10, 255), a), end = IM_COL32(8, 8, 10, 0);
        const float m = (left + right) * 0.5f;
        dl->AddRectFilledMultiColor(ImVec2(left, top), ImVec2(m, bottom), end, mid, mid, end);
        dl->AddRectFilledMultiColor(ImVec2(m, top), ImVec2(right, bottom), mid, end, end, mid);
    };
    auto vignette = [&](float cxc, float yc, float halfW, float halfH, float a)
    {
        const ImU32 mid = WithA(IM_COL32(8, 8, 10, 255), a), end = IM_COL32(8, 8, 10, 0);
        const float L = cxc - halfW, R = cxc + halfW, T = yc - halfH, B = yc + halfH;
        dl->AddRectFilledMultiColor(ImVec2(L, T), ImVec2(cxc, yc), end, end, mid, end); // TL: peak at centre (br)
        dl->AddRectFilledMultiColor(ImVec2(cxc, T), ImVec2(R, yc), end, end, end, mid); // TR: peak at centre (bl)
        dl->AddRectFilledMultiColor(ImVec2(L, yc), ImVec2(cxc, B), end, mid, end, end); // BL: peak at centre (ur)
        dl->AddRectFilledMultiColor(ImVec2(cxc, yc), ImVec2(R, B), mid, end, end, end); // BR: peak at centre (ul)
    };

    // -- the banner: full three-tier (map entry / big-banner sector) OR a single framed line (one-line sector) ----
    const float cx = W * 0.5f;
    const float pad = 18.f * scale;
    const bool showRegion = full && c.zdShowRegion && !region_.empty();
    const bool showSector = full && c.zdShowArea && !sector_.empty();
    const bool frameLone = !full; // one-line sector: frame the lone hero (sector) with a top + bottom hairline
    const float ruleH = 10.f * scale;

    std::string regionUp = region_;
    for (char &ch : regionUp)
        ch = (char)std::toupper((unsigned char)ch);

    // Pre-measure widths (for the backdrop) and the row centre Ys (top-down stack).
    const float heroW = rowWidth(heroStr, heroPx, heroTrack);
    float maxW = heroW;
    if (showRegion)
        maxW = (std::max)(maxW, rowWidth(regionUp.c_str(), regionPx, regionTrack));
    if (showSector)
        maxW = (std::max)(maxW, rowWidth(sector_.c_str(), sectorPx, sectorTrack));

    // Anchor the HERO (zone) at the vertical-position setting; seat the region ABOVE and the sector BELOW by EQUAL
    // visual whitespace -- measured from each line's ACTUAL glyph extent, NOT its font cell. The zone caps don't sit
    // centred in their cell, so cell-based spacing left a bigger gap above the zone than below; measuring the real
    // glyph top/bottom and placing both rules an equal distance from them keeps the zone optically centred.
    const float anchorY = H * (c.zdVerticalPct / 100.f);
    const float heroYc = anchorY;
    float hTopOff, hBotOff;
    rowVOff(heroStr, heroPx, hTopOff, hBotOff);
    const float heroGTop = heroYc + hTopOff, heroGBot = heroYc + hBotOff; // the hero's ACTUAL glyph top / bottom
    const float VGAP = heroPx * 0.52f;                                    // the equal visual gap above + below the hero (seats a rule with margin)
    const float frameGap = heroPx * 0.40f;                                // lone hero: gap from its glyph edge to the framing rule

    float regionYc = 0, ruleYc = 0, sectorYc = 0, ruleYcBot = 0;
    bool drawRuleTop = false, drawRuleBot = false, topMinor = false; // top rule is major (region->zone) or minor (lone frame)
    float stackTop = heroGTop, stackBottom = heroGBot;
    if (showRegion)
    {
        float rTopOff, rBotOff;
        rowVOff(regionUp.c_str(), regionPx, rTopOff, rBotOff);
        regionYc = heroGTop - VGAP - rBotOff; // region glyph BOTTOM sits VGAP above the hero glyph top
        ruleYc = heroGTop - VGAP * 0.5f;
        drawRuleTop = true; // major filigree centred in that gap
        stackTop = (std::min)(stackTop, regionYc + rTopOff);
    }
    else if (frameLone)
    {
        ruleYc = heroGTop - frameGap;
        drawRuleTop = true;
        topMinor = true;
        stackTop = (std::min)(stackTop, ruleYc - ruleH * 0.5f);
    }
    if (showSector)
    {
        float sTopOff, sBotOff;
        rowVOff(sector_.c_str(), sectorPx, sTopOff, sBotOff);
        sectorYc = heroGBot + VGAP - sTopOff; // sector glyph TOP sits VGAP below the hero glyph bottom
        ruleYcBot = heroGBot + VGAP * 0.5f;
        drawRuleBot = true; // minor filigree centred in that gap
        stackBottom = (std::max)(stackBottom, sectorYc + sBotOff);
    }
    else if (frameLone)
    {
        ruleYcBot = heroGBot + frameGap;
        drawRuleBot = true;
        stackBottom = (std::max)(stackBottom, ruleYcBot + ruleH * 0.5f);
    }
    const float padV = 12.f * scale; // snug, symmetric vertical padding around the measured text

    // Backdrop (behind everything): Band (L/R-feathered) / Soft vignette (all-sides) / Per-line glow / None.
    const float bg = baseA * (c.zdBackdropOpacity / 100.f);
    if (c.zdBackdropStyle == 0)
        softBandLR(cx - (maxW * 0.5f + pad * 2.f), cx + (maxW * 0.5f + pad * 2.f),
                   stackTop - padV + driftY, stackBottom + padV + driftY, bg);
    else if (c.zdBackdropStyle == 1)
        vignette(cx, (stackTop + stackBottom) * 0.5f + driftY,
                 maxW * 0.5f + pad * 2.5f, (stackBottom - stackTop) * 0.5f + pad * 1.5f, bg);
    else if (c.zdBackdropStyle == 2)
    {
        if (showRegion)
            vignette(cx, regionYc + driftY, rowWidth(regionUp.c_str(), regionPx, regionPx * 0.30f) * 0.5f + pad, regionPx, bg);
        vignette(cx, heroYc + driftY, heroW * 0.5f + pad * 1.5f, heroPx * 0.72f, bg);
        if (showSector)
            vignette(cx, sectorYc + driftY, rowWidth(sector_.c_str(), sectorPx, 0.f) * 0.5f + pad, sectorPx * 0.85f, bg);
    }

    if (drawRuleTop) // major diamond rule (region->zone), OR a minor hairline framing the lone hero
        Render::DrawZoneRule(dl, ImVec2(cx, ruleYc + driftY),
                             topMinor ? maxW * 0.42f + 10.f * scale : maxW * 0.31f + 22.f * scale, ruleH,
                             WithA(headBase, baseA), Ease::SmoothStep(inT), /*minor*/ topMinor);
    if (drawRuleBot) // subtler hairline: the zone->sector divider (dimmed), or the lone hero's bottom frame
        Render::DrawZoneRule(dl, ImVec2(cx, ruleYcBot + driftY),
                             frameLone ? maxW * 0.42f + 10.f * scale : maxW * 0.22f + 12.f * scale, ruleH,
                             WithA(headBase, frameLone ? baseA : baseA * 0.7f), Ease::SmoothStep(inT), /*minor*/ true);

    // The banner body. Two renderers, chosen by phase: a LIVE per-glyph path (drawRow, LabelDL) owns hold + the
    // cascade/clean entrances + the letter/crumble/glyph-dissolve exits; a faithful PER-GLYPH dissolve owns the
    // Decode/Slide/Twist entrances and the Burn/Slide exits. NO render target, each glyph is
    // drawn through dissolve.fx with its OWN atlas UV, so Mode 0's x+y diagonal is the glyph's ATLAS position (the
    // per-letter "decode" scramble), not a banner-wide swipe.
    const int entrance = entStyle; // 0 Heralded / 1 Clean / 2 Decode / 3 Slide / 4 Twist (main or sector)
    const bool dissolveReady = (atlasTex != (ImTextureID)0) && Fx::BurnReady();
    const bool needsKrytan = (entrance == 2 || entrance == 4);
    const bool entDissolve = (entrance == 2 || entrance == 3 || entrance == 4) && dissolveReady && (!needsKrytan || app.zdKrytanFont);
    const bool exitDissolve = burnExit && dissolveReady; // exit 4 Burn / 5 Slide

    // NewKrytan renders LARGER than the Latin banner font at the same px, so match its CAP HEIGHT to the Latin's
    // (via the 'H' glyph) -- a width match over-shrinks it (Krytan glyphs are wide). Applied as a glyph-size
    // multiplier to the Krytan layer; the per-glyph fit-cap in dissolveRow then keeps wide glyphs inside their cell.
    float kScale = 1.f;
    if (app.zdKrytanFont)
    {
        const ImFontGlyph *gl = font->FindGlyph((ImWchar)'H');
        const ImFontGlyph *gk = app.zdKrytanFont->FindGlyph((ImWchar)'H');
        if (gl && gk)
        {
            const float hL = (gl->Y1 - gl->Y0) / font->FontSize;
            const float hK = (gk->Y1 - gk->Y0) / app.zdKrytanFont->FontSize;
            if (hK > 0.01f)
                kScale = hL / hK;
        }
    }

    // Banner screen rect for the SPATIAL slide (Mode 1): maps the stack bounds to 0..1 for a banner-wide diagonal.
    const float rMinX = cx - (maxW * 0.5f + pad), rMaxX = cx + (maxW * 0.5f + pad);
    const float rMinY = stackTop - pad * 0.5f + driftY, rMaxY = stackBottom + pad * 0.5f + driftY;
    const ImVec4 bannerRect(rMinX, rMinY, 1.f / (std::max)(1.f, rMaxX - rMinX), 1.f / (std::max)(1.f, rMaxY - rMinY));

    // Draw ONE row of glyphs through the per-glyph dissolve. gf = glyph shapes; lf = layout advances. For Decode
    // the Krytan glyph (gf != lf) is centred in its LATIN cell so it morphs IN PLACE; else lf == gf. wgt > 0 adds
    // the faux-bold pass so the dissolving text matches the bold live banner at the phase boundaries.
    // `stagger`: drive each glyph over its OWN time window (Decode) so letters resolve in a varied order across the
    // whole entrance -- our atlas packs the banner glyphs into a narrow UV band, so the shader's atlas x+y thresholds
    // would otherwise cluster (a "freeze then flip"). When staggering, `amount` is the 0..1 PROGRESS and `reveal`
    // flips the direction (Latin in vs Krytan out). When not, `amount` is used directly as the dissolve amount.
    auto dissolveRow = [&](float yc, const char *s, ImFont *gf, ImFont *lf, float fsGlyph, float fsLayout, float track,
                           ImU32 color, float amount, int mode, int glowKind, float wgt, bool stagger, bool reveal, int seed, float opacity)
    {
        if (!gf || !lf || ((color >> 24) & 0xFF) == 0 || opacity <= 0.003f)
            return;
        const ImU32 colA = (opacity >= 0.999f) ? color : WithA(color, opacity); // Stage-1 Opacity fade-in rides in the colour alpha
        const bool centerCell = (gf != lf);                                     // Krytan drawn at fsGlyph, advanced/centred in the Latin cells (fsLayout)
        const float bwt = (wgt < 0.f) ? 0.8f : wgt;                             // DrawLabelCore's kBoldWeight default -- region/sector are faux-bolded too
        float tw = 0.f;
        for (const char *p = s; *p;)
        {
            const int n = Utf8Len((unsigned char)*p);
            char b[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                b[k] = p[k];
            b[k] = '\0';
            tw += Gw2Ui::MeasureWidth(b, fsLayout, lf) + track;
            p += k;
        }
        if (tw > track)
            tw -= track;
        float penX = cx - tw * 0.5f;
        const float lineTop = std::floor((yc + driftY) - fsGlyph * 0.5f);
        const float gsc0 = fsGlyph / gf->FontSize;
        int gi = 0;
        for (const char *p = s; *p; ++gi)
        {
            const int n = Utf8Len((unsigned char)*p);
            char b2[8];
            int k = 0;
            for (; k < n && p[k]; ++k)
                b2[k] = p[k];
            b2[k] = '\0';
            const float cellW = Gw2Ui::MeasureWidth(b2, fsLayout, lf);
            const ImFontGlyph *g = gf->FindGlyph((ImWchar)decodeCp(b2, k));
            if (g && g->Visible)
            {
                float a = amount;
                if (stagger) // each glyph dissolves over a 0.45-wide window starting at a hashed offset in [0, 0.55]
                {
                    const float ph = Hash01(gi * 7 + seed * 101) * 0.55f;
                    const float localT = Ease::Clamp01((amount - ph) / 0.45f);
                    a = reveal ? (1.f - localT) : localT;
                }
                // Establish the glyph's PEN (penGX/penGY) so every pass can floor it independently, EXACTLY as
                // ImGui 1.80's AddText does (ImFont::RenderText's "align to be pixel perfect" IM_FLOOR per call).
                // The live hold banner goes through AddText, so matching the per-pass flooring keeps the held and
                // dissolving glyphs on the identical pixel grid (no shift/inset at the swap).
                float gsc = gsc0, penGX, penGY;
                if (centerCell) // Krytan centred in its Latin cell (transient; sized to fit, no pile)
                {
                    const float gw0 = (g->X1 - g->X0) * gsc;
                    if (gw0 > cellW * 0.96f && gw0 > 1.f)
                        gsc *= (cellW * 0.96f) / gw0;
                    const float gw2 = (g->X1 - g->X0) * gsc, gh2 = (g->Y1 - g->Y0) * gsc;
                    penGX = penX + (cellW - gw2) * 0.5f - g->X0 * gsc;
                    penGY = (yc + driftY - gh2 * 0.5f) - g->Y0 * gsc;
                }
                else
                {
                    penGX = penX;
                    penGY = (yc + driftY) - fsGlyph * 0.5f;
                } // floor(penGY) == lineTop
                const ImVec2 uv0(g->U0, g->V0), uv1(g->U1, g->V1);
                auto pass = [&](float dx, float dy, ImU32 colq) { // one glyph pass with the pen floored, like AddText
                    const float px = std::floor(penGX + dx), py = std::floor(penGY + dy);
                    Fx::DissolveGlyph(dl, atlasTex, ImVec2(px + g->X0 * gsc, py + g->Y0 * gsc), ImVec2(px + g->X1 * gsc, py + g->Y1 * gsc),
                                      uv0, uv1, colq, a, mode, glowKind, bannerRect);
                };
                if (strokeNow) // black stroke THROUGH the dissolve
                {              // the outline burns with the glyph and the ember lights it -> the fire traces the letters
                    const ImU32 strokeCol = WithA(IM_COL32(0, 0, 0, 255), opacity);
                    const float d = 1.f;
                    const float so[8][2] = {{0, -d}, {d, -d}, {d, 0}, {d, d}, {0, d}, {-d, d}, {-d, 0}, {-d, -d}};
                    for (int o = 0; o < 8; ++o)
                        pass(so[o][0], so[o][1], strokeCol);
                }
                pass(0.f, 0.f, colA); // fill
                if (bwt > 0.f)
                {
                    if (bwt >= 1.2f)
                        pass(bwt * 0.5f, 0.f, colA);
                    pass(bwt, 0.f, colA);
                } // faux-bold, like DrawLabelCore
            }
            penX += cellW + track;
            p += k;
        }
    };
    // gmul scales the GLYPH draw size (Krytan = kScale to match the Latin's apparent size; Latin = 1). When centring
    // Krytan into Latin cells (gf != lf) the layout cells stay the row's base size; otherwise layout = the scaled size.
    auto dissolveBanner = [&](ImFont *gf, ImFont *lf, float gmul, float amount, int mode, int glowKind, bool stagger, bool reveal, float opacity)
    {
        const float lmul = (gf == lf) ? gmul : 1.f;
        if (showRegion)
            dissolveRow(regionYc, regionUp.c_str(), gf, lf, regionPx * gmul, regionPx * lmul, regionTrack, headBase, amount, mode, glowKind, -1.f, stagger, reveal, 1, opacity);
        dissolveRow(heroYc, heroStr, gf, lf, heroPx * gmul, heroPx * lmul, heroTrack, heroBase, amount, mode, glowKind, weight, stagger, reveal, 2, opacity);
        if (showSector)
            dissolveRow(sectorYc, sector_.c_str(), gf, lf, sectorPx * gmul, sectorPx * lmul, sectorTrack, sectorBase, amount, mode, glowKind, -1.f, stagger, reveal, 3, opacity);
    };

    if (age < enter && entDissolve) // ---- ENTRANCE dissolve: Decode / Slide / Twist ----
    {
        if (entrance == 3) // Slide: single Latin layer, spatial diagonal wipe-in (ember front)
            dissolveBanner(font, font, 1.f, 1.f - inT, 1, 1, false, false, 1.f);
        else // Decode(2) / Twist(4) -- Two stages:
        {
            const float fadeIn = enter * 0.55f;
            if (age < fadeIn) // Stage 1: the intact Krytan FADES IN (opacity 0->1); no Latin, no glow
            {
                const float op = Ease::SmoothStep(Ease::Clamp01(age / fadeIn));
                if (entrance == 2)
                    dissolveBanner(app.zdKrytanFont, font, kScale, 0.f, 0, 0, false, false, op);
                else
                    dissolveBanner(app.zdKrytanFont, font, kScale, 0.f, 1, 0, false, false, op); // Twist Krytan on the Latin cells -> normal width
            }
            else // Stage 2: cross-dissolve Krytan -> Latin with the orange ember (the burn/fire swap)
            {
                const float dT = Ease::Clamp01((age - fadeIn) / (enter - fadeIn));
                if (entrance == 2) // Decode: per-letter staggered atlas dissolve, in place
                {
                    dissolveBanner(app.zdKrytanFont, font, kScale, dT, 0, 1, /*stagger*/ true, /*reveal*/ false, 1.f); // Krytan OUT (under)
                    dissolveBanner(font, font, 1.f, dT, 0, 1, /*stagger*/ true, /*reveal*/ true, 1.f);                 // Latin IN (over)
                }
                else // Twist: spatial swipe cross-dissolve, Krytan on the Latin cells (normal width)
                {
                    dissolveBanner(app.zdKrytanFont, font, kScale, dT, 1, 1, false, false, 1.f); // Krytan OUT (spatial)
                    dissolveBanner(font, font, 1.f, 1.f - dT, 1, 1, false, false, 1.f);          // Latin IN (spatial)
                }
            }
        }
    }
    else if (age >= holdEnd && exitDissolve) // ---- EXIT dissolve: Burn (perlin) / Slide (spatial) ----
    {
        if (exitMode == 4)
            dissolveBanner(font, font, 1.f, 0.50f + outT * 0.42f, 2, 2, false, false, 1.f); // Burn: perlin active range dv in [0.5,0.89]
        else
            dissolveBanner(font, font, 1.f, outT, 1, 1, false, false, 1.f); // Slide: ember wipe-out
    }
    else // ---- LIVE: hold + cascade/clean entrances + letter/crumble/fade exits ----
    {
        // Region + sector mirror the hero's per-letter exit (Letter fade / Dissolve / Crumble) so the WHOLE banner
        // animates out together; for a non-per-glyph exit (Fade) this is just baseA, unchanged. The per-glyph exit
        // handles its own vanish, so it drops auxFade (else a double-fade).
        const float auxRowA = inA * (perGlyphExit ? 1.f : auxFade);
        if (showRegion)
            drawRow(cx, regionYc, regionUp.c_str(), regionPx, headBase, -1.f, regionTrack, auxRowA, -1.f, rowExit, outT, strokeNow, nullptr, nullptr);

        float hL = cx, hR = cx;
        const float heroRowA = (cascade ? 1.f : inA) * (perGlyphExit ? 1.f : auxFade);
        drawRow(cx, heroYc, heroStr, heroPx, heroBase, weight, heroTrack, heroRowA,
                cascade ? age : -1.f, rowExit, outT, strokeNow, &hL, &hR);

        if (showSector)
            drawRow(cx, sectorYc, sector_.c_str(), sectorPx, sectorBase, -1.f, sectorTrack, auxRowA, -1.f, rowExit, outT, strokeNow, nullptr, nullptr);

        // Shine across the hero line during the settle + hold (not during the exit). Letter glint or GPU sweep.
        if (shineStyle != 3 && age < holdEnd)
        {
            // Dissolve entrances (Decode/Slide/Twist) only reach this live shine at HOLD, so start the sweep
            // FRESH there (+ a small grace so the revealed letters register first) -- otherwise st is already
            // ~half-done the instant the real letters appear. Live entrances keep the settle-time start.
            const float shineStart = entDissolve ? (enter + 0.12f) : enter * 0.85f;
            const float st = Ease::Clamp01((age - shineStart) / 0.60f);
            if (st > 0.f && st < 1.f && heroRowA > 0.05f)
            {
                const bool wantSweep = (shineStyle == 1 || shineStyle == 2);
                if (wantSweep && Fx::ZoneSheenReady())
                {
                    const float top = heroYc - heroPx * 0.70f + driftY, bot = heroYc + heroPx * 0.70f + driftY;
                    const float intensity = (shineStyle == 1) ? 0.50f : 0.65f;
                    const float band = (shineStyle == 1) ? 0.004f : 0.02f;
                    Fx::ZoneSheen(dl, ImVec2(hL - 8.f, top), ImVec2(hR + 8.f, bot),
                                  Ease::Lerp(-0.15f, 1.15f, st), intensity, heroRowA, band);
                }
                else
                {
                    const float sweepX = Ease::Lerp(hL - heroPx * 0.5f, hR + heroPx * 0.5f, st);
                    glint(heroStr, heroYc, heroPx, hL, sweepX, heroPx * 0.5f, 0.85f * heroRowA, heroTrack);
                }
            }
        }
    }
    Gw2Ui::PopTextScale();
}
