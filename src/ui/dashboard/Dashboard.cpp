#include "Dashboard.h"
#include "widgets/Widget.h"
#include "Notify.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/SettingsWindow.h"        // OpenSettingsTab (the header "Open Hub" button)
#include "render/glyphs/Glyphs.h"     // Render::DrawGlyph (profile-chip caret)
#include "ui/tabs/SettingsCommon.h"   // SettingsText scale + SettingsParagraph (this file draws the Dashboard settings)
#include "ui/profiles/ProfileBar.h"      // Profiles::DrawProfileBar + IProfileHost (in-panel profile chip/menu)
#include "ui/profiles/ConfigProfiles.h"  // ConfigProfiles::Host / RegisterLayout / Owner (per-character profiles)
#include "ui/LayoutOrder.h"           // shared ordered add/remove/reorder model (Dashboard/Info Panel/HUD)
#include "ui/LayoutOrderJson.h"          // UiLayout::OrderedToJson / OrderedFromJson (dashLayout persistence)
#include "ui/LayoutTypes.h"              // DashSlot (also via Config.h/App.h, but explicit)
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX               // keep windows.h's min/max macros from clobbering std::min/std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>           // GetAsyncKeyState: detect game-world clicks Nexus doesn't feed into ImGui io

// -----------------------------------------------------------------------------------------------------
// Layout model: an ordered list of {widget key, shown, span}, held in Config (app.config.dashLayout) and made
// per-character by the Dashboard ConfigProfileFamily (ui/profiles/ConfigProfiles.h). Reconciled against the
// registry so newly added widgets appear (with their defaultOn) and removed ones drop out -- forward-compatible
// across versions. There is no bespoke payload/store here anymore -- the layout IS a slice of Config.
// -----------------------------------------------------------------------------------------------------
namespace
{
    bool   g_reconciled = false;

    // Old layout-key renames (applied to items + the removed set during Reconcile).
    const std::map<std::string, std::string> kDashRenames = { {"nearbyRadar", "routeArrow"}, {"pinnedWaypoints", "favoriteWaypoints"} };
    // A fresh slot for `key` with the registry's default extras (used to append a re-enabled / new widget).
    DashSlot MakeSlot(const std::string& key)
    {
        const DashWidget* w = FindDashWidget(key.c_str());
        DashSlot s; s.key = key; s.span = w ? w->defaultSpan : 1; s.hoverChrome = w ? w->defaultHoverChrome : false;
        return s;
    }
    std::vector<UiLayout::Ordered<DashSlot>::Reg> BuildReg()
    {
        std::vector<UiLayout::Ordered<DashSlot>::Reg> reg;
        for (const DashWidget& w : DashWidgets()) reg.push_back({ w.key, w.defaultOn, [k = w.key] { return MakeSlot(k); } });
        return reg;
    }

    char        g_profSearch[64] = "";         // header menu search box
    char        g_profNameBuf[64] = "";        // shared name prompt buffer
    enum class  ProfPrompt { New, Rename, Duplicate };
    ProfPrompt  g_profPrompt = ProfPrompt::New;
    int         g_profPromptIdx = -1;          // target profile for Rename/Duplicate (panel "Create New" path)
    bool        g_profMenuReq = false;         // open the header profile menu this frame
    bool        g_profPromptReq = false;       // open the name prompt this frame

    std::map<std::string, float> g_hoverAnim;   // per-widget hover-chrome reveal (0..1), drives the inline strip

    bool   g_open  = false;
    float  g_anim  = 0.f;             // 0 closed .. 1 open
    float  g_bodyH = 320.f;           // measured body content height (drives the panel height; 1-frame lag)
    double g_lastInside = 0.0;        // last time the cursor was over the panel/handle (auto-hide-on-leave)
    constexpr float kAutoHideDelay = 1.4f;   // seconds the cursor must be away before auto-collapse

    // handle drag state
    bool   g_hDown = false;
    bool   g_hDragged = false;
    ImVec2 g_hStart{};

    // widget drag-reorder state
    std::string g_dragKey;
    struct Row { std::string key; float x0, y0, x1, y1; };
    std::vector<Row> g_rows;

    constexpr float kT = 20.f;   // handle thickness (off the edge)
    constexpr float kL = 56.f;   // handle length (along the edge)

    void Reconcile(App& app)
    {
        if (g_reconciled) return;
        app.config.dashLayout.Reconcile(BuildReg(), kDashRenames);   // renames + drop-missing + append default-on (shared model)
        g_reconciled = true;
    }

    DashSlot* SlotFor(App& app, const std::string& key) { return app.config.dashLayout.Find(key); }
    void  MoveSlot(App& app, const std::string& fromKey, const std::string& toKey, bool insertBefore = true) { app.config.dashLayout.MoveRelative(fromKey, toKey, insertBefore); }

    // --- quick-add modal state ---
    bool        g_addReq = false;     // a "+" was clicked this frame -> open the modal
    std::string g_addCtx;             // the lone-half key to fill as a partner ("" = general add)
    char        g_addSearch[64] = "";

    // --- remove-confirm state ---
    std::string g_removeReq;          // a trash/right-click "remove" was clicked -> open the confirm
    std::string g_removeKey;          // the widget the confirm card is asking about

    // Enable a hidden widget. When `afterKey` is a lone half, the new widget becomes its half partner
    // (span=2, placed right after it) so it drops into the empty slot.
    void EnableWidget(App& app, const std::string& key, const std::string& afterKey)
    {
        app.config.dashLayout.Enable(key, [key] { return MakeSlot(key); });   // clears any removal + appends at the bottom if absent
        if (afterKey.empty()) return;
        // Dropped into a lone half slot -> become its half partner (span=2, placed right after it).
        const DashWidget* wd = FindDashWidget(key.c_str());
        DashSlot* s = app.config.dashLayout.Find(key);
        DashSlot* partner = app.config.dashLayout.Find(afterKey);
        if (wd && wd->canHalf && s && partner) { s->span = 2; partner->span = 2; app.config.dashLayout.MoveRelative(key, afterKey, /*before*/ false); }
    }

    // ---- Profiles ------------------------------------------------------------------------------------------
    // A fresh layout from the registry defaults (used to seed the default profile via RegisterConfig::seedDefault).
    UiLayout::Ordered<DashSlot> DefaultLayout()
    {
        UiLayout::Ordered<DashSlot> o;
        std::vector<const DashWidget*> on;
        for (const DashWidget& w : DashWidgets()) if (w.defaultOn) on.push_back(&w);   // each widget carries its own default
        std::sort(on.begin(), on.end(), [](const DashWidget* a, const DashWidget* b) { return a->defaultOrder < b->defaultOrder; });
        for (const DashWidget* w : on) o.items.push_back(MakeSlot(w->key));   // shown = default-on, in defaultOrder
        return o;
    }

    // Profile CRUD routes through the ONE per-character ConfigProfiles family (Owner::Dash). These thin wrappers
    // preserve the header-menu call sites + mark settings dirty; the family mirrors the active profile into the
    // live Config (app.config.dash* + app.config.dashLayout) on switch.
    void SwitchProfile(App& app, int i)       { ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash).SetActive(i);   app.settingsDirty = true; }
    void CreateProfile(App& app, const std::string& name)        { ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash).New(name);         app.settingsDirty = true; }
    void RenameProfile(App& app, int i, const std::string& name) { ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash).Rename(i, name);    app.settingsDirty = true; }
    void DuplicateProfile(App& app, int i, const std::string& name) { ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash).Duplicate(i, name); app.settingsDirty = true; }
}

