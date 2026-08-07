#include "Toast.h"
#include "Notify.h"
#include "app/App.h"
#include "ui/ApiReminder.h"
#include "ui/NotifyActions.h"   // one dispatcher for a clicked notification
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <vector>

namespace
{
    constexpr float kCardW    = 300.f;   // fixed toast width (independent of the dashboard panel width)
    constexpr float kPad      = 10.f;
    constexpr float kAccentW  = 4.f;
    constexpr float kGap      = 8.f;
    constexpr float kMargin   = 14.f;    // distance from the screen edge
    constexpr float kTitlePx  = 17.f;
    constexpr float kBodyPx   = 14.f;
    constexpr float kInAnim   = 0.22f;   // slide-in seconds
    constexpr float kOutAnim  = 0.5f;    // fade-out seconds before expiry

    ImU32 WithA(ImU32 c, float a)
    {
        a = a < 0.f ? 0.f : (a > 1.f ? 1.f : a);
        return (c & 0x00FFFFFF) | ((ImU32)(((c >> 24) & 0xFF) * a) << 24);
    }

    struct Layout { float h; float textX; float textW; float titleH; float bodyH; };

    Layout Measure(const App& app, const Notify::Item& it, ImFont* font)
    {
        Layout L{};
        const float sc = Gw2Ui::GlobalScale();
        const float cardW = kCardW * sc;
        const float pad = kPad * sc;
        const float accentW = kAccentW * sc;
        const float iconW = app.config.notifyIcons ? 16.f * sc : 0.f;
        L.textX  = accentW + pad + (iconW > 0.f ? iconW + 8.f * sc : 0.f);
        L.textW  = cardW - L.textX - pad;
        L.titleH = it.title.empty() ? 0.f : Gw2Ui::MeasureWrappedHeight(it.title.c_str(), kTitlePx, L.textW, font);
        L.bodyH  = it.body.empty()  ? 0.f : Gw2Ui::MeasureWrappedHeight(it.body.c_str(),  kBodyPx,  L.textW, font);
        L.h = pad + L.titleH + (L.bodyH > 0.f ? 4.f * sc + L.bodyH : 0.f) + pad;
        if (L.h < 38.f * sc) L.h = 38.f * sc;
        return L;
    }
}

void Toast::Render(App& app)
{
    if (!app.config.notifyEnabled) return;

    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 1.f || H < 1.f) return;

    const float sc = Gw2Ui::GlobalScale();
    const float cardW = kCardW * sc;
    const float gap = kGap * sc;
    const float margin = kMargin * sc;
    const float pad = kPad * sc;
    const float accentW = kAccentW * sc;
    const double now = ImGui::GetTime();
    std::vector<const Notify::Item*> live = Notify::Live(now);
    if (live.empty()) return;

    int maxN = app.config.notifyMax; maxN = maxN < 1 ? 1 : (maxN > 10 ? 10 : maxN);
    if ((int)live.size() > maxN) live.erase(live.begin(), live.end() - maxN);  // keep newest maxN

    ImFont* font = (app.config.notifyFont == 1) ? Gw2Ui::StockFont() : Gw2Ui::UiFontResolved();

    const int corner = app.config.notifyCorner;       // 0 TL,1 TC,2 TR,3 Center,4 BL,5 BC,6 BR
    const bool bottom = (corner >= 4);
    const bool left   = (corner == 0 || corner == 4);
    const bool right  = (corner == 2 || corner == 6);

    // Pre-measure so we can centre the column vertically (Center) and stack from the right anchor.
    std::vector<Layout> lay; lay.reserve(live.size());
    float totalH = 0.f;
    for (const Notify::Item* it : live) { Layout L = Measure(app, *it, font); lay.push_back(L); totalH += L.h; }
    if (live.size() > 1) totalH += gap * (live.size() - 1);

    // Horizontal anchor (card left x), then apply the user offset.
    float cardX;
    if (left)       cardX = margin;
    else if (right) cardX = W - margin - cardW;
    else            cardX = (W - cardW) * 0.5f;        // centre columns (1,3,5)
    cardX += app.config.notifyOffsetX;

    // Vertical start edge. Newest is drawn nearest the anchor: top/centre stack downward, bottom upward.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float edge;
    if (bottom)        edge = H - margin;                   // bottom of the newest card
    else if (corner == 3) edge = (H - totalH) * 0.5f;      // centre block
    else               edge = margin;                      // top of the newest card
    edge += app.config.notifyOffsetY;

    // Draw newest-first (nearest the anchor).
    for (int n = (int)live.size() - 1; n >= 0; --n)
    {
        const Notify::Item& it = *live[n];
        const Layout& L = lay[n];

        float top = bottom ? (edge - L.h) : edge;
        // animation: slide-in from the nearest edge + fade; fade out near expiry.
        const float tin  = std::min(1.f, (float)(now - it.created) / kInAnim);
        const float tout = std::min(1.f, (float)(it.expires - now) / kOutAnim);
        const float alpha = std::max(0.f, std::min(tin, tout));
        const float slide = (1.f - tin) * 16.f * sc;
        float ox = 0.f, oy = 0.f;
        if (left)        ox = -slide;
        else if (right)  ox =  slide;
        else             oy = bottom ? slide : -slide;

        const ImVec2 a(cardX + ox, top + oy);
        const ImVec2 b(a.x + cardW, a.y + L.h);

        // hover-pause: freeze the timer while the cursor is over the card (geometry test only; no input capture).
        const bool over = io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
        if (over) Notify::ExtendLive(it.id, now);
        // Clickable toasts (an action attached): a click opens the target and dismisses the toast.
        if (over && it.action != Notify::Action::None)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                NotifyActions::Dispatch(app, it.action, it.payload);
                Notify::Dismiss(it.id);
            }
        }

        const Notify::KindMeta& meta = Notify::Meta(it.kind);
        const ImU32 accent = WithA(meta.accent, alpha);

        if (app.config.notifyBackground)
        {
            dl->AddRectFilled(a, b, WithA(IM_COL32(18, 15, 11, 235), alpha), 4.f * sc);
            dl->AddRect      (a, b, WithA(IM_COL32(120, 100, 60, 200), alpha), 4.f * sc);
        }
        // left accent bar
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + accentW, b.y), accent, 4.f * sc, ImDrawCornerFlags_Left);
        // icon marker (a small accent dot until real gw2dat icons are wired in)
        if (app.config.notifyIcons)
            dl->AddCircleFilled(ImVec2(a.x + accentW + pad + 8.f * sc, (a.y + b.y) * 0.5f), 5.f * sc, accent);

        const float tx = a.x + L.textX;
        const float tw = L.textW;
        float ty = a.y + pad;
        const bool stroke = !app.config.notifyBackground;   // outline text when there is no card behind it
        if (!it.title.empty())
        {
            Gw2Ui::LabelDL(dl, ImVec2(tx, ty), ImVec2(tx + tw, ty + L.titleH), it.title.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, WithA(IM_COL32(247, 241, 224, 255), alpha),
                           stroke, font, kTitlePx, tw, 1.0f);
            ty += L.titleH + 4.f * sc;
        }
        if (!it.body.empty())
            Gw2Ui::LabelDL(dl, ImVec2(tx, ty), ImVec2(tx + tw, ty + L.bodyH), it.body.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, WithA(IM_COL32(202, 196, 178, 255), alpha),
                           stroke, font, kBodyPx, tw);

        edge = bottom ? (top - gap) : (top + L.h + gap);
    }
}
