#include "InfoPanel.h"
#include "InfoData.h"
#include "app/App.h"                     // Config (app.config.info*)
#include "app/AccountData.h"            // AccountData::HasKey (no-key data-text gating)
#include "Shared.h"                      // IsInCombat / IsMapOpen
#include "ui/Gw2Ui.h"
#include "ui/SettingsWindow.h"           // OpenSettingsTab + OpenOptionsSection
#include "ui/tabs/SettingsModel.h"       // SEC_INFO + DrawSettingSection (scalar model rows)
#include "ui/profiles/ProfileBar.h"      // Profiles::DrawProfileBar
#include "ui/profiles/ConfigProfiles.h"  // ConfigProfiles::Host / RegisterLayout / Owner
#include "ui/LayoutOrderJson.h"          // OrderedToJson / OrderedFromJson (text layout persistence)
#include "ui/LayoutTypes.h"              // InfoSlot (also via Config.h, but explicit)
#include "ui/tabs/SettingsCommon.h"      // SettingsParagraph + SettingsText
#include "api/core/Json.h"               // Api::Json null-safe readers (text layout load)
#include "render/LockIcon.h"             // Render::DrawLock (locked-waypoint padlock)
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// The Info Panel data-text bar. EVERY setting lives in Config (config.info* scalars + config.infoTexts placement +
// config.infoTextOpts per-text options) and is per-character via the Info ConfigProfileFamily (see
// ui/profiles/ConfigProfiles.h) -- there is no bespoke payload/store here anymore. Three zones (0 Left, 1 Center,
// 2 Right) spread across a full-width strip docked top/bottom. The data values come from the HudData catalog
// (which reads the shared AccountData hub, gated by NeededDomains).
namespace
{
    enum class MenuKind { None, Seg, Bar };
    MenuKind    g_menuReq = MenuKind::None;
    std::string g_menuKey;
    bool        g_popupReq = false;   // a left-click on a popup-text -> open its list popup this frame
    std::string g_popupKey;
    bool        g_textMenuOpen = false;   // a right-click on a data text -> open its unified context menu this frame
    bool        g_barMenuOpen = false;    // a right-click on the empty bar -> open the panel/add menu this frame

    InfoSlot MakeText(const std::string& key)   // new/re-enabled -> its own default zone
    {
        const InfoData::HudText* t = InfoData::FindText(key.c_str());
        InfoSlot s; s.key = key; s.zone = t ? t->defaultZone : 0; return s;   // each text carries its own default zone
    }
    std::vector<UiLayout::Ordered<InfoSlot>::Reg> BuildReg()
    {
        std::vector<UiLayout::Ordered<InfoSlot>::Reg> reg;
        for (const InfoData::HudText& t : InfoData::HudTexts()) reg.push_back({ t.key, t.defaultOn, [k = t.key] { return MakeText(k); } });
        return reg;
    }
    // The catalog default layout: each HudText carries its own default (defaultOn = shown out of the box,
    // defaultZone = which zone, defaultOrder = bar order among default-on texts). No list here.
    UiLayout::Ordered<InfoSlot> DefaultTexts()
    {
        UiLayout::Ordered<InfoSlot> o;
        std::vector<const InfoData::HudText*> on;
        for (const InfoData::HudText& t : InfoData::HudTexts()) if (t.defaultOn) on.push_back(&t);
        std::sort(on.begin(), on.end(), [](const InfoData::HudText* a, const InfoData::HudText* b) { return a->defaultOrder < b->defaultOrder; });
        for (const InfoData::HudText* t : on) o.items.push_back(MakeText(t->key));
        return o;
    }

    int FindSlotIdx(App& app, const std::string& key)
    {
        auto& items = app.config.infoTexts.items;
        for (int i = 0; i < (int)items.size(); ++i) if (items[i].key == key) return i;
        return -1;
    }

    // The standing per-text option store: options live in config.infoTextOpts keyed by text key, NOT in the slot,
    // so they survive removal and are editable before a text is ever placed (mirrors how a widget's app.config
    // option always exists). The entry is created on first touch (operator[]); an EMPTY map reads back as
    // all-default (HudOpts::Get falls back on a missing key), and capture skips empty maps -- so a created-but-
    // untouched entry never persists.
    const std::map<std::string, int>* OptsPtr(App& app, const std::string& key) { return &app.config.infoTextOpts[key]; }