void Dashboard::RegisterConfig()
{
    ConfigProfiles::RegisterLayout(ConfigProfiles::Owner::Dash, {
        // capture: the structured dashLayout field -> the slice json (the dash* scalars come from the Settings() table)
        [](App& app, nlohmann::json& j) {
            j["dashLayout"] = UiLayout::OrderedToJson(app.config.dashLayout,
                [](const DashSlot& s, nlohmann::json& e) { e["span"] = s.span; e["hover"] = s.hoverChrome; });
        },
        // apply: slice json -> dashLayout, then reconcile against the widget registry (renames + surface new widgets)
        [](App& app, const nlohmann::json& j) {
            if (j.contains("dashLayout"))
                UiLayout::OrderedFromJson(app.config.dashLayout, j["dashLayout"],
                    [](const nlohmann::json& e) { DashSlot s; s.key = e.value("key", std::string()); s.span = e.value("span", 1); s.hoverChrome = e.value("hover", false); return s; });
            app.config.dashLayout.Reconcile(BuildReg(), kDashRenames);
            g_reconciled = true;   // freshly reconciled by the applied profile -> Reconcile() can no-op this frame
        },
        // seedDefault: dashLayout <- the registry default layout (feeds the baked default slice at Init)
        [](App& app) { app.config.dashLayout = DefaultLayout(); }
    });
}

void Dashboard::Toggle()
{
    g_open = !g_open;
    if (g_open) Notify::MarkAllRead();
}

bool Dashboard::IsOpen() { return g_open || g_anim > 0.001f; }

bool Dashboard::HasVisibleWvwWidget(App& app)
{
    // Gates AccountData::TickWvw: poll WvW only when a WvW widget is actually on a visible dashboard.
    if (!IsOpen()) return false;
    return app.config.dashLayout.Shown("wvwMatchup") || app.config.dashLayout.Shown("wvwCurrentMap") || app.config.dashLayout.Shown("wvwMaps");
}

// ------------------------------------------------------------------------------------------------------
// One widget card: a grip + icon + title header, then the widget body. Wrapped in a group so we can record
// its screen rect for drag-reorder hit-testing.
// ------------------------------------------------------------------------------------------------------
namespace
{
    // The dashboard chrome buttons are thin wrappers over the shared Gw2Ui::SmallIconButton primitive (ONE
    // place for the glyph + hover styling). The grip wires its reorder drag via ImGui::IsItemActivated() after
    // the call (the primitive's InvisibleButton is the last item); the right-click is wired at the call site.
    void DrawGrip(const char* key, ImVec2 at)
    {
        Gw2Ui::SmallIconButton(key, at, 16.f, Gw2Ui::IconBtn::Grip, g_dragKey == key);
        if (ImGui::IsItemActivated()) g_dragKey = key;
    }

    // (The per-row width / chrome / trash / arrow / plus buttons now come from Gw2Ui::ReorderRow + EnableRow
    //  in DrawWidgetSettings -- the shared reorder widget the HUD + Info Panel settings also use.)

    // A faint "+ Add widget" placeholder filling an empty half-slot. Clicking requests the quick-add modal,
    // remembering `loneKey` so the chosen widget becomes its half partner.
    void DrawAddPlaceholder(float w, const std::string& loneKey)
    {
        const float sc = Gw2Ui::TextScale();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = 54.f * sc;
        ImGui::PushID(("add_" + loneKey).c_str());
        const bool clicked = ImGui::InvisibleButton("##addslot", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) { g_addReq = true; g_addCtx = loneKey; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = hov ? Gw2Ui::Alpha(Gw2Ui::kGold, 200) : IM_COL32(120, 110, 86, 140);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), col, 3.f * sc, 0, 1.2f * sc);
        const ImVec2 c(p.x + w * 0.5f, p.y + h * 0.5f - 6.f * sc);
        dl->AddLine(ImVec2(c.x - 6.f * sc, c.y), ImVec2(c.x + 6.f * sc, c.y), col, 1.8f * sc);
        dl->AddLine(ImVec2(c.x, c.y - 6.f * sc), ImVec2(c.x, c.y + 6.f * sc), col, 1.8f * sc);
        Gw2Ui::LabelDL(dl, ImVec2(p.x, c.y + 8.f * sc), ImVec2(p.x + w, c.y + 24.f * sc), "Add widget",
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, col, false, nullptr, 14.f);
    }

    // The control strip: grip (left, drag + right-click) | title | chrome-toggle + half + trash (right). Split
    // into INPUT (submit the buttons + handle clicks/menu) and VISUAL (paint backing + glyphs + title) so the
    // HOVER overlay can submit its input BEFORE the widget body (to win clicks) and paint on top AFTER -- all
    // inline, no separate window. PINNED (bar) mode uses Both: it sits in reserved space, so there's no overlap.
    enum class StripPhase { Both, Input, Visual };
    struct StripHover { bool grip = false, chrome = false, half = false, trash = false; };

    void DrawControlStrip(App& app, const DashWidget& wdef, DashSlot* sl, ImVec2 at, float w, float bandH, bool faint,
                          StripPhase phase = StripPhase::Both, StripHover* sh = nullptr)
    {
        const bool doInput  = (phase != StripPhase::Visual);
        const bool doVisual = (phase != StripPhase::Input);
        const float sc = Gw2Ui::TextScale();
        const float tsz = 18.f * sc, gp = 6.f * sc, gripSz = 16.f * sc;
        const float yC = at.y + (bandH - tsz) * 0.5f;
        char ctxId[96]; std::snprintf(ctxId, sizeof(ctxId), "##hctx_%s", wdef.key);

        // shared geometry (right-to-left): trash | half | chrome | ... | title
        const ImVec2 gripAt(at.x, at.y + (bandH - gripSz) * 0.5f);
        float rx = at.x + w - 4.f * sc - tsz;
        const ImVec2 trashAt(rx, yC);                  rx -= tsz + gp;
        const bool   hasHalf = wdef.canHalf;
        const ImVec2 halfAt(rx, yC);                   if (hasHalf) rx -= tsz + gp;
        const ImVec2 chromeAt(rx, yC);
        const float  titleRight = rx - gp;
        const ImVec2 titleAt(at.x + 22.f * sc, at.y);

        StripHover local; StripHover& hv = sh ? *sh : local;

        if (doInput)
        {
            ImGui::PushID(wdef.key);
            ImGui::SetCursorScreenPos(gripAt);
            ImGui::InvisibleButton("##grip", ImVec2(gripSz, gripSz));
            hv.grip = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated()) g_dragKey = wdef.key;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(ctxId);

            ImGui::SetCursorScreenPos(trashAt);
            const bool trashClick = ImGui::InvisibleButton("##trash", ImVec2(tsz, tsz));
            hv.trash = ImGui::IsItemHovered();
            if (hv.trash) Gw2Ui::Tooltip("Disable (re-enable with + button at the top)");
            if (trashClick) g_removeReq = wdef.key;

            if (hasHalf)
            {
                ImGui::SetCursorScreenPos(halfAt);
                const bool halfClick = ImGui::InvisibleButton("##wh", ImVec2(tsz, tsz));
                hv.half = ImGui::IsItemHovered();
                if (hv.half) Gw2Ui::Tooltip((sl && sl->span == 2) ? "Switch to full width" : "Switch to half width (side-by-side)");
                if (halfClick && sl) { sl->span = (sl->span == 2) ? 1 : 2; app.settingsDirty = true; }
            }

            ImGui::SetCursorScreenPos(chromeAt);
            const bool chromeClick = ImGui::InvisibleButton("##chrome", ImVec2(tsz, tsz));
            hv.chrome = ImGui::IsItemHovered();
            if (hv.chrome) Gw2Ui::Tooltip((sl && sl->hoverChrome) ? "Show header bar" : "Hide header (controls on hover)");
            if (chromeClick && sl) { sl->hoverChrome = !sl->hoverChrome; app.settingsDirty = true; }

            ImGui::SetCursorScreenPos(titleAt);
            ImGui::InvisibleButton("##hdr", ImVec2(std::max(10.f * sc, titleRight - at.x - 22.f * sc), bandH));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(ctxId);
            ImGui::PopID();

            // right-click context menu (chrome -> native text scale so it never overflows its panel)
            const std::string togHalf = (sl && sl->span == 2) ? "Make full width" : "Make half width";
            const std::string togChr  = (sl && sl->hoverChrome) ? "Show header bar" : "Hide header (controls on hover)";
            const char* items[3]; int n = 0;
            const int halfIdx = hasHalf ? n++ : -1; if (hasHalf) items[halfIdx] = togHalf.c_str();
            const int chrIdx  = n++; items[chrIdx] = togChr.c_str();
            const int remIdx  = n++; items[remIdx] = "Disable";
            Gw2Ui::PushTextScale(1.0f);
            const int sel = Gw2Ui::ContextMenu(ctxId, items, n);
            Gw2Ui::PopTextScale();
            if      (sel == halfIdx) { if (sl) { sl->span = (sl->span == 2) ? 1 : 2; app.settingsDirty = true; } }
            else if (sel == chrIdx)  { if (sl) { sl->hoverChrome = !sl->hoverChrome; app.settingsDirty = true; } }
            else if (sel == remIdx)  g_removeReq = wdef.key;
        }

        if (doVisual)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (faint)
            {
                dl->AddRectFilled(at, ImVec2(at.x + w, at.y + bandH), IM_COL32(22, 19, 13, 226), 3.f * sc);
                dl->AddRect(at, ImVec2(at.x + w, at.y + bandH), IM_COL32(120, 100, 60, 150), 3.f * sc);
            }
            Gw2Ui::SmallIconGlyph(dl, gripAt, gripSz, Gw2Ui::IconBtn::Grip, hv.grip || g_dragKey == wdef.key);
            Gw2Ui::SmallIconGlyph(dl, trashAt, tsz, Gw2Ui::IconBtn::Minus, hv.trash);
            if (hasHalf) Gw2Ui::SmallIconGlyph(dl, halfAt, tsz, (sl && sl->span == 2) ? Gw2Ui::IconBtn::WidthHalf : Gw2Ui::IconBtn::WidthFull, hv.half);
            Gw2Ui::SmallIconGlyph(dl, chromeAt, tsz, (sl && sl->hoverChrome) ? Gw2Ui::IconBtn::ChromeHover : Gw2Ui::IconBtn::ChromeBar, hv.chrome);
            // Auto-fit the title to the space left of the controls (half-width cards are narrow).
            float titleFs = 18.f;
            const float titleAvail = titleRight - (at.x + 22.f * sc);
            const float titleMeasured = Gw2Ui::MeasureWidth(wdef.title, titleFs);
            if (titleMeasured > titleAvail && titleMeasured > 1.f)
                titleFs = std::max(11.f, titleFs * (titleAvail / titleMeasured));
            Gw2Ui::LabelDL(dl, titleAt, ImVec2(titleRight, at.y + bandH), wdef.title,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, false, nullptr, titleFs, 0.f, 1.3f);
        }
    }

    // One widget = an optional control strip + the body. selfFramed widgets draw their OWN card(s) (the
    // dashboard adds NO frame -> never nests a channel-split); normal widgets get the dashboard card frame.
    // The strip is pinned (bar mode) or hidden until hover (hoverChrome). Group flow is left to ImGui so the
    // half-pair SameLine + inter-widget spacing keep working.
    void DrawCard(App& app, const DashWidget& wdef, float width)
    {
        DashSlot* sl = SlotFor(app, wdef.key);
        const bool hover = sl && sl->hoverChrome;
        const float sc = Gw2Ui::TextScale();
        const float bandH = std::max(24.f * sc, Gw2Ui::MeasureWrappedHeight("Ag", 18.f, 0.f) + 6.f * sc);
        const ImGuiIO& io = ImGui::GetIO();

        // Hover overlay (no window): if it was revealed last frame, submit its INPUT at the band BEFORE the body
        // (so its buttons win the click), then paint its VISUAL on top AFTER. Everything is drawn inline in the
        // body child -> clipped to the panel, never a focus-stealing top-level window.
        const bool  inputThisFrame = hover && g_hoverAnim[wdef.key] > 0.01f;
        StripHover  hv;
        ImVec2      bandPos{}; float bandW = 0.f;

        ImGui::BeginGroup();
        if (wdef.selfFramed)
        {
            const ImVec2 top = ImGui::GetCursorScreenPos();
            bandPos = top; bandW = width;
            if (!hover)
            {
                DrawControlStrip(app, wdef, sl, top, width, bandH, false);   // pinned bar (Both)
                ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + bandH + 2.f * sc));
            }
            else if (inputThisFrame)
            {
                DrawControlStrip(app, wdef, sl, top, width, bandH, true, StripPhase::Input, &hv);
                ImGui::SetCursorScreenPos(top);
            }
            wdef.draw(app, width);
        }
        else if (Gw2Ui::BeginCard(wdef.key, width))
        {
            const float inner = Gw2Ui::CardInnerWidth();
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            bandPos = hp; bandW = inner;
            if (!hover)
            {
                DrawControlStrip(app, wdef, sl, hp, inner, bandH, false);
                ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + bandH));
                Gw2Ui::Divider(inner);
            }
            else if (inputThisFrame)
            {
                DrawControlStrip(app, wdef, sl, hp, inner, bandH, true, StripPhase::Input, &hv);
                ImGui::SetCursorScreenPos(hp);
            }
            wdef.draw(app, inner);
            Gw2Ui::EndCard();
        }
        ImGui::EndGroup();
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();

        if (hover)
        {
            // Reveal on hover, RESPECTING window stacking (IsWindowHovered -> false when the Settings window or
            // anything else is over the dashboard) and clamped to the body child's visible band (scroll). The
            // strip is INLINE, so "hover the strip" == "hover the widget rect" while the body child is on top.
            const float clipTop = ImGui::GetWindowPos().y;
            const float clipBot = clipTop + ImGui::GetWindowSize().y;
            const bool  visible = std::min(mx.y, clipBot) - std::max(mn.y, clipTop) > 2.f;
            const bool  overWidget = visible && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                                     ImGui::IsMouseHoveringRect(ImVec2(mn.x, std::max(mn.y, clipTop)),
                                                                ImVec2(mx.x, std::min(mx.y, clipBot)));
            char ctxId[96]; std::snprintf(ctxId, sizeof(ctxId), "##hctx_%s", wdef.key);
            const bool keepOpen = overWidget || g_dragKey == wdef.key
                                || hv.grip || hv.trash || hv.half || hv.chrome || ImGui::IsPopupOpen(ctxId);
            float& a = g_hoverAnim[wdef.key];
            a += (keepOpen ? 1.f : -1.f) * (io.DeltaTime > 0.f ? io.DeltaTime : 0.016f) * 8.f;
            a = std::min(std::max(a, 0.f), 1.f);
            if (a > 0.01f && visible)
                DrawControlStrip(app, wdef, sl, bandPos, bandW, bandH, true, StripPhase::Visual, &hv);
        }

        g_rows.push_back(Row{ wdef.key, mn.x, mn.y, mx.x, mx.y });
    }

    // Quick-add: a searchable card listing the widgets you don't have enabled. Opened from the header "+" or
    // an empty half-slot "+". Picking one enables it (and drops it into the empty slot as a half partner when
    // opened from a slot). Opened via ImGui::OpenPopup("##dashAdd") at panel level.
    void DrawAddModal(App& app)
    {
        ImGui::SetNextWindowSize(ImVec2(270.f, 320.f));
        if (!ImGui::BeginPopup("##dashAdd")) return;
        Gw2Ui::Label(g_addCtx.empty() ? "Add a widget" : "Add a widget (fills the slot)",
                     IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.2f);
        Gw2Ui::SearchBox("##addsearch", g_addSearch, sizeof(g_addSearch), ImGui::GetContentRegionAvail().x, "Search widgets...");
        Gw2Ui::Divider(0.f);

        std::string q = g_addSearch;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char ch){ return (char)std::tolower(ch); });

        // Opened from a half-slot "+" (g_addCtx set) -> the pick becomes the half partner beside it, so only
        // offer split-capable (canHalf) widgets; a full-width-only one can't pair into that slot.
        const bool halfOnly = !g_addCtx.empty();

        std::string picked;
        ImGui::BeginChild("##addlist", ImVec2(0.f, 232.f), false);
        int rowIdx = 0;
        for (const DashWidget& wd : DashWidgets())   // the widgets you don't currently have on the dashboard
        {
            if (app.config.dashLayout.Shown(wd.key)) continue;
            if (halfOnly && !wd.canHalf) continue;   // half-slot partner must be half-width-capable
            if (!q.empty())
            {
                std::string t = wd.title;
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch){ return (char)std::tolower(ch); });
                if (t.find(q) == std::string::npos) continue;
            }
            if (Gw2Ui::MenuItem(wd.title, false, 30.f, rowIdx++)) picked = wd.key;
        }
        if (rowIdx == 0)
            Gw2Ui::Label(halfOnly ? "No half-width widgets available." : "All widgets are already shown.",
                         Gw2Ui::kTextDim, false, nullptr, 14.f);
        ImGui::EndChild();

        if (!picked.empty()) { EnableWidget(app, picked, g_addCtx); app.settingsDirty = true; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Confirm card for "Disable and remove" (a widget is only hidden -> re-addable with +). Opened via
    // ImGui::OpenPopup("##dashRemove") at panel level; the target is g_removeKey.
    void DrawRemoveConfirm(App& app)
    {
        if (!ImGui::BeginPopup("##dashRemove")) return;
        const DashWidget* wd = FindDashWidget(g_removeKey.c_str());
        char msg[128]; std::snprintf(msg, sizeof(msg), "Disable \"%s\"?", wd ? wd->title : "this widget");
        const float tw = 300.f;
        const float th = Gw2Ui::MeasureWrappedHeight(msg, 16.f, tw);
        const ImVec2 mp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(tw, th));
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), mp, ImVec2(mp.x + tw, mp.y + th), msg,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(236, 230, 212, 255), false, nullptr, 16.f, tw);
        ImGui::Spacing();
        Gw2Ui::Label("You can re-enable it anytime with +.", Gw2Ui::kTextSub, false, nullptr, 14.f);
        ImGui::Spacing();
        if (Gw2Ui::ActionButton("Yes", 90.f, 26.f, Gw2Ui::ActionButtonVariant::Danger)) { app.config.dashLayout.Remove(g_removeKey); app.settingsDirty = true; g_removeKey.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(0.f, 8.f);
        if (Gw2Ui::ActionButton("No",  90.f, 26.f)) { g_removeKey.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Shared body of the New / Rename / Duplicate name prompt (reads g_profPrompt / g_profPromptIdx / g_profNameBuf).
    // Drawn inside a BeginPopup by both the panel ("##profPromptP") and the settings tab ("##profPromptS").
    void DrawProfilePromptBody(App& app, bool actionStyle)
    {
        const char* title   = g_profPrompt == ProfPrompt::Rename    ? "Rename profile"
                            : g_profPrompt == ProfPrompt::Duplicate ? "Duplicate profile" : "Name new profile";
        const char* okLabel = g_profPrompt == ProfPrompt::Rename    ? "Rename"
                            : g_profPrompt == ProfPrompt::Duplicate ? "Duplicate" : "Create";
        Gw2Ui::Label(title, IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.2f);
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        Gw2Ui::TextBox("##profname", g_profNameBuf, sizeof(g_profNameBuf), 240.f);
        const bool enter = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) ||
                           ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeyPadEnter), false);
        ImGui::Spacing();
        bool commit = enter;
        if (actionStyle)
        {
            if (Gw2Ui::ActionButton(okLabel, 110.f, 26.f, Gw2Ui::ActionButtonVariant::Primary)) commit = true;
        }
        else if (Gw2Ui::Button(okLabel, 110.f, 26.f)) commit = true;
        ImGui::SameLine(0.f, 8.f);
        if (actionStyle)
        {
            if (Gw2Ui::ActionButton("Cancel", 90.f, 26.f)) ImGui::CloseCurrentPopup();
        }
        else if (Gw2Ui::Button("Cancel", 90.f, 26.f)) ImGui::CloseCurrentPopup();
        if (commit && g_profNameBuf[0] != '\0')
        {
            if      (g_profPrompt == ProfPrompt::Rename)    RenameProfile(app, g_profPromptIdx, g_profNameBuf);
            else if (g_profPrompt == ProfPrompt::Duplicate) DuplicateProfile(app, g_profPromptIdx, g_profNameBuf);
            else                                            CreateProfile(app, g_profNameBuf);
            ImGui::CloseCurrentPopup();
        }
    }

    // Header profile selector: search box + the profile list (active highlighted) + a separated "Create New".
    // Opened from the header chip via g_profMenuReq -> OpenPopup("##dashProfiles").
    void DrawProfileMenu(App& app)
    {
        ImGui::SetNextWindowSize(ImVec2(232.f, 300.f));
        if (!ImGui::BeginPopup("##dashProfiles")) return;
        Profiles::IProfileHost& host = ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash);
        Gw2Ui::Label("Dashboard profiles", IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.2f);
        Gw2Ui::SearchBox("##profsearch", g_profSearch, sizeof(g_profSearch), ImGui::GetContentRegionAvail().x, "Search profiles...");
        Gw2Ui::Divider(0.f);

        std::string q = g_profSearch;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char ch){ return (char)std::tolower(ch); });

        int switchTo = -1;
        ImGui::BeginChild("##proflist", ImVec2(0.f, 196.f), false);
        int rowIdx = 0;
        for (int i = 0; i < host.Count(); ++i)
        {
            const std::string nm = host.NameAt(i);
            if (!q.empty())
            {
                std::string t = nm;
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch){ return (char)std::tolower(ch); });
                if (t.find(q) == std::string::npos) continue;
            }
            if (Gw2Ui::MenuItem(nm.c_str(), i == host.Active(), 30.f, rowIdx++)) switchTo = i;
        }
        if (rowIdx == 0) Gw2Ui::Label("No profiles match.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        ImGui::EndChild();

        Gw2Ui::Divider(0.f);
        if (Gw2Ui::MenuItem("+ Create New", false, 30.f, 0))   // separated below the divider
        {
            g_profPrompt = ProfPrompt::New;
            std::snprintf(g_profNameBuf, sizeof(g_profNameBuf), "%s", host.Suggest("Profile").c_str());
            g_profPromptReq = true;            // deferred so it isn't a child of this (closing) popup
            ImGui::CloseCurrentPopup();
        }

        if (switchTo >= 0) { SwitchProfile(app, switchTo); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void Dashboard::Render(App& app)
{
    if (!app.config.dashEnabled) return;
    // The per-character dashboard profile is bound + applied by the ConfigProfiles Dash family (entry.cpp TickAll);
    // here we just keep the applied layout reconciled against the widget registry.
    Reconcile(app);

    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 2.f || H < 2.f) return;
    const float ui = Gw2Ui::GlobalScale();

    const float dt = io.DeltaTime > 0.f ? io.DeltaTime : 0.016f;
    g_anim += (g_open ? 1.f : -1.f) * dt * 8.f;
    g_anim = std::max(0.f, std::min(1.f, g_anim));

    const int edge = app.config.dashEdge;   // 0 Top,1 Bottom,2 Left,3 Right
    const bool vert = (edge == 2 || edge == 3);   // handle slides off a vertical edge (left/right)
    const float hT = kT * ui, hL = kL * ui;
    const float m = 8.f * ui;

    // ---- handle geometry (recomputed each frame from dashPos) ----
    ImVec2 hMin, hMax;
    if (edge == 2)      { float y0 = std::min(std::max(app.config.dashPos * H - hL * 0.5f, m), H - hL - m); hMin = ImVec2(0.f, y0);       hMax = ImVec2(hT, y0 + hL); }
    else if (edge == 3) { float y0 = std::min(std::max(app.config.dashPos * H - hL * 0.5f, m), H - hL - m); hMin = ImVec2(W - hT, y0);    hMax = ImVec2(W, y0 + hL); }
    else if (edge == 0) { float x0 = std::min(std::max(app.config.dashPos * W - hL * 0.5f, m), W - hL - m); hMin = ImVec2(x0, 0.f);       hMax = ImVec2(x0 + hL, hT); }
    else                { float x0 = std::min(std::max(app.config.dashPos * W - hL * 0.5f, m), W - hL - m); hMin = ImVec2(x0, H - hT);    hMax = ImVec2(x0 + hL, H); }

    // ---- handle window (small, interactive so clicks/drag don't leak to the game) ----
    ImGui::SetNextWindowPos(hMin);
    ImGui::SetNextWindowSize(ImVec2(hMax.x - hMin.x, hMax.y - hMin.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##tcDashHandle", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    {
        ImGui::InvisibleButton("hb", ImVec2(hMax.x - hMin.x, hMax.y - hMin.y));
        if (ImGui::IsItemActivated()) { g_hDown = true; g_hDragged = false; g_hStart = io.MousePos; }
        // When locked the handle still toggles open/close (g_hDragged stays false -> the deactivate below
        // Toggles), but it can't be dragged to reposition.
        if (g_hDown && ImGui::IsItemActive() && !app.config.dashLocked)
        {
            const float dx = io.MousePos.x - g_hStart.x, dy = io.MousePos.y - g_hStart.y;
            if (std::sqrt(dx * dx + dy * dy) > 4.f * ui) g_hDragged = true;
            if (g_hDragged)
            {
                app.config.dashPos = vert ? (io.MousePos.y / H) : (io.MousePos.x / W);
                app.config.dashPos = std::min(std::max(app.config.dashPos, 0.f), 1.f);
                app.settingsDirty = true;
            }
        }
        if (ImGui::IsItemDeactivated())
        {
            if (!g_hDragged) Toggle();
            g_hDown = false; g_hDragged = false;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hov = ImGui::IsItemHovered() || g_hDown;
        const ImU32 bg  = hov ? IM_COL32(46, 38, 24, 245) : IM_COL32(28, 23, 15, 230);
        const ImU32 brd = IM_COL32(150, 124, 70, 220);
        dl->AddRectFilled(hMin, hMax, bg, 5.f * ui);
        dl->AddRect(hMin, hMax, brd, 5.f * ui, 0, ui);
        // chevron: points toward screen centre when closed, back toward the edge when open.
        const ImVec2 c((hMin.x + hMax.x) * 0.5f, (hMin.y + hMax.y) * 0.5f);
        const float s = 4.f * ui;
        bool pointIn = !g_open;   // closed -> open it (point inward)
        const ImU32 cc = IM_COL32(245, 226, 170, 255);
        if (vert)
        {
            float dir = ((edge == 2) == pointIn) ? 1.f : -1.f;   // left edge inward = right
            dl->AddLine(ImVec2(c.x - s * dir, c.y - s), ImVec2(c.x + s * dir, c.y), cc, 2.f * ui);
            dl->AddLine(ImVec2(c.x + s * dir, c.y), ImVec2(c.x - s * dir, c.y + s), cc, 2.f * ui);
        }
        else
        {
            float dir = ((edge == 0) == pointIn) ? 1.f : -1.f;   // top edge inward = down
            dl->AddLine(ImVec2(c.x - s, c.y - s * dir), ImVec2(c.x, c.y + s * dir), cc, 2.f * ui);
            dl->AddLine(ImVec2(c.x, c.y + s * dir), ImVec2(c.x + s, c.y - s * dir), cc, 2.f * ui);
        }
        // unread badge
        const int unread = Notify::UnreadCount();
        if (unread > 0 && !g_open)
        {
            char nb[8]; std::snprintf(nb, sizeof(nb), "%d", unread > 99 ? 99 : unread);
            const ImVec2 bc(hMax.x - 6.f * ui, hMin.y + 6.f * ui);
            dl->AddCircleFilled(bc, 7.f * ui, IM_COL32(200, 60, 50, 255));
            const ImVec2 ts = ImGui::CalcTextSize(nb);
            dl->AddText(ImVec2(bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f), IM_COL32_WHITE, nb);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (g_anim <= 0.001f && !g_open) return;   // panel fully closed

    // ---- panel geometry ----
    const float panelWLocal = std::min(std::max(app.config.dashWidth, 300.f), 600.f);
    const float panelW = panelWLocal * ui;
    const float headerH = 30.f * ui;
    float panelH = headerH + g_bodyH + 28.f * ui;
    panelH = std::min(std::max(panelH, 160.f * ui), 0.80f * H);

    const float hCenterY = (hMin.y + hMax.y) * 0.5f;
    const float hCenterX = (hMin.x + hMax.x) * 0.5f;
    ImVec2 pPos;
    const float slide = (1.f - g_anim) * 16.f * ui;
    if (edge == 2)      pPos = ImVec2(hT - slide,                std::min(std::max(hCenterY - panelH * 0.5f, m), H - panelH - m));
    else if (edge == 3) pPos = ImVec2(W - hT - panelW + slide,   std::min(std::max(hCenterY - panelH * 0.5f, m), H - panelH - m));
    else if (edge == 0) pPos = ImVec2(std::min(std::max(hCenterX - panelW * 0.5f, m), W - panelW - m), hT - slide);
    else                pPos = ImVec2(std::min(std::max(hCenterX - panelW * 0.5f, m), W - panelW - m), H - hT - panelH + slide);

    ImGui::SetNextWindowPos(pPos);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.05f, g_anim));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f * ui);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, ui);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * ui, 8.f * ui));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.038f, 0.028f, 0.975f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.50f, 0.42f, 0.24f, 0.55f));
    ImGui::Begin("##tcDashPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    {
        // header: [+] add  [book] open guide (left)  |  centered title  |  [x] close (right). Every icon has a tooltip.
        const ImVec2 hbase = ImGui::GetCursorScreenPos();
        const float cw = ImGui::GetContentRegionAvail().x;
        const float rowH = 24.f * ui;
        ImDrawList* hdl = ImGui::GetWindowDrawList();

        // + add
        ImGui::SetCursorScreenPos(hbase);
        ImGui::InvisibleButton("##dashadd", ImVec2(22.f * ui, rowH));
        const bool addHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) { g_addReq = true; g_addCtx.clear(); }
        if (addHov) Gw2Ui::Tooltip("Add a widget");
        {
            const ImU32 ac = addHov ? Gw2Ui::kGold : IM_COL32(190, 170, 120, 230);
            const ImVec2 c(hbase.x + 10.f * ui, hbase.y + rowH * 0.5f);
            hdl->AddLine(ImVec2(c.x - 6.f * ui, c.y), ImVec2(c.x + 6.f * ui, c.y), ac, 2.f * ui);
            hdl->AddLine(ImVec2(c.x, c.y - 6.f * ui), ImVec2(c.x, c.y + 6.f * ui), ac, 2.f * ui);
        }

        // open Hub (same action + icon the viewer uses)
        const ImVec2 gbase(hbase.x + 26.f * ui, hbase.y);
        ImGui::SetCursorScreenPos(gbase);
        ImGui::InvisibleButton("##dashguide", ImVec2(22.f * ui, rowH));
        const bool gHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) OpenSettingsTab(app, SettingsTabHub);
        if (gHov) Gw2Ui::Tooltip("Open Hub");
        Gw2Ui::GuideGlyph(hdl, ImVec2(gbase.x + 11.f * ui, gbase.y + rowH * 0.5f),
                          gHov ? IM_COL32(255, 220, 150, 255) : IM_COL32(200, 178, 130, 235));

        // profile chip (name + caret) just left of the X -> opens the profile selector menu
        Profiles::IProfileHost& profHost = ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash);
        std::string profNameS = profHost.Count() ? profHost.NameAt(profHost.Active()) : std::string("Default");
        const char* profName = profNameS.c_str();
        const float caretW = 13.f * ui, chipPad = 8.f * ui;
        const float nameW  = Gw2Ui::MeasureWidth(profName, 14.f);
        const float chipW  = std::min(150.f * ui, nameW + caretW + chipPad * 2.f);
        const float chipRight = hbase.x + cw - 16.f * ui - 8.f * ui;   // 8px gap before the X
        const float chipLeft  = chipRight - chipW;
        const ImVec2 chipMin(chipLeft, hbase.y + (rowH - 20.f * ui) * 0.5f), chipMax(chipRight, chipMin.y + 20.f * ui);
        ImGui::SetCursorScreenPos(chipMin);
        ImGui::InvisibleButton("##dashprof", ImVec2(chipW, 20.f));
        const bool pHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) g_profMenuReq = true;
        if (pHov) Gw2Ui::Tooltip("Switch dashboard profile");
        hdl->AddRectFilled(chipMin, chipMax, pHov ? IM_COL32(60, 50, 30, 235) : IM_COL32(38, 31, 20, 210), 3.f * ui);
        hdl->AddRect(chipMin, chipMax, IM_COL32(130, 108, 64, 200), 3.f * ui, 0, ui);
        const float caretX = chipMax.x - caretW;
        {
            float fs = 14.f;
            const float avail = (caretX - 2.f * ui) - (chipMin.x + chipPad);
            const float meas  = Gw2Ui::MeasureWidth(profName, fs);
            if (meas > avail && meas > 1.f) fs = std::max(9.f, fs * (avail / meas));
            Gw2Ui::LabelDL(hdl, ImVec2(chipMin.x + chipPad, chipMin.y), ImVec2(caretX - 2.f * ui, chipMax.y), profName,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, pHov ? Gw2Ui::kGold : IM_COL32(220, 206, 170, 255), false, nullptr, fs);
        }
        const ImVec2 cc(caretX + caretW * 0.5f - 1.f * ui, chipMin.y + 10.f * ui);
        const ImU32  caretCol = pHov ? Gw2Ui::kGold : IM_COL32(200, 182, 140, 235);
        Render::DrawGlyph(hdl, cc, 12.f * ui, Render::Glyph::CaretDown, caretCol, { false, false, false });

        // centred title, bounded clear of the left icons and the chip (auto-fit so it never overflows)
        const float tLeft = hbase.x + 52.f * ui, tRight = chipLeft - 6.f * ui;
        float tFs = 20.f;
        const float tAvail = tRight - tLeft, tMeas = Gw2Ui::MeasureWidth("Dashboard", tFs);
        if (tMeas > tAvail && tMeas > 1.f) tFs = std::max(12.f, tFs * (tAvail / tMeas));
        Gw2Ui::LabelDL(hdl, ImVec2(tLeft, hbase.y), ImVec2(tRight, hbase.y + rowH), "Dashboard",
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(255, 244, 207, 255), false, nullptr, tFs, 0.f, 1.3f);

        const ImVec2 xp(hbase.x + cw - 16.f * ui, hbase.y + (rowH - 18.f * ui) * 0.5f);
        ImGui::SetCursorScreenPos(xp);
        ImGui::InvisibleButton("##dashclose", ImVec2(16.f * ui, 18.f * ui));
        const bool xhov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) Toggle();
        if (xhov) Gw2Ui::Tooltip("Close dashboard");
        const ImU32 xc = xhov ? IM_COL32(255, 200, 200, 255) : IM_COL32(180, 168, 140, 255);
        hdl->AddLine(ImVec2(xp.x + 2.f * ui, xp.y + 3.f * ui), ImVec2(xp.x + 13.f * ui, xp.y + 14.f * ui), xc, 1.6f * ui);
        hdl->AddLine(ImVec2(xp.x + 13.f * ui, xp.y + 3.f * ui), ImVec2(xp.x + 2.f * ui, xp.y + 14.f * ui), xc, 1.6f * ui);

        ImGui::SetCursorScreenPos(ImVec2(hbase.x, hbase.y + rowH));
        Gw2Ui::Divider(0.f);

        // body (scrolls when content exceeds the clamped panel height). All widget text + measured row
        // heights scale by the user's "Text size" setting (one knob, applied uniformly).
        g_rows.clear();
        Gw2Ui::PushTextScale(std::min(std::max(app.config.dashTextScale, 1.0f), 1.6f));
        ImGui::BeginChild("##dashBody", ImVec2(0, 0), false);
        {
            const float bodyW = ImGui::GetContentRegionAvail().x;
            const float gap = 8.f * Gw2Ui::TextScale();
            // Too narrow for a usable side-by-side pair -> collapse to ONE full-width column: every widget
            // stacks (its draw() renders compact, since the width drops below DashUtil::kNarrowWidth) and no
            // lone-half "+ Add" placeholder shows. The slot's span stays 2, so widening the panel re-pairs automatically.
            const bool allowHalf = ((bodyW - gap) * 0.5f) >= 190.f * Gw2Ui::TextScale();   // kHalfMin (tunable)
            // gather shown widgets in layout order (app.config.dashLayout.items are all shown -- no holes)
            std::vector<std::pair<const DashSlot*, const DashWidget*>> show;
            for (const DashSlot& s : app.config.dashLayout.items)
            {
                const DashWidget* wd = FindDashWidget(s.key.c_str());
                if (wd) show.push_back({ &s, wd });
            }
            for (size_t i = 0; i < show.size(); ++i)
            {
                const DashSlot* s = show[i].first; const DashWidget* wd = show[i].second;
                const bool half = allowHalf && s->span == 2 && wd->canHalf;
                const bool pairNext = half && (i + 1 < show.size()) &&
                                      show[i + 1].first->span == 2 && show[i + 1].second->canHalf;
                if (pairNext)
                {
                    const float halfW = (bodyW - gap) * 0.5f;
                    DrawCard(app, *wd, halfW);
                    ImGui::SameLine(0.f, gap);
                    DrawCard(app, *show[i + 1].second, halfW);
                    ++i;
                }
                else if (half)   // lone half -> render at half width with a "+ Add" partner slot beside it
                {
                    const float halfW = (bodyW - gap) * 0.5f;
                    DrawCard(app, *wd, halfW);
                    ImGui::SameLine(0.f, gap);
                    DrawAddPlaceholder(halfW, s->key);
                }
                else
                {
                    DrawCard(app, *wd, bodyW);
                }
            }
            g_bodyH = ImGui::GetCursorPosY();

            // Edge auto-scroll while a widget is held: reaching for a drop spot that is scrolled out of view is
            // otherwise impossible, because the drop target is hit-tested against g_rows -- the rows currently
            // LAID OUT -- so anything off-screen simply cannot be dropped onto. Must live inside the child:
            // SetScrollY applies to the current window, and the drag-reorder block below runs after ImGui::End().
            if (!g_dragKey.empty())
            {
                const float maxScroll = ImGui::GetScrollMaxY();
                if (maxScroll > 0.f)
                {
                    const ImVec2 cMin = ImGui::GetWindowPos();
                    const ImVec2 cSize = ImGui::GetWindowSize();
                    const float zone = 40.f * Gw2Ui::GlobalScale();      // grab band at each edge
                    const float maxPxPerSec = 900.f * Gw2Ui::GlobalScale();
                    // Only while the cursor is over this column, but ABOVE/BELOW the edge counts too -- you
                    // naturally overshoot past the top when reaching for the first slot.
                    if (io.MousePos.x >= cMin.x && io.MousePos.x <= cMin.x + cSize.x)
                    {
                        // How deep into the band we are, 0..1, so it eases in instead of jumping to full speed.
                        float dir = 0.f, depth = 0.f;
                        if (io.MousePos.y < cMin.y + zone)
                        {
                            dir = -1.f;
                            depth = (cMin.y + zone - io.MousePos.y) / zone;
                        }
                        else if (io.MousePos.y > cMin.y + cSize.y - zone)
                        {
                            dir = 1.f;
                            depth = (io.MousePos.y - (cMin.y + cSize.y - zone)) / zone;
                        }
                        if (dir != 0.f)
                        {
                            depth = std::min(depth, 1.f);
                            // DeltaTime-scaled: a fixed per-frame step would fly on a high-refresh display.
                            const float step = dir * depth * maxPxPerSec * ImGui::GetIO().DeltaTime;
                            ImGui::SetScrollY(std::min(std::max(ImGui::GetScrollY() + step, 0.f), maxScroll));
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
        Gw2Ui::PopTextScale();

        // --- resize from the bottom corners: a corner grab handle (diagonal grip lines) in the panel's free
        // side PADDING column (the `##dashBody` child doesn't cover the padding, so the click lands), with the
        // corner resize cursor. Left dock -> bottom-right; right dock -> bottom-left; top/bottom -> both.
        if (g_open && g_anim > 0.95f && !app.config.dashLocked)
        {
            // A generous corner grab so it's easy to hit (the old 10x28 strip was fiddly). The hotspot extends
            // gripW inward from the free edge and gripH up from the bottom, overlapping the body's corner.
            const float gripW = 24.f * ui, gripH = 40.f * ui;
            auto cornerGrip = [&](const char* id, bool leftCorner)
            {
                const float ex = leftCorner ? pPos.x : pPos.x + panelW;        // the free vertical edge
                ImGui::SetCursorScreenPos(ImVec2(leftCorner ? ex : ex - gripW, pPos.y + panelH - gripH));
                ImGui::InvisibleButton(id, ImVec2(gripW, gripH));
                const bool active = ImGui::IsItemActive();
                const bool actHov = active || ImGui::IsItemHovered();
                if (actHov) ImGui::SetMouseCursor(leftCorner ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeNWSE);
                ImDrawList* gdl = ImGui::GetWindowDrawList();
                const ImU32 lc = actHov ? Gw2Ui::kGold : IM_COL32(150, 128, 80, 205);
                const ImVec2 c(ex, pPos.y + panelH);   // the corner
                for (int k = 0; k < 4; ++k)
                {
                    const float o = (5.f + k * 5.f) * ui;     // bigger, more grip lines so it reads as a resize handle
                    if (leftCorner) gdl->AddLine(ImVec2(c.x + o, c.y), ImVec2(c.x, c.y - o), lc, actHov ? 2.f * ui : 1.6f * ui);
                    else            gdl->AddLine(ImVec2(c.x - o, c.y), ImVec2(c.x, c.y - o), lc, actHov ? 2.f * ui : 1.6f * ui);
                }
                if (active)
                {
                    float nw;
                    if (edge == 2)      nw = io.MousePos.x - pPos.x;                 // left dock: right edge moves
                    else if (edge == 3) nw = (pPos.x + panelW) - io.MousePos.x;       // right dock: left edge moves
                    else                nw = 2.f * std::fabs(io.MousePos.x - hCenterX); // top/bottom: symmetric, centred
                    app.config.dashWidth = std::min(std::max(nw / ui, 300.f), 600.f);
                    app.settingsDirty = true;
                }
            };
            if (edge == 2)      cornerGrip("##rzBR", false);
            else if (edge == 3) cornerGrip("##rzBL", true);
            else { cornerGrip("##rzBL", true); cornerGrip("##rzBR", false); }
        }

        // quick-add + disable/remove confirm popups (opened at panel level so their ids resolve)
        if (g_addReq)             { ImGui::OpenPopup("##dashAdd");    g_addReq = false; g_addSearch[0] = '\0'; }
        if (!g_removeReq.empty()) { ImGui::OpenPopup("##dashRemove"); g_removeKey = g_removeReq; g_removeReq.clear(); }
        DrawAddModal(app);
        DrawRemoveConfirm(app);

        // profile selector menu + its (panel-side) name prompt -- the menu's "Create New" defers via g_profPromptReq
        if (g_profMenuReq) { ImGui::OpenPopup("##dashProfiles"); g_profMenuReq = false; g_profSearch[0] = '\0'; }
        DrawProfileMenu(app);
        if (g_profPromptReq) { ImGui::OpenPopup("##profPromptP"); g_profPromptReq = false; }
        if (ImGui::BeginPopup("##profPromptP")) { DrawProfilePromptBody(app, true); ImGui::EndPopup(); }

        // auto-hide: collapse the open panel when the cursor leaves it (after a short delay) and/or when you
        // click outside it. Suppressed while interacting (dragging, resizing, a popup/menu open) so it never
        // closes mid-action. Both options default off.
        if (g_open && (app.config.dashAutoHideHoverOff || app.config.dashAutoHideClickOff))
        {
            const bool overPanel  = io.MousePos.x >= pPos.x && io.MousePos.x <= pPos.x + panelW && io.MousePos.y >= pPos.y && io.MousePos.y <= pPos.y + panelH;
            const bool overHandle = io.MousePos.x >= hMin.x && io.MousePos.x <= hMax.x && io.MousePos.y >= hMin.y && io.MousePos.y <= hMax.y;
            const bool busy = !g_dragKey.empty() || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
            if (busy || overPanel || overHandle) g_lastInside = ImGui::GetTime();

            // Click-outside edge via Win32, NOT ImGui io: Nexus only feeds clicks that land on an addon window
            // into io.MouseClicked, so clicks on the game world / native GW2 UI were missed. GetAsyncKeyState
            // reads the real button regardless. Tracked every frame here so the edge never goes stale.
            static bool s_prevLDown = false;
            const bool  lDown     = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const bool  clickEdge = lDown && !s_prevLDown;
            s_prevLDown = lDown;

            if (!busy)
            {
                if (app.config.dashAutoHideHoverOff && !overPanel && !overHandle && (ImGui::GetTime() - g_lastInside) > kAutoHideDelay)
                    g_open = false;
                if (app.config.dashAutoHideClickOff && clickEdge && !overPanel && !overHandle)
                    g_open = false;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    // ---- drag-reorder + drag-to-pair: a grip is held -> show an insertion indicator at the drop spot, then
    // move on release. Half-onto-half drops LEFT/RIGHT (and pairs them); otherwise it's a before/after row drop.
    if (!g_dragKey.empty())
    {
        const DashWidget* dragWd = FindDashWidget(g_dragKey.c_str());
        DashSlot* dragSlot = SlotFor(app, g_dragKey);
        const bool dragHalf = dragWd && dragWd->canHalf;

        const Row* tgt = nullptr;
        for (const Row& r : g_rows)
        {
            if (r.key == g_dragKey) continue;
            if (io.MousePos.x >= r.x0 && io.MousePos.x <= r.x1 && io.MousePos.y >= r.y0 && io.MousePos.y <= r.y1) { tgt = &r; break; }
        }

        bool before = true, sideways = false;
        if (tgt)
        {
            const DashWidget* tWd = FindDashWidget(tgt->key.c_str());
            DashSlot* tSlot = SlotFor(app, tgt->key);
            const bool tgtHalf = tWd && tWd->canHalf && tSlot && tSlot->span == 2;
            ImDrawList* fdl = ImGui::GetForegroundDrawList();
            if (dragHalf && tgtHalf)   // pair: left or right of the target half
            {
                sideways = true;
                before = io.MousePos.x < (tgt->x0 + tgt->x1) * 0.5f;
                const float bx = before ? tgt->x0 - 2.f : tgt->x1 + 2.f;
                fdl->AddRectFilled(ImVec2(bx - 1.5f, tgt->y0), ImVec2(bx + 1.5f, tgt->y1), Gw2Ui::Alpha(Gw2Ui::kGold, 240));
            }
            else                        // before/after this row
            {
                before = io.MousePos.y < (tgt->y0 + tgt->y1) * 0.5f;
                const float by = before ? tgt->y0 - 2.f : tgt->y1 + 2.f;
                fdl->AddRectFilled(ImVec2(tgt->x0, by - 1.5f), ImVec2(tgt->x1, by + 1.5f), Gw2Ui::Alpha(Gw2Ui::kGold, 240));
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))   // released -> apply
        {
            if (tgt)
            {
                if (sideways && dragSlot) dragSlot->span = 2;   // dropping a half beside a half pairs them
                MoveSlot(app, g_dragKey, tgt->key, before);
                app.settingsDirty = true;
            }
            g_dragKey.clear();
        }
    }
}

// ------------------------------------------------------------------------------------------------------
// Settings: profile selector + management (switch dropdown, New / Rename / Duplicate / Delete). Drawn at the
// TOP of the SEC_DASHBOARD section. Shares the name prompt body + profile helpers with the header menu.
// ------------------------------------------------------------------------------------------------------
void Dashboard::DrawProfileSettings(App& app)
{
    Reconcile(app);
    Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::Dash), "dashprof",
        "Profiles save your full dashboard preset -- layout, panel width, text size and dock placement -- "
        "stored per character. Import a profile from another character below.");
}

// ------------------------------------------------------------------------------------------------------
// Settings: the SEC_DASHBOARD per-widget show + full/half list, plus up/down reorder (a reliable companion to
// the panel's drag-reorder). Iterates the layout in order.
// ------------------------------------------------------------------------------------------------------
void Dashboard::DrawWidgetSettings(App& app)
{
    Reconcile(app);
    int moveFrom = -1, moveTo = -1;
    std::string hideKey, enableKey;   // (before the card so the post-card applies can read them)
    Gw2Ui::BeginCard("dash-widgets");
    // Intro paragraphs (wrapped help prose -- the shared settings paragraph helper sizes + wraps them).
    SettingsParagraph("Arrange your dashboard: set each widget's width, reorder, or disable. Enable disabled widgets below.", IM_COL32(205, 197, 175, 255));
    SettingsParagraph("In the panel you can also drag a card by its grip; drop a half beside another half to pair them.", IM_COL32(168, 158, 136, 255));
    ImGui::Spacing();

    const float startX = ImGui::GetCursorScreenPos().x;
    const float availW = Gw2Ui::CardInnerWidth();
    const float gap = 8.f, rh = 30.f;

    // one settings cell via the shared reorder row (cell mode -> two half cells can share a row): name +
    // chrome + width toggles + up/down + remove. The Dashboard uses a trash disable icon + a cell border.
    auto drawCell = [&](int li, const DashWidget* wd, float cx, float cw, float rowTop)
    {
        DashSlot& s = app.config.dashLayout.items[li];
        Gw2Ui::ReorderRowDesc rd;
        rd.label = wd->title;
        rd.fontSize = SettingsText::Hint;
        rd.disableIcon = Gw2Ui::IconBtn::Trash;
        rd.disableTip = "Remove from the dashboard";
        rd.border = true;
        rd.canUp = (li > 0); rd.canDown = (li < (int)app.config.dashLayout.items.size() - 1);
        rd.extra[0] = { s.hoverChrome ? Gw2Ui::IconBtn::ChromeHover : Gw2Ui::IconBtn::ChromeBar, true, s.hoverChrome, "Header chrome: bar / hover" };
        rd.extra[1] = { (s.span == 2) ? Gw2Ui::IconBtn::WidthHalf : Gw2Ui::IconBtn::WidthFull, wd->canHalf, (s.span == 2), "Width: full / half" };
        char rid[48]; std::snprintf(rid, sizeof(rid), "dw_%s", s.key.c_str());
        const Gw2Ui::ReorderResult rr = Gw2Ui::ReorderRow(rid, cx, cw, rh, rd, rowTop);
        switch (rr.act)
        {
            case Gw2Ui::ReorderResult::Disable: hideKey = s.key; break;
            case Gw2Ui::ReorderResult::Down:    moveFrom = li; moveTo = li + 1; break;
            case Gw2Ui::ReorderResult::Up:      moveFrom = li; moveTo = li - 1; break;
            case Gw2Ui::ReorderResult::Extra0:  s.hoverChrome = !s.hoverChrome; app.settingsDirty = true; break;
            case Gw2Ui::ReorderResult::Extra1:  s.span = (s.span == 2) ? 1 : 2; app.settingsDirty = true; break;
            default: break;
        }
    };

    // all entries are shown (no holes), paired like the panel
    std::vector<std::pair<int, const DashWidget*>> shown;
    for (int i = 0; i < (int)app.config.dashLayout.items.size(); ++i)
        { const DashWidget* wd = FindDashWidget(app.config.dashLayout.items[i].key.c_str()); if (wd) shown.push_back({ i, wd }); }

    for (size_t k = 0; k < shown.size(); ++k)
    {
        const float rowTop = ImGui::GetCursorScreenPos().y;
        ImGui::Dummy(ImVec2(availW, rh));
        const DashSlot& s0 = app.config.dashLayout.items[shown[k].first];
        const bool half0 = (s0.span == 2 && shown[k].second->canHalf);
        const bool pair = half0 && (k + 1 < shown.size()) &&
                          app.config.dashLayout.items[shown[k + 1].first].span == 2 && shown[k + 1].second->canHalf;
        if (pair)
        {
            const float hw = (availW - gap) * 0.5f;
            drawCell(shown[k].first,     shown[k].second,     startX,            hw, rowTop);
            drawCell(shown[k + 1].first, shown[k + 1].second, startX + hw + gap, hw, rowTop);
            ++k;
        }
        else drawCell(shown[k].first, shown[k].second, startX, availW, rowTop);
        ImGui::SetCursorScreenPos(ImVec2(startX, rowTop + rh));
        ImGui::Spacing();
    }
    ImGui::Dummy(ImVec2(availW, 2.f));   // pad the card height so it covers the last row
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));

    // disabled widgets (registry entries not on the dashboard) -> a simple "click + to enable" list
    bool anyHidden = false;
    for (const DashWidget& w : DashWidgets()) if (!app.config.dashLayout.Shown(w.key)) { anyHidden = true; break; }
    if (anyHidden)
    {
        Gw2Ui::Label("Disabled widgets", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("dash-disabled");
        const float dStartX = ImGui::GetCursorScreenPos().x;
        const float dAvail = Gw2Ui::CardInnerWidth();
        for (const DashWidget& wd : DashWidgets())
        {
            if (app.config.dashLayout.Shown(wd.key)) continue;
            char rid[48]; std::snprintf(rid, sizeof(rid), "dd_%s", wd.key);
            if (Gw2Ui::EnableRow(rid, dStartX, dAvail, 26.f, wd.title, -1, SettingsText::Hint)) enableKey = wd.key;
        }
        ImGui::Dummy(ImVec2(dAvail, 2.f));
        Gw2Ui::EndCard();
    }

    if (moveFrom >= 0 && moveTo >= 0 && moveTo < (int)app.config.dashLayout.items.size()) { std::swap(app.config.dashLayout.items[moveFrom], app.config.dashLayout.items[moveTo]); app.settingsDirty = true; }
    if (!hideKey.empty())   { app.config.dashLayout.Remove(hideKey); app.settingsDirty = true; }
    if (!enableKey.empty()) { EnableWidget(app, enableKey, ""); app.settingsDirty = true; }
}

// Layout persistence + per-character profile switching now flow through ConfigProfiles (Owner::Dash): the
// dashLayout slice is (de)serialized by the LayoutIO in RegisterConfig, and Serialize/DeserializeAll +
// TickAll drive it. There is no bespoke SerializeLayout/DeserializeLayout/ProfileHost here anymore.