    // A single data text's options subsection (its own page in the Info Panel section's left submenu).
    void DrawTextOptionsPage(App& app, const char* key)
    {
        const InfoData::HudText* def = InfoData::FindText(key);
        if (!def) return;
        Gw2Ui::Label(def->title, IM_COL32(255, 244, 207, 255), false, nullptr, 18.f, 1.2f);
        SettingsParagraph("Settings for this data text in the current profile. Zone/order/show are also on the "
                          "Layout & texts page.", IM_COL32(168, 158, 136, 255));
        ImGui::Spacing();
        // UNGATED: options persist whether or not the text is placed, so this page is always reachable. "Show this
        // text" just toggles PLACEMENT via the shared Ordered model.
        Gw2Ui::Label("Placement", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-textopt-place");
        bool shown = app.config.infoTexts.Shown(key);
        if (Gw2Ui::Checkbox("Show this text", &shown))
        {
            if (shown) { std::string k = key; app.config.infoTexts.Enable(k, [k] { return MakeText(k); }); }
            else       app.config.infoTexts.Remove(std::string(key));
            app.settingsDirty = true;
        }
        const int si = shown ? FindSlotIdx(app, key) : -1;   // re-query AFTER the toggle (Enable may realloc items)
        if (si >= 0)
        {
            static const char* kZones[] = { "Left", "Center", "Right" };
            Gw2Ui::Label("Zone", Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
            if (Gw2Ui::Dropdown("##zsel", kZones, 3, &app.config.infoTexts.items[si].zone, 220.f)) app.settingsDirty = true;
        }
        else Gw2Ui::Label("Not on the panel -- options below are still saved.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        Gw2Ui::EndCard();
        ImGui::Dummy(ImVec2(0.f, 6.f));

        const std::vector<InfoData::HudOption>& opts = InfoData::OptionsFor(key);
        if (opts.empty()) { Gw2Ui::Label("This text has no extra options.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
        std::map<std::string, int>& m = app.config.infoTextOpts[key];   // standing store (created on first touch; empty = all-default)

        // Options card -- the per-text bool/int/enum controls.
        Gw2Ui::Label("Options", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-textopt-options");
        for (const InfoData::HudOption& op : opts)
        {
            const auto it = m.find(op.key);
            if (op.kind == InfoData::HudOption::Kind::Bool)
            {
                bool v = (it != m.end()) ? it->second != 0 : op.def != 0;
                if (Gw2Ui::Checkbox(op.label, &v)) { m[op.key] = v ? 1 : 0; app.settingsDirty = true; }
            }
            else if (op.kind == InfoData::HudOption::Kind::Int)
            {
                int v = (it != m.end()) ? it->second : op.def;
                Gw2Ui::Label(op.label, Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
                ImGui::PushID(op.key);
                if (Gw2Ui::SliderInt("##o", &v, op.imin, op.imax, 150.f)) { m[op.key] = v; app.settingsDirty = true; }
                ImGui::PopID();
            }
            else
            {
                int v = (it != m.end()) ? it->second : op.def;
                Gw2Ui::Label(op.label, Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
                ImGui::PushID(op.key);
                if (Gw2Ui::Dropdown("##o", op.enames, op.ecount, &v, 220.f)) { m[op.key] = v; app.settingsDirty = true; }
                ImGui::PopID();
            }
        }
        Gw2Ui::EndCard();
    }
}

void InfoPanel::RegisterConfig()
{
    ConfigProfiles::RegisterLayout(ConfigProfiles::Owner::Info, {
        // capture: config structured fields -> the slice json
        [](App& app, nlohmann::json& j) {
            j["infoTexts"] = UiLayout::OrderedToJson(app.config.infoTexts, [](const InfoSlot& s, nlohmann::json& e) { e["zone"] = s.zone; });
            nlohmann::json topts = nlohmann::json::object();
            for (const auto& kv : app.config.infoTextOpts) if (!kv.second.empty()) topts[kv.first] = kv.second;   // skip all-default
            j["infoTextOpts"] = std::move(topts);
        },
        // apply: slice json -> config structured fields, then reconcile against the text catalog
        [](App& app, const nlohmann::json& j) {
            // NEW-format standing option store (authoritative for current saves). Read BEFORE texts so the legacy
            // per-slot "opts" migration below can tell whether a key already has values.
            app.config.infoTextOpts.clear();
            if (j.contains("infoTextOpts") && j["infoTextOpts"].is_object())
                for (auto it = j["infoTextOpts"].begin(); it != j["infoTextOpts"].end(); ++it)
                    if (it.value().is_object())
                        for (auto o = it.value().begin(); o != it.value().end(); ++o)
                            if (o.value().is_number_integer()) app.config.infoTextOpts[it.key()][o.key()] = o.value().get<int>();
            if (j.contains("infoTexts"))
                UiLayout::OrderedFromJson(app.config.infoTexts, j["infoTexts"],
                    [&app](const nlohmann::json& s) {
                        InfoSlot c; c.key = Api::Json::Str(s, "key"); c.zone = Api::Json::Int(s, "zone", 0);
                        // MIGRATE legacy per-slot "opts" -> standing store (only if the new store lacks this key).
                        if (s.contains("opts") && s["opts"].is_object() && app.config.infoTextOpts.find(c.key) == app.config.infoTextOpts.end())
                            for (auto it = s["opts"].begin(); it != s["opts"].end(); ++it)
                                if (it.value().is_number_integer()) app.config.infoTextOpts[c.key][it.key()] = it.value().get<int>();
                        return c;
                    });
            app.config.infoTexts.Reconcile(BuildReg());   // drop-missing + append default-on (shared model)
        },
        // seedDefault: config structured fields <- catalog default (feeds the baked m_default)
        [](App& app) { app.config.infoTexts = DefaultTexts(); app.config.infoTextOpts.clear(); }
    });
}

unsigned InfoPanel::NeededDomains(App& app)
{
    if (!app.config.infoEnabled) return 0u;
    unsigned mask = 0u;
    for (const InfoSlot& s : app.config.infoTexts.items)
        { const InfoData::HudText* def = InfoData::FindText(s.key.c_str()); if (def) mask |= def->domains; }
    return mask;
}

bool InfoPanel::HasWvwText(App& app)
{
    if (!app.config.infoEnabled) return false;
    for (const InfoSlot& s : app.config.infoTexts.items)   // the live-match texts (NOT wvwrank/wvwstatus, which need no match poll)
        if (s.key == "wvwscore" || s.key == "wvwskirmish" || s.key == "wvwppt" || s.key == "wvwkd" || s.key == "wvwcontrol")
            return true;
    return false;
}

bool InfoPanel::IsEnabled(App& app)            { return app.config.infoEnabled; }
void InfoPanel::SetEnabled(App& app, bool on)  { if (app.config.infoEnabled != on) { app.config.infoEnabled = on; app.settingsDirty = true; } }

void InfoPanel::Render(App& app)
{
    Config& cfg = app.config;
    if (!cfg.infoEnabled) return;
    if (cfg.infoHideInCombat && IsInCombat()) return;
    if (cfg.infoHideOnMap && IsMapOpen()) return;

    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 4.f || H < 4.f) return;

    const float fs = cfg.infoTextSize, labelFs = std::max(10.f, fs - 4.f);   // labels distinctly smaller than values
    const float barH = fs + 14.f, margin = 12.f, gap = 16.f;

    // The visible segment per zone. Each compute() reads live state and several do O(N) scans (wallet, Wizard's
    // Vault, deaths, zone%) -- recomputing all of them EVERY frame is the single biggest always-on CPU cost, and
    // GW2 is CPU-bound on the present thread. So compute + measure only ~5 Hz (or immediately when the character /
    // text size / screen size changes); the bar still DRAWS the cached segments every frame, and per-frame paint
    // callbacks (e.g. the route arrow) keep their motion smooth. opts is COPIED into the Seg so a cached pointer
    // can never dangle into a config.infoTexts the settings page mutated between recomputes.
    struct Seg { std::string key; InfoData::HudSeg s; float w = 0.f; std::map<std::string, int> opts; };
    static std::vector<Seg> s_zones[3];
    static bool        s_any   = false;
    static double      s_lastT = -1e9;
    static std::string s_char;
    static float       s_fs = -1.f, s_W = -1.f;
    auto& zones = s_zones;   // alias: the draw code below reads the cached segments

    const double now = ImGui::GetTime();
    if ((now - s_lastT) >= 0.2 || s_char != app.state.currentChar || s_fs != fs || s_W != W)
    {
        s_lastT = now; s_char = app.state.currentChar; s_fs = fs; s_W = W;
        for (auto& z : zones) z.clear();
        s_any = false;
        for (const InfoSlot& slot : cfg.infoTexts.items)
        {
            if (slot.zone < 0 || slot.zone > 2) continue;
            const InfoData::HudText* def = InfoData::FindText(slot.key.c_str());
            if (!def) continue;
            const std::map<std::string, int>* mp = OptsPtr(app, slot.key);
            InfoData::HudSeg seg = def->compute(app, InfoData::HudOpts{ mp });
            if (seg.value.empty() && !seg.paint) continue;
            Seg e; e.key = slot.key; e.s = std::move(seg); if (mp) e.opts = *mp;
            const float lw = e.s.label.empty() ? 0.f : Gw2Ui::MeasureWidth(e.s.label.c_str(), labelFs) + 5.f;
            if (e.s.paint) e.w = lw + (e.s.paintSize > 0.f ? e.s.paintSize : (barH - 6.f));   // square icon (route arrow)
            else           e.w = lw + (e.s.locked ? fs + 4.f : 0.f) + (e.s.icon ? fs + 6.f : 0.f) + Gw2Ui::MeasureWidth(e.s.value.c_str(), fs);   // icon = fs+2 box + 4 gap (matches the draw)
            zones[slot.zone].push_back(std::move(e));
            s_any = true;
        }
    }
    if (!s_any) return;

    const float winY = (cfg.infoEdge == 0) ? 0.f : (H - barH);
    ImGui::SetNextWindowPos(ImVec2(0.f, winY));
    ImGui::SetNextWindowSize(ImVec2(W, barH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##tcInfoPanel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (cfg.infoOpacity > 0)
        {
            const int a = (int)(cfg.infoOpacity * 2.35f);   // 0..100 -> 0..235
            dl->AddRectFilled(ImVec2(0.f, winY), ImVec2(W, winY + barH), IM_COL32(18, 15, 10, a < 0 ? 0 : (a > 235 ? 235 : a)));
            dl->AddLine(ImVec2(0.f, cfg.infoEdge == 0 ? winY + barH : winY), ImVec2(W, cfg.infoEdge == 0 ? winY + barH : winY), IM_COL32(150, 124, 70, 150), 1.f);
        }

        auto drawSeg = [&](Seg& e, float x)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, winY));
            ImGui::PushID(e.key.c_str());
            const bool lclick = ImGui::InvisibleButton("##s", ImVec2(e.w, barH));
            const bool hov = ImGui::IsItemHovered();
            const bool rclick = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            ImGui::PopID();
            const InfoData::HudText* def = InfoData::FindText(e.key.c_str());
            const bool needsKey = def && def->domains != 0u && !AccountData::HasKey();   // API text with no key entered
            const bool clickable = def && (needsKey || def->onClick || def->actions || def->popup);
            if (hov) { dl->AddRectFilled(ImVec2(x - 3.f, winY + 1.f), ImVec2(x + e.w + 3.f, winY + barH - 1.f), IM_COL32(255, 220, 140, 26), 3.f);
                       if (clickable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
            float cx = x;
            if (!e.s.label.empty())
            {
                Gw2Ui::LabelDL(dl, ImVec2(cx, winY), ImVec2(cx + 1e4f, winY + barH), e.s.label.c_str(),
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(190, 165, 110, 255), false, nullptr, labelFs);   // gold "kind" label
                cx += Gw2Ui::MeasureWidth(e.s.label.c_str(), labelFs) + 6.f;
            }
            if (e.s.paint)   // a square icon (e.g. the route arrow) instead of a value
            {
                const float isz = e.s.paintSize > 0.f ? e.s.paintSize : (barH - 6.f);
                e.s.paint(app, InfoData::HudOpts{ &e.opts }, dl, ImVec2(cx + isz * 0.5f, winY + barH * 0.5f), isz);
            }
            else
            {
                if (e.s.locked) { Render::DrawLock(dl, ImVec2(cx, winY + (barH - fs) * 0.5f), fs, true); cx += fs + 4.f; }   // padlock = locked waypoint
                if (e.s.icon) { const float is = fs + 2.f; dl->AddImage((ImTextureID)e.s.icon, ImVec2(cx, winY + (barH - is) * 0.5f), ImVec2(cx + is, winY + (barH + is) * 0.5f)); cx += is + 4.f; }   // objective type icon
                Gw2Ui::LabelDL(dl, ImVec2(cx, winY), ImVec2(cx + 1e4f, winY + barH), e.s.value.c_str(),
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, e.s.color, false, nullptr, fs);
            }
            if (hov)
            {
                if (needsKey) { if (Gw2Ui::TooltipBegin()) { Gw2Ui::TooltipTitle(def->title); Gw2Ui::TooltipMuted("Needs an API key. Click to add one in Options."); Gw2Ui::TooltipEnd(); } }
                else if (def && def->tip) def->tip(app, InfoData::HudOpts{ &e.opts });
                else if (def)        Gw2Ui::Tooltip(def->title);
            }
            if (lclick && def)
            {
                if (needsKey)         { OpenSettingsTab(app, SettingsTabOptions); OpenOptionsSection(SEC_API); }   // jump to the API-key field
                else if (def->popup)  { g_popupReq = true; g_popupKey = e.key; }   // left-click opens its list popup
                else if (def->onClick) def->onClick(app, InfoData::HudOpts{ &e.opts });
            }
            if (rclick) { g_menuKey = e.key; g_textMenuOpen = true; }   // one unified context menu for every data text
        };
        auto drawZone = [&](std::vector<Seg>& v, float startX)
        {
            float x = startX;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (i > 0) dl->AddLine(ImVec2(x - gap * 0.5f, winY + 5.f), ImVec2(x - gap * 0.5f, winY + barH - 5.f), IM_COL32(120, 108, 82, 120), 1.f);
                drawSeg(v[i], x);
                x += v[i].w + gap;
            }
        };
        auto groupW = [&](const std::vector<Seg>& v) { float w = 0.f; for (size_t i = 0; i < v.size(); ++i) w += v[i].w + (i ? gap : 0.f); return w; };
        // Center zone: anchor the MIDDLE widget on the true screen center (W/2) so it sits on the GW2 globe's
        // center line, with the other center texts flanking it -- instead of block-centering the whole group
        // (which a wide "Current Objective" would shove off-center). Odd count -> the middle widget is centered;
        // even count -> the midpoint between the two middle widgets is centered.
        auto centerStartX = [&](const std::vector<Seg>& v) -> float {
            const int n = (int)v.size();
            if (n == 0) return W * 0.5f;
            const int mid = n / 2;
            float anchor = 0.f;
            for (int i = 0; i < mid; ++i) anchor += v[i].w + gap;   // offset to the start of segment[mid]
            if (n % 2 == 1) anchor += v[mid].w * 0.5f;              // odd: center of the middle widget
            else            anchor -= gap * 0.5f;                   // even: center of the gap between the two middles
            return W * 0.5f - anchor;
        };

        if (!zones[0].empty()) drawZone(zones[0], margin);
        if (!zones[1].empty()) drawZone(zones[1], centerStartX(zones[1]));
        if (!zones[2].empty()) drawZone(zones[2], W - margin - groupW(zones[2]));

        // empty-area right-click -> panel menu. Only when the click is on EMPTY bar (no segment item hovered) --
        // a right-click on a segment is that segment's own menu, so the bar menu must not also open on top of it.
        if (g_menuReq == MenuKind::None && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            g_menuReq = MenuKind::Bar;
        if (g_menuReq == MenuKind::Bar) g_barMenuOpen = true;   // empty-area panel menu (opened below via openNow, like the data-text menu)
        g_menuReq = MenuKind::None;
        if (g_popupReq) { ImGui::OpenPopup("##ipDataPopup"); g_popupReq = false; }

        // Left-click data popup (e.g. Zone / Favorite waypoint lists) -- a GW2-dark framed window.
        {
            const InfoData::HudText* pdef = InfoData::FindText(g_popupKey.c_str());
            const std::map<std::string, int>* popts = OptsPtr(app, g_popupKey);   // by key -- works even if unplaced
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(20, 17, 11, 245));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(150, 124, 70, 220));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            if (ImGui::BeginPopup("##ipDataPopup"))
            {
                if (pdef && pdef->popup) pdef->popup(app, InfoData::HudOpts{ popts });
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        // ONE unified context menu per data text: the text's own items (its menuNodes hook, or its flat actions)
        // on top, then a shared "Bar Control" submenu (Disable / Move zone / Move panel / Settings) at the bottom.
        {
            const int si = FindSlotIdx(app, g_menuKey);   // still used below for zone / remove
            const InfoData::HudText* def = InfoData::FindText(g_menuKey.c_str());
            const std::map<std::string, int>* opts = OptsPtr(app, g_menuKey);   // by key -- independent of the slot
            const InfoData::HudOpts ho{ opts };
            constexpr int kBar = 900000;   // Bar Control ids -- well above any text's own node ids

            // Build the menu tree only when it (re)opens; ContextMenuTree still needs the nodes every frame the
            // popup is up (so cache them statically), but they were rebuilt every frame even with no menu open.
            static std::vector<Gw2Ui::MenuNode> nodes;
            if (g_textMenuOpen)
            {
                nodes.clear();
                bool special = false;
                if (def && def->menuNodes) { def->menuNodes(app, ho, nodes); special = !nodes.empty(); }
                else if (def && def->actions)
                {
                    std::vector<const char*> acts; def->actions(app, ho, acts);
                    for (int i = 0; i < (int)acts.size(); ++i) { Gw2Ui::MenuNode n; n.label = acts[i]; n.id = i; nodes.push_back(std::move(n)); }
                    special = !acts.empty();
                }
                if (special) { Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep); }

                const int zone = (si >= 0) ? app.config.infoTexts.items[si].zone : -1;
                Gw2Ui::MenuNode bar; bar.label = "Bar Control"; bar.id = -1;
                auto child = [&](const char* lbl, int id, bool sel = false) { Gw2Ui::MenuNode c; c.label = lbl; c.id = id; c.selected = sel; bar.children.push_back(std::move(c)); };
                child("Disable this", kBar + 0);
                child("Move to Left",   kBar + 1, zone == 0);
                child("Move to Center", kBar + 2, zone == 1);
                child("Move to Right",  kBar + 3, zone == 2);
                { Gw2Ui::MenuNode s; s.separator = true; bar.children.push_back(s); }
                child(app.config.infoEdge == 0 ? "Move panel to bottom" : "Move panel to top", kBar + 4);
                child("Info Panel settings...", kBar + 5);
                nodes.push_back(std::move(bar));
            }

            const int picked = Gw2Ui::ContextMenuTree("##ipTextMenu", nodes, g_textMenuOpen);
            g_textMenuOpen = false;
            if (picked >= kBar)
            {
                const int p = picked - kBar;
                if      (p == 0 && si >= 0) { app.config.infoTexts.Remove(app.config.infoTexts.items[si].key); app.settingsDirty = true; }
                else if (p >= 1 && p <= 3 && si >= 0) { app.config.infoTexts.items[si].zone = p - 1; app.settingsDirty = true; }
                else if (p == 4) { app.config.infoEdge = (app.config.infoEdge == 0) ? 1 : 0; app.settingsDirty = true; }
                else if (p == 5) { OpenSettingsTab(app, SettingsTabOptions); OpenOptionsSection(SEC_INFO, InfoData::OptionsFor(g_menuKey.c_str()).empty() ? nullptr : g_menuKey.c_str()); }
            }
            else if (picked >= 0 && def)
            {
                if (def->menuPick)     def->menuPick(app, ho, picked);
                else if (def->onAction) def->onAction(app, ho, picked);
            }
        }
        {
            // empty-area panel menu: "Add data text" submenu (texts not on the bar) -> each text is a submenu of
            // Left / Center / Right, so the whole add+zone choice stays in ONE menu tree (no second popup). The
            // panel controls follow. id of a zone leaf = textIndex*3 + zone (0=L,1=C,2=R), kept below kPanel*.
            constexpr int kPanelEdge = 800000, kPanelSettings = 800001;
            // Build the avail list (+ its title sort) and the menu tree only when the menu (re)opens.
            // ContextMenuTree needs the nodes every frame the popup is up, and the pick handler indexes into
            // `avail`, so cache BOTH statically and rebuild them together on the open trigger.
            static std::vector<std::string> avail;   // available text keys, alphabetical by title (index drives the leaf id)
            static std::vector<Gw2Ui::MenuNode> nodes;
            if (g_barMenuOpen)
            {
                avail.clear();
                for (const InfoData::HudText& t : InfoData::HudTexts()) if (!app.config.infoTexts.Shown(t.key)) avail.push_back(t.key);
                std::sort(avail.begin(), avail.end(), [](const std::string& a, const std::string& b) {
                    const InfoData::HudText* da = InfoData::FindText(a.c_str()); const InfoData::HudText* db = InfoData::FindText(b.c_str());
                    return std::string(da ? da->title : "") < std::string(db ? db->title : ""); });

                nodes.clear();
                { Gw2Ui::MenuNode add; add.label = "Add data text"; add.id = -1;
                  if (avail.empty()) { Gw2Ui::MenuNode z; z.label = "(all data texts shown)"; z.id = -1; add.children.push_back(z); }
                  else for (int i = 0; i < (int)avail.size(); ++i)
                  {
                      const InfoData::HudText* d = InfoData::FindText(avail[i].c_str());
                      Gw2Ui::MenuNode t; t.label = d ? d->title : avail[i].c_str(); t.id = -1;
                      static const char* kZ[3] = { "Left", "Center", "Right" };
                      for (int z = 0; z < 3; ++z) { Gw2Ui::MenuNode c; c.label = kZ[z]; c.id = i * 3 + z; t.children.push_back(std::move(c)); }
                      add.children.push_back(std::move(t));
                  }
                  nodes.push_back(std::move(add)); }
                { Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep); }
                { Gw2Ui::MenuNode n; n.label = app.config.infoEdge == 0 ? "Move panel to bottom" : "Move panel to top"; n.id = kPanelEdge; nodes.push_back(n); }
                { Gw2Ui::MenuNode n; n.label = "Info Panel settings..."; n.id = kPanelSettings; nodes.push_back(n); }
            }

            const int picked = Gw2Ui::ContextMenuTree("##ipBarMenu", nodes, g_barMenuOpen);
            g_barMenuOpen = false;
            if      (picked == kPanelEdge)     { app.config.infoEdge = (app.config.infoEdge == 0) ? 1 : 0; app.settingsDirty = true; }
            else if (picked == kPanelSettings) { OpenSettingsTab(app, SettingsTabOptions); OpenOptionsSection(SEC_INFO); }
            else if (picked >= 0 && picked < (int)avail.size() * 3)   // add text picked/3 to zone picked%3
            {
                const std::string key = avail[picked / 3];
                if (InfoSlot* s = app.config.infoTexts.Enable(key, [key] { return MakeText(key); })) { s->zone = picked % 3; app.settingsDirty = true; }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void InfoPanel::DrawSettings(App& app, const char* page)
{
    if (page && *page) { DrawTextOptionsPage(app, page); return; }   // a data text's options subsection

    Config& cfg = app.config;

    // --- Profiles (top) -- one universal per-character profile family, via ConfigProfiles ---
    Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::Info), "infoprof",
        "The Info Panel is a data-text bar (left / center / right zones) docked top or bottom. Everything here "
        "-- placement, text size, and which texts sit in which zone -- is saved per character in this profile; "
        "import one from another character below.");

    // --- Scalar settings: the SEC_INFO model rows (searchable + tooltipped, same look as every other section) ---
    DrawSettingSection(app, SEC_INFO);
    ImGui::Dummy(ImVec2(0.f, 6.f));

    // --- Data-text placement (structured; edits config.infoTexts) ---
    Gw2Ui::Label("Data texts", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    int moveA = -1, moveB = -1;        // swap these two vector indices (reorder within a region)
    int zoneIdx = -1, zoneTo = -1;     // send vector index zoneIdx to region zoneTo (appended to its end)
    std::string hideKey, addKey;       // (before the card so the post-card applies can read them)
    static const char* kZoneNames[3] = { "Left", "Center", "Right" };
    static const char* kZoneLetters[3] = { "L", "C", "R" };
    Gw2Ui::BeginCard("info-datatexts");
    SettingsParagraph("Texts are grouped into the Left / Center / Right regions. Reorder within a region with the "
                      "up/down arrows; click L/C/R to send a text to another region (it lands at that region's end); "
                      "the minus disables it. Disabled datatexts can be re-enabled below.", IM_COL32(168, 158, 136, 255));
    ImGui::Spacing();

    const float availW = Gw2Ui::CardInnerWidth();
    const float startX = ImGui::GetCursorScreenPos().x;
    const float rh = 28.f;

    for (int z = 0; z < 3; ++z)
    {
        // texts in this region, in vector order (their order here == their order on the bar)
        std::vector<int> rows;
        for (int i = 0; i < (int)cfg.infoTexts.items.size(); ++i)
            if (cfg.infoTexts.items[i].zone == z && InfoData::FindText(cfg.infoTexts.items[i].key.c_str()))
                rows.push_back(i);

        ImGui::Spacing();
        Gw2Ui::Label(kZoneNames[z], Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        if (rows.empty()) { Gw2Ui::Label("   (empty)", Gw2Ui::kTextDim, false, nullptr, 14.f); continue; }

        for (size_t r = 0; r < rows.size(); ++r)
        {
            const int i = rows[r];
            const InfoSlot& s = cfg.infoTexts.items[i];
            const InfoData::HudText* def = InfoData::FindText(s.key.c_str());
            Gw2Ui::ReorderRowDesc rd;
            rd.label = def->title;
            rd.note = (def->domains != 0u) ? "(needs API key)" : nullptr;
            rd.group = s.zone; rd.letters = kZoneLetters; rd.groupCount = 3;
            rd.canUp = (r > 0); rd.canDown = (r + 1 < rows.size()); rd.fontSize = SettingsText::Hint;
            char rid[24]; std::snprintf(rid, sizeof(rid), "it%d", i);
            const Gw2Ui::ReorderResult rr = Gw2Ui::ReorderRow(rid, startX, availW, rh, rd);
            if      (rr.act == Gw2Ui::ReorderResult::Disable)  hideKey = s.key;
            else if (rr.act == Gw2Ui::ReorderResult::Down)     { moveA = i; moveB = rows[r + 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::Up)       { moveA = i; moveB = rows[r - 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::SetGroup) { zoneIdx = i; zoneTo = rr.toGroup; }
        }
    }
    ImGui::Dummy(ImVec2(availW, 2.f));   // pad the card height so it covers the last row
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    // Apply ONE mutation per frame (a click only triggers one; indices shift, so never do two at once).
    if (moveA >= 0 && moveB >= 0) { std::swap(cfg.infoTexts.items[moveA], cfg.infoTexts.items[moveB]); app.settingsDirty = true; }
    else if (zoneIdx >= 0)
    {
        auto& items = cfg.infoTexts.items;
        InfoSlot moved = items[zoneIdx]; moved.zone = zoneTo;
        items.erase(items.begin() + zoneIdx);
        int insertAt = (int)items.size();   // append after the last text already in the target region
        for (int j = (int)items.size() - 1; j >= 0; --j)
            if (items[j].zone == zoneTo) { insertAt = j + 1; break; }
        items.insert(items.begin() + insertAt, moved);
        app.settingsDirty = true;
    }
    if (!hideKey.empty()) { cfg.infoTexts.Remove(hideKey); app.settingsDirty = true; }

    bool anyHidden = false;
    for (const InfoData::HudText& t : InfoData::HudTexts()) if (!cfg.infoTexts.Shown(t.key)) { anyHidden = true; break; }
    if (anyHidden)
    {
        Gw2Ui::Label("Disabled datatexts", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-disabled");
        const float dAvail = Gw2Ui::CardInnerWidth();
        const float dStartX = ImGui::GetCursorScreenPos().x;
        SettingsParagraph("Grouped by kind, alphabetical within each. Click + to enable one.",
                          IM_COL32(168, 158, 136, 255));
        ImGui::Spacing();
        // The datatexts not currently on the bar, grouped by family, alphabetical within each.
        const InfoData::HudFam fams[] = { InfoData::HudFam::Location, InfoData::HudFam::Character,
                                         InfoData::HudFam::Economy,  InfoData::HudFam::System };
        for (InfoData::HudFam fam : fams)
        {
            std::vector<std::string> group;   // keys not on the bar, in this family
            for (const InfoData::HudText& t : InfoData::HudTexts())
                if (!cfg.infoTexts.Shown(t.key) && t.family == fam) group.push_back(t.key);
            if (group.empty()) continue;
            std::sort(group.begin(), group.end(), [](const std::string& a, const std::string& b) {
                const InfoData::HudText* da = InfoData::FindText(a.c_str());
                const InfoData::HudText* db = InfoData::FindText(b.c_str());
                return std::string(da->title) < std::string(db->title);
            });
            ImGui::Spacing();
            Gw2Ui::Label(InfoData::FamilyName(fam), Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
            for (const std::string& k : group)
            {
                const InfoData::HudText* def = InfoData::FindText(k.c_str());
                char rid[40]; std::snprintf(rid, sizeof(rid), "id_%s", k.c_str());
                if (Gw2Ui::EnableRow(rid, dStartX, dAvail, 26.f, def->title, -1, SettingsText::Hint)) addKey = k;
            }
        }
        ImGui::Dummy(ImVec2(dAvail, 2.f));
        Gw2Ui::EndCard();
    }
    if (!addKey.empty()) { cfg.infoTexts.Enable(addKey, [addKey] { return MakeText(addKey); }); app.settingsDirty = true; }
}
