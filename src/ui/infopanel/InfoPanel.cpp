#include "InfoPanel.h"
#include "InfoData.h"
#include "app/App.h"
#include "app/AccountData.h"
#include "Shared.h"
#include "ui/Gw2Ui.h"
#include "ui/SettingsWindow.h"
#include "ui/tabs/SettingsModel.h"
#include "ui/profiles/ProfileBar.h"
#include "ui/profiles/ConfigProfiles.h"
#include "ui/LayoutOrderJson.h"
#include "ui/LayoutTypes.h"
#include "ui/tabs/SettingsCommon.h"
#include "api/core/Json.h"
#include "render/LockIcon.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Info Panel data texts are configured as up to five independent bars. The profile-level setting
// Config::infoEnabled is the master switch; each InfoBarConfig owns its own placement, layout, and per-text
// options. Old single-bar profile slices are migrated into infoBars[0] here.
namespace
{
    constexpr int kBarCount = 5;
    static const char* kBarNames[kBarCount] = { "Bar 1", "Bar 2", "Bar 3", "Bar 4", "Bar 5" };
    static const char* kZones[3] = { "Left", "Center", "Right" };
    static const char* kZoneLetters[3] = { "L", "C", "R" };
    static const char* kEdgeNames[2] = { "Top", "Bottom" };
    static const char* kHeightNames[] = {
        "Auto", "18 px", "20 px", "22 px", "24 px", "26 px", "28 px", "32 px", "36 px", "40 px", "44 px", "48 px", "56 px", "64 px"
    };
    static constexpr float kHeights[] = { 0.f, 18.f, 20.f, 22.f, 24.f, 26.f, 28.f, 32.f, 36.f, 40.f, 44.f, 48.f, 56.f, 64.f };
    static constexpr int kHeightCount = static_cast<int>(sizeof(kHeights) / sizeof(kHeights[0]));

    enum class MenuKind { None, Bar };

    int         g_settingsBar = 0;
    MenuKind    g_menuReq = MenuKind::None;
    int         g_menuBar = 0;
    std::string g_menuKey;
    bool        g_popupReq = false;
    int         g_popupBar = 0;
    std::string g_popupKey;
    bool        g_textMenuOpen = false;
    bool        g_barMenuOpen = false;

    InfoSlot MakeText(const std::string& key)
    {
        const InfoData::HudText* t = InfoData::FindText(key.c_str());
        InfoSlot s;
        s.key = key;
        s.zone = t ? t->defaultZone : 0;
        return s;
    }

    std::vector<UiLayout::Ordered<InfoSlot>::Reg> BuildReg(bool includeDefaultOn)
    {
        std::vector<UiLayout::Ordered<InfoSlot>::Reg> reg;
        for (const InfoData::HudText& t : InfoData::HudTexts())
            reg.push_back({ t.key, includeDefaultOn && t.defaultOn, [k = t.key] { return MakeText(k); } });
        return reg;
    }

    UiLayout::Ordered<InfoSlot> DefaultTexts()
    {
        UiLayout::Ordered<InfoSlot> o;
        std::vector<const InfoData::HudText*> on;
        for (const InfoData::HudText& t : InfoData::HudTexts())
            if (t.defaultOn) on.push_back(&t);
        std::sort(on.begin(), on.end(), [](const InfoData::HudText* a, const InfoData::HudText* b) {
            return a->defaultOrder < b->defaultOrder;
        });
        for (const InfoData::HudText* t : on)
            o.items.push_back(MakeText(t->key));
        return o;
    }

    void SeedDefaultBars(Config& cfg)
    {
        cfg.infoBars = {};
        cfg.infoBars[0].enabled = true;
        cfg.infoBars[0].texts = DefaultTexts();
    }

    void ReconcileBar(InfoBarConfig& bar, bool includeDefaultOn)
    {
        bar.texts.Reconcile(BuildReg(includeDefaultOn));
        bar.edge = std::clamp(bar.edge, 0, 1);
        bar.widthPct = std::clamp(bar.widthPct, 25, 100);
        bar.opacity = std::clamp(bar.opacity, 0, 100);
        bar.textSize = std::clamp(bar.textSize, 12.f, 32.f);
        bar.barHeight = std::clamp(bar.barHeight, 0, kHeightCount - 1);
    }

    float FixedBarHeightLogical(const InfoBarConfig& bar)
    {
        const int idx = std::clamp(bar.barHeight, 0, kHeightCount - 1);
        return kHeights[idx];
    }

    int ClampBarIndex(int index)
    {
        return std::clamp(index, 0, kBarCount - 1);
    }

    InfoBarConfig& Bar(App& app, int index)
    {
        return app.config.infoBars[ClampBarIndex(index)];
    }

    int FindSlotIdx(const InfoBarConfig& bar, const std::string& key)
    {
        const auto& items = bar.texts.items;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (items[i].key == key) return i;
        return -1;
    }

    const std::map<std::string, int>* OptsPtr(InfoBarConfig& bar, const std::string& key)
    {
        return &bar.textOpts[key];
    }

    void LoadTextOpts(InfoBarConfig& bar, const nlohmann::json& j)
    {
        bar.textOpts.clear();
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_object()) continue;
            for (auto o = it.value().begin(); o != it.value().end(); ++o)
                if (o.value().is_number_integer())
                    bar.textOpts[it.key()][o.key()] = o.value().get<int>();
        }
    }

    void LoadTexts(InfoBarConfig& bar, const nlohmann::json& j)
    {
        UiLayout::OrderedFromJson(bar.texts, j, [&bar](const nlohmann::json& s) {
            InfoSlot c;
            c.key = Api::Json::Str(s, "key");
            c.zone = Api::Json::Int(s, "zone", 0);
            if (s.contains("opts") && s["opts"].is_object() && bar.textOpts.find(c.key) == bar.textOpts.end())
                for (auto it = s["opts"].begin(); it != s["opts"].end(); ++it)
                    if (it.value().is_number_integer())
                        bar.textOpts[c.key][it.key()] = it.value().get<int>();
            return c;
        });
    }

    nlohmann::json TextOptsToJson(const InfoBarConfig& bar)
    {
        nlohmann::json topts = nlohmann::json::object();
        for (const auto& kv : bar.textOpts)
            if (!kv.second.empty())
                topts[kv.first] = kv.second;
        return topts;
    }

    nlohmann::json BarToJson(const InfoBarConfig& bar)
    {
        nlohmann::json j = nlohmann::json::object();
        j["enabled"] = bar.enabled;
        j["edge"] = bar.edge;
        j["widthPct"] = bar.widthPct;
        j["offsetX"] = bar.offsetX;
        j["offsetY"] = bar.offsetY;
        j["opacity"] = bar.opacity;
        j["textSize"] = bar.textSize;
        j["barHeight"] = bar.barHeight;
        j["hideInCombat"] = bar.hideInCombat;
        j["hideOnMap"] = bar.hideOnMap;
        j["showTextShadow"] = bar.showTextShadow;
        j["texts"] = UiLayout::OrderedToJson(bar.texts, [](const InfoSlot& s, nlohmann::json& e) { e["zone"] = s.zone; });
        j["textOpts"] = TextOptsToJson(bar);
        return j;
    }

    void BarFromJson(InfoBarConfig& bar, const nlohmann::json& j, bool includeDefaultOn)
    {
        if (!j.is_object()) return;
        bar.enabled = Api::Json::Bool(j, "enabled", bar.enabled);
        bar.edge = Api::Json::Int(j, "edge", bar.edge);
        bar.widthPct = Api::Json::Int(j, "widthPct", bar.widthPct);
        bar.offsetX = static_cast<float>(Api::Json::Num(j, "offsetX", bar.offsetX));
        bar.offsetY = static_cast<float>(Api::Json::Num(j, "offsetY", bar.offsetY));
        bar.opacity = Api::Json::Int(j, "opacity", bar.opacity);
        bar.textSize = static_cast<float>(Api::Json::Num(j, "textSize", bar.textSize));
        bar.barHeight = Api::Json::Int(j, "barHeight", bar.barHeight);
        bar.hideInCombat = Api::Json::Bool(j, "hideInCombat", bar.hideInCombat);
        bar.hideOnMap = Api::Json::Bool(j, "hideOnMap", bar.hideOnMap);
        bar.showTextShadow = Api::Json::Bool(j, "showTextShadow", bar.showTextShadow);
        LoadTextOpts(bar, Api::Json::Node(j, "textOpts"));
        if (j.contains("texts"))
            LoadTexts(bar, j["texts"]);
        ReconcileBar(bar, includeDefaultOn);
    }

    void LegacyBarFromJson(InfoBarConfig& bar, const nlohmann::json& j)
    {
        bar = {};
        bar.enabled = true;
        bar.edge = Api::Json::Int(j, "infoEdge", 1);
        bar.widthPct = Api::Json::Int(j, "infoWidthPct", 100);
        bar.offsetX = static_cast<float>(Api::Json::Num(j, "infoOffsetX", 0.0));
        bar.offsetY = static_cast<float>(Api::Json::Num(j, "infoOffsetY", 0.0));
        bar.opacity = Api::Json::Int(j, "infoOpacity", 60);
        bar.textSize = static_cast<float>(Api::Json::Num(j, "infoTextSize", 24.0));
        bar.barHeight = Api::Json::Int(j, "infoBarHeight", 0);
        bar.hideInCombat = Api::Json::Bool(j, "infoHideInCombat", true);
        bar.hideOnMap = Api::Json::Bool(j, "infoHideOnMap", false);
        LoadTextOpts(bar, Api::Json::Node(j, "infoTextOpts"));
        if (j.contains("infoTexts"))
            LoadTexts(bar, j["infoTexts"]);
        else
            bar.texts = DefaultTexts();
        ReconcileBar(bar, true);
    }

    struct Seg
    {
        std::string key;
        InfoData::HudSeg s;
        float w = 0.f;
        std::map<std::string, int> opts;
    };

    struct RenderCache
    {
        std::array<std::vector<Seg>, 3> zones;
        bool any = false;
        double lastT = -1e9;
        std::string character;
        float fsPx = -1.f;
        float panelW = -1.f;
        float barH = -1.f;
    };

    std::array<RenderCache, kBarCount> g_cache;

    void InvalidateBarCache(int index)
    {
        g_cache[ClampBarIndex(index)].lastT = -1e9;
    }

    void DrawTextOptionsPage(App& app, int barIndex, const char* key)
    {
        InfoBarConfig& bar = Bar(app, barIndex);
        const InfoData::HudText* def = InfoData::FindText(key);
        if (!def) return;

        ImGui::PushID(barIndex);
        Gw2Ui::Label(def->title, IM_COL32(255, 244, 207, 255), false, nullptr, 18.f, 1.2f);
        char explain[192];
        std::snprintf(explain, sizeof(explain), "Settings for this data text on %s. Zone, order, and show/hide are also on the Layout & texts page.",
                      kBarNames[barIndex]);
        SettingsParagraph(explain, IM_COL32(168, 158, 136, 255));
        ImGui::Spacing();

        Gw2Ui::Label("Placement", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-textopt-place");
        bool shown = bar.texts.Shown(key);
        char showLabel[96];
        std::snprintf(showLabel, sizeof(showLabel), "Show this text on %s", kBarNames[barIndex]);
        if (Gw2Ui::Checkbox(showLabel, &shown))
        {
            if (shown)
            {
                std::string k = key;
                bar.texts.Enable(k, [k] { return MakeText(k); });
            }
            else
                bar.texts.Remove(std::string(key));
            app.settingsDirty = true;
            InvalidateBarCache(barIndex);
        }
        const int si = shown ? FindSlotIdx(bar, key) : -1;
        if (si >= 0)
        {
            Gw2Ui::Label("Zone", Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
            if (Gw2Ui::Dropdown("##zsel", kZones, 3, &bar.texts.items[si].zone, 220.f))
            {
                app.settingsDirty = true;
                InvalidateBarCache(barIndex);
            }
        }
        else
            Gw2Ui::Label("Not on this bar. Options below are still saved for this bar.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        Gw2Ui::EndCard();
        ImGui::Dummy(ImVec2(0.f, 6.f));

        const std::vector<InfoData::HudOption>& opts = InfoData::OptionsFor(key);
        if (opts.empty())
        {
            Gw2Ui::Label("This text has no extra options.", Gw2Ui::kTextDim, false, nullptr, 14.f);
            ImGui::PopID();
            return;
        }
        std::map<std::string, int>& m = bar.textOpts[key];

        Gw2Ui::Label("Options", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-textopt-options");
        for (const InfoData::HudOption& op : opts)
        {
            const auto it = m.find(op.key);
            if (op.kind == InfoData::HudOption::Kind::Bool)
            {
                bool v = (it != m.end()) ? it->second != 0 : op.def != 0;
                if (Gw2Ui::Checkbox(op.label, &v))
                {
                    m[op.key] = v ? 1 : 0;
                    app.settingsDirty = true;
                    InvalidateBarCache(barIndex);
                }
            }
            else if (op.kind == InfoData::HudOption::Kind::Int)
            {
                int v = (it != m.end()) ? it->second : op.def;
                Gw2Ui::Label(op.label, Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
                ImGui::PushID(op.key);
                if (Gw2Ui::SliderInt("##o", &v, op.imin, op.imax, 150.f))
                {
                    m[op.key] = v;
                    app.settingsDirty = true;
                    InvalidateBarCache(barIndex);
                }
                ImGui::PopID();
            }
            else
            {
                int v = (it != m.end()) ? it->second : op.def;
                Gw2Ui::Label(op.label, Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
                ImGui::PushID(op.key);
                if (Gw2Ui::Dropdown("##o", op.enames, op.ecount, &v, 220.f))
                {
                    m[op.key] = v;
                    app.settingsDirty = true;
                    InvalidateBarCache(barIndex);
                }
                ImGui::PopID();
            }
        }
        Gw2Ui::EndCard();
        ImGui::PopID();
    }

    bool AnyEnabledText(const InfoBarConfig& bar)
    {
        for (const InfoSlot& slot : bar.texts.items)
            if (InfoData::FindText(slot.key.c_str()))
                return true;
        return false;
    }

    void OpenInfoSettings(App& app, int barIndex, const char* page = nullptr)
    {
        InfoPanel::SetSettingsBar(barIndex);
        OpenSettingsTab(app, SettingsTabOptions);
        OpenOptionsSection(SEC_INFO, page);
    }

    void RecomputeCache(App& app, int barIndex, InfoBarConfig& bar, RenderCache& cache, float fs, float labelFs,
                        float fsPx, float panelW, float barH, float ui)
    {
        cache.lastT = ImGui::GetTime();
        cache.character = app.state.currentChar;
        cache.fsPx = fsPx;
        cache.panelW = panelW;
        cache.barH = barH;
        for (auto& z : cache.zones) z.clear();
        cache.any = false;

        for (const InfoSlot& slot : bar.texts.items)
        {
            if (slot.zone < 0 || slot.zone > 2) continue;
            const InfoData::HudText* def = InfoData::FindText(slot.key.c_str());
            if (!def) continue;
            const std::map<std::string, int>* mp = OptsPtr(bar, slot.key);
            InfoData::HudSeg seg = def->compute(app, InfoData::HudOpts{ mp });
            if (seg.value.empty() && !seg.paint) continue;

            Seg e;
            e.key = slot.key;
            e.s = std::move(seg);
            if (mp) e.opts = *mp;
            const float lw = e.s.label.empty() ? 0.f : Gw2Ui::MeasureWidth(e.s.label.c_str(), labelFs) + 5.f * ui;
            if (e.s.paint)
                e.w = lw + (e.s.paintSize > 0.f ? e.s.paintSize * ui : (barH - 6.f * ui));
            else
                e.w = lw + (e.s.locked ? fsPx + 4.f * ui : 0.f) + (e.s.icon ? fsPx + 6.f * ui : 0.f) +
                      Gw2Ui::MeasureWidth(e.s.value.c_str(), fs);
            cache.zones[slot.zone].push_back(std::move(e));
            cache.any = true;
        }
        (void)barIndex;
    }

    void RenderBar(App& app, int barIndex, InfoBarConfig& bar)
    {
        if (!bar.enabled) return;
        if (bar.hideInCombat && IsInCombat()) return;
        if (bar.hideOnMap && IsMapOpen()) return;

        const ImGuiIO& io = ImGui::GetIO();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;
        if (W < 4.f || H < 4.f) return;

        const float ui = Gw2Ui::GlobalScale();
        const float fs = std::clamp(bar.textSize, 12.f, 32.f);
        const float labelFs = std::max(10.f, fs - 4.f);
        const float fsPx = fs * ui;
        const float fixedBarH = FixedBarHeightLogical(bar) * ui;
        const float autoBarH = fsPx + 14.f * ui;
        const float minBarH = fsPx;
        const float barH = (fixedBarH > 0.f) ? std::max(fixedBarH, minBarH) : autoBarH;
        const float margin = 12.f * ui;
        const float gap = 16.f * ui;
        const int widthPct = std::clamp(bar.widthPct, 25, 100);
        const float minPanelW = std::min(W, 240.f * ui);
        const float panelW = std::clamp(W * static_cast<float>(widthPct) / 100.f, minPanelW, W);
        const float maxPanelX = std::max(0.f, W - panelW);
        const float panelX = std::round(maxPanelX * 0.5f + std::clamp(bar.offsetX, -maxPanelX * 0.5f, maxPanelX * 0.5f));
        const float maxPanelY = std::max(0.f, H - barH);
        const float edgeOffsetY = std::clamp(bar.offsetY, 0.f, maxPanelY);
        const int edge = std::clamp(bar.edge, 0, 1);
        const float winY = (edge == 0) ? edgeOffsetY : (maxPanelY - edgeOffsetY);
        const ImVec2 panelMin(panelX, winY);
        const ImVec2 panelMax(panelX + panelW, winY + barH);

        RenderCache& cache = g_cache[barIndex];
        const double now = ImGui::GetTime();
        if ((now - cache.lastT) >= 0.2 || cache.character != app.state.currentChar || cache.fsPx != fsPx ||
            cache.panelW != panelW || cache.barH != barH)
            RecomputeCache(app, barIndex, bar, cache, fs, labelFs, fsPx, panelW, barH, ui);
        if (!cache.any) return;

        char winId[32];
        std::snprintf(winId, sizeof(winId), "##tcInfoPanel%d", barIndex);
        ImGui::SetNextWindowPos(panelMin);
        ImGui::SetNextWindowSize(ImVec2(panelW, barH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin(winId, nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (bar.opacity > 0)
            {
                const int a = std::clamp(static_cast<int>(bar.opacity * 2.35f), 0, 235);
                dl->AddRectFilled(panelMin, panelMax, IM_COL32(18, 15, 10, a));
                dl->AddLine(ImVec2(panelX, edge == 0 ? winY + barH : winY),
                            ImVec2(panelX + panelW, edge == 0 ? winY + barH : winY), IM_COL32(150, 124, 70, 150), ui);
            }
            dl->PushClipRect(panelMin, panelMax, true);

            auto drawSeg = [&](Seg& e, float x)
            {
                ImGui::SetCursorScreenPos(ImVec2(x, winY));
                ImGui::PushID(e.key.c_str());
                const bool lclick = ImGui::InvisibleButton("##s", ImVec2(e.w, barH));
                const bool hov = ImGui::IsItemHovered();
                const bool rclick = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                ImGui::PopID();
                const InfoData::HudText* def = InfoData::FindText(e.key.c_str());
                const bool needsKey = def && def->domains != 0u && !AccountData::HasKey();
                const bool clickable = def && (needsKey || def->onClick || def->actions || def->popup);
                if (hov)
                {
                    dl->AddRectFilled(ImVec2(x - 3.f * ui, winY + 1.f * ui),
                                      ImVec2(x + e.w + 3.f * ui, winY + barH - 1.f * ui), IM_COL32(255, 220, 140, 26), 3.f * ui);
                    if (clickable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                float cx = x;
                if (!e.s.label.empty())
                {
                    Gw2Ui::LabelDL(dl, ImVec2(cx, winY), ImVec2(cx + 1e4f, winY + barH), e.s.label.c_str(),
                                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(190, 165, 110, 255), bar.showTextShadow, nullptr, labelFs);
                    cx += Gw2Ui::MeasureWidth(e.s.label.c_str(), labelFs) + 6.f * ui;
                }
                if (e.s.paint)
                {
                    const float isz = e.s.paintSize > 0.f ? e.s.paintSize * ui : (barH - 6.f * ui);
                    e.s.paint(app, InfoData::HudOpts{ &e.opts }, dl, ImVec2(cx + isz * 0.5f, winY + barH * 0.5f), isz);
                }
                else
                {
                    if (e.s.locked)
                    {
                        Render::DrawLock(dl, ImVec2(cx, winY + (barH - fsPx) * 0.5f), fsPx, true);
                        cx += fsPx + 4.f * ui;
                    }
                    if (e.s.icon)
                    {
                        const float is = fsPx + 2.f * ui;
                        dl->AddImage((ImTextureID)e.s.icon, ImVec2(cx, winY + (barH - is) * 0.5f),
                                     ImVec2(cx + is, winY + (barH + is) * 0.5f));
                        cx += is + 4.f * ui;
                    }
                    Gw2Ui::LabelDL(dl, ImVec2(cx, winY), ImVec2(cx + 1e4f, winY + barH), e.s.value.c_str(),
                                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, e.s.color, bar.showTextShadow, nullptr, fs);
                }

                if (hov)
                {
                    if (needsKey)
                    {
                        if (Gw2Ui::TooltipBegin())
                        {
                            Gw2Ui::TooltipTitle(def->title);
                            Gw2Ui::TooltipMuted("Needs an API key. Click to add one in Options.");
                            Gw2Ui::TooltipEnd();
                        }
                    }
                    else if (def && def->tip)
                        def->tip(app, InfoData::HudOpts{ &e.opts });
                    else if (def)
                        Gw2Ui::Tooltip(def->title);
                }

                if (lclick && def)
                {
                    if (needsKey)
                    {
                        OpenSettingsTab(app, SettingsTabOptions);
                        OpenOptionsSection(SEC_API);
                    }
                    else if (def->popup)
                    {
                        g_popupReq = true;
                        g_popupBar = barIndex;
                        g_popupKey = e.key;
                    }
                    else if (def->onClick)
                        def->onClick(app, InfoData::HudOpts{ &e.opts });
                }
                if (rclick)
                {
                    g_menuBar = barIndex;
                    g_menuKey = e.key;
                    g_textMenuOpen = true;
                }
            };

            auto drawZone = [&](std::vector<Seg>& v, float startX)
            {
                float x = startX;
                for (size_t i = 0; i < v.size(); ++i)
                {
                    if (i > 0)
                        dl->AddLine(ImVec2(x - gap * 0.5f, winY + 5.f * ui),
                                    ImVec2(x - gap * 0.5f, winY + barH - 5.f * ui), IM_COL32(120, 108, 82, 120), ui);
                    drawSeg(v[i], x);
                    x += v[i].w + gap;
                }
            };
            auto groupW = [&](const std::vector<Seg>& v)
            {
                float w = 0.f;
                for (size_t i = 0; i < v.size(); ++i)
                    w += v[i].w + (i ? gap : 0.f);
                return w;
            };
            auto centerStartX = [&](const std::vector<Seg>& v) -> float
            {
                const int n = static_cast<int>(v.size());
                if (n == 0) return panelX + panelW * 0.5f;
                const int mid = n / 2;
                float anchor = 0.f;
                for (int i = 0; i < mid; ++i) anchor += v[i].w + gap;
                if (n % 2 == 1) anchor += v[mid].w * 0.5f;
                else anchor -= gap * 0.5f;
                return panelX + panelW * 0.5f - anchor;
            };

            if (!cache.zones[0].empty()) drawZone(cache.zones[0], panelX + margin);
            if (!cache.zones[1].empty()) drawZone(cache.zones[1], centerStartX(cache.zones[1]));
            if (!cache.zones[2].empty()) drawZone(cache.zones[2], panelX + panelW - margin - groupW(cache.zones[2]));
            dl->PopClipRect();

            if (g_menuReq == MenuKind::None && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                g_menuReq = MenuKind::Bar;
                g_menuBar = barIndex;
            }
            if (g_menuReq == MenuKind::Bar && g_menuBar == barIndex)
                g_barMenuOpen = true;
            if (g_menuBar == barIndex)
                g_menuReq = MenuKind::None;

            char popupId[32], textMenuId[32], barMenuId[32];
            std::snprintf(popupId, sizeof(popupId), "##ipDataPopup%d", barIndex);
            std::snprintf(textMenuId, sizeof(textMenuId), "##ipTextMenu%d", barIndex);
            std::snprintf(barMenuId, sizeof(barMenuId), "##ipBarMenu%d", barIndex);

            if (g_popupReq && g_popupBar == barIndex)
            {
                ImGui::OpenPopup(popupId);
                g_popupReq = false;
            }

            const InfoData::HudText* pdef = nullptr;
            const std::map<std::string, int>* popts = nullptr;
            if (g_popupBar == barIndex && !g_popupKey.empty())
            {
                pdef = InfoData::FindText(g_popupKey.c_str());
                popts = OptsPtr(bar, g_popupKey);
            }
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(20, 17, 11, 245));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(150, 124, 70, 220));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * ui, 10.f * ui));
            if (ImGui::BeginPopup(popupId))
            {
                if (pdef && pdef->popup) pdef->popup(app, InfoData::HudOpts{ popts });
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            const bool menuForThisBar = g_menuBar == barIndex && !g_menuKey.empty();
            const bool openTextMenu = g_textMenuOpen && menuForThisBar;
            {
                const int si = menuForThisBar ? FindSlotIdx(bar, g_menuKey) : -1;
                const InfoData::HudText* def = menuForThisBar ? InfoData::FindText(g_menuKey.c_str()) : nullptr;
                const std::map<std::string, int>* opts = (menuForThisBar && def) ? OptsPtr(bar, g_menuKey) : nullptr;
                const InfoData::HudOpts ho{ opts };
                constexpr int kBarBase = 900000;
                static std::vector<Gw2Ui::MenuNode> nodes;
                if (openTextMenu)
                {
                    nodes.clear();
                    bool special = false;
                    if (def && def->menuNodes)
                    {
                        def->menuNodes(app, ho, nodes);
                        special = !nodes.empty();
                    }
                    else if (def && def->actions)
                    {
                        std::vector<const char*> acts;
                        def->actions(app, ho, acts);
                        for (int i = 0; i < static_cast<int>(acts.size()); ++i)
                        {
                            Gw2Ui::MenuNode n;
                            n.label = acts[i];
                            n.id = i;
                            nodes.push_back(std::move(n));
                        }
                        special = !acts.empty();
                    }
                    if (special)
                    {
                        Gw2Ui::MenuNode sep;
                        sep.separator = true;
                        nodes.push_back(sep);
                    }

                    const int zone = (si >= 0) ? bar.texts.items[si].zone : -1;
                    Gw2Ui::MenuNode ctrl;
                    ctrl.label = "Bar Control";
                    ctrl.id = -1;
                    auto child = [&](const char* lbl, int id, bool sel = false)
                    {
                        Gw2Ui::MenuNode c;
                        c.label = lbl;
                        c.id = id;
                        c.selected = sel;
                        ctrl.children.push_back(std::move(c));
                    };
                    child("Disable this", kBarBase + 0);
                    child("Move to Left", kBarBase + 1, zone == 0);
                    child("Move to Center", kBarBase + 2, zone == 1);
                    child("Move to Right", kBarBase + 3, zone == 2);
                    {
                        Gw2Ui::MenuNode sep;
                        sep.separator = true;
                        ctrl.children.push_back(sep);
                    }
                    child(edge == 0 ? "Move panel to bottom" : "Move panel to top", kBarBase + 4);
                    child("Info Panel settings...", kBarBase + 5);
                    nodes.push_back(std::move(ctrl));
                }

                const int picked = Gw2Ui::ContextMenuTree(textMenuId, nodes, openTextMenu);
                if (openTextMenu) g_textMenuOpen = false;
                if (picked >= kBarBase)
                {
                    const int p = picked - kBarBase;
                    if (p == 0 && si >= 0)
                    {
                        bar.texts.Remove(bar.texts.items[si].key);
                        app.settingsDirty = true;
                        InvalidateBarCache(barIndex);
                    }
                    else if (p >= 1 && p <= 3 && si >= 0)
                    {
                        bar.texts.items[si].zone = p - 1;
                        app.settingsDirty = true;
                        InvalidateBarCache(barIndex);
                    }
                    else if (p == 4)
                    {
                        bar.edge = (edge == 0) ? 1 : 0;
                        app.settingsDirty = true;
                    }
                    else if (p == 5)
                    {
                        const char* page = InfoData::OptionsFor(g_menuKey.c_str()).empty() ? nullptr : g_menuKey.c_str();
                        OpenInfoSettings(app, barIndex, page);
                    }
                }
                else if (picked >= 0 && def)
                {
                    if (def->menuPick) def->menuPick(app, ho, picked);
                    else if (def->onAction) def->onAction(app, ho, picked);
                }
            }

            const bool openBarMenu = g_barMenuOpen && g_menuBar == barIndex;
            {
                constexpr int kPanelEdge = 800000, kPanelSettings = 800001;
                static std::vector<std::string> avail;
                static std::vector<Gw2Ui::MenuNode> nodes;
                if (openBarMenu)
                {
                    avail.clear();
                    for (const InfoData::HudText& t : InfoData::HudTexts())
                        if (!bar.texts.Shown(t.key))
                            avail.push_back(t.key);
                    std::sort(avail.begin(), avail.end(), [](const std::string& a, const std::string& b) {
                        const InfoData::HudText* da = InfoData::FindText(a.c_str());
                        const InfoData::HudText* db = InfoData::FindText(b.c_str());
                        return std::string(da ? da->title : "") < std::string(db ? db->title : "");
                    });

                    nodes.clear();
                    {
                        Gw2Ui::MenuNode add;
                        add.label = "Add data text";
                        add.id = -1;
                        if (avail.empty())
                        {
                            Gw2Ui::MenuNode z;
                            z.label = "(all data texts shown)";
                            z.id = -1;
                            add.children.push_back(z);
                        }
                        else
                        {
                            for (int i = 0; i < static_cast<int>(avail.size()); ++i)
                            {
                                const InfoData::HudText* d = InfoData::FindText(avail[i].c_str());
                                Gw2Ui::MenuNode t;
                                t.label = d ? d->title : avail[i].c_str();
                                t.id = -1;
                                for (int z = 0; z < 3; ++z)
                                {
                                    Gw2Ui::MenuNode c;
                                    c.label = kZones[z];
                                    c.id = i * 3 + z;
                                    t.children.push_back(std::move(c));
                                }
                                add.children.push_back(std::move(t));
                            }
                        }
                        nodes.push_back(std::move(add));
                    }
                    {
                        Gw2Ui::MenuNode sep;
                        sep.separator = true;
                        nodes.push_back(sep);
                    }
                    {
                        Gw2Ui::MenuNode n;
                        n.label = edge == 0 ? "Move panel to bottom" : "Move panel to top";
                        n.id = kPanelEdge;
                        nodes.push_back(n);
                    }
                    {
                        Gw2Ui::MenuNode n;
                        n.label = "Info Panel settings...";
                        n.id = kPanelSettings;
                        nodes.push_back(n);
                    }
                }

                const int picked = Gw2Ui::ContextMenuTree(barMenuId, nodes, openBarMenu);
                if (openBarMenu) g_barMenuOpen = false;
                if (picked == kPanelEdge)
                {
                    bar.edge = (edge == 0) ? 1 : 0;
                    app.settingsDirty = true;
                }
                else if (picked == kPanelSettings)
                    OpenInfoSettings(app, barIndex);
                else if (picked >= 0 && picked < static_cast<int>(avail.size()) * 3)
                {
                    const std::string key = avail[picked / 3];
                    if (InfoSlot* s = bar.texts.Enable(key, [key] { return MakeText(key); }))
                    {
                        s->zone = picked % 3;
                        app.settingsDirty = true;
                        InvalidateBarCache(barIndex);
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void DrawBarSelector()
    {
        Gw2Ui::Label("Editing bar", Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        ImGui::SameLine(160.f * Gw2Ui::GlobalScale());
        Gw2Ui::Dropdown("##infoBarSel", kBarNames, kBarCount, &g_settingsBar, 160.f);
        g_settingsBar = ClampBarIndex(g_settingsBar);
    }

    void DrawPlacementRows(App& app, int barIndex, InfoBarConfig& bar)
    {
        const float ui = Gw2Ui::GlobalScale();
        const float labelCol = 210.f * ui;
        auto place = [&]()
        {
            if (ImGui::GetContentRegionAvail().x > labelCol + 120.f * ui)
                ImGui::SameLine(labelCol);
            else
                ImGui::Dummy(ImVec2(0.f, 2.f * ui));
        };
        auto mark = [&]()
        {
            app.settingsDirty = true;
            InvalidateBarCache(barIndex);
        };

        if (Gw2Ui::Checkbox("Enable this bar", &bar.enabled)) mark();

        Gw2Ui::Label("Dock edge");
        place();
        if (Gw2Ui::Dropdown("##edge", kEdgeNames, 2, &bar.edge, 180.f)) mark();

        Gw2Ui::Label("Width (%)");
        place();
        if (Gw2Ui::SliderInt("##width", &bar.widthPct, 25, 100, 260.f)) mark();

        Gw2Ui::Label("Horizontal offset (px)");
        place();
        if (Gw2Ui::Slider("##offsetX", &bar.offsetX, -1000.f, 1000.f, "%.0f")) mark();

        Gw2Ui::Label("Edge offset (px)");
        place();
        if (Gw2Ui::Slider("##offsetY", &bar.offsetY, 0.f, 400.f, "%.0f")) mark();

        Gw2Ui::Label("Background opacity");
        place();
        if (Gw2Ui::SliderInt("##opacity", &bar.opacity, 0, 100, 260.f)) mark();

        Gw2Ui::Label("Text size");
        place();
        if (Gw2Ui::Slider("##textSize", &bar.textSize, 12.f, 32.f, "%.0f")) mark();

        Gw2Ui::Label("Bar height");
        place();
        if (Gw2Ui::Dropdown("##barHeight", kHeightNames, kHeightCount, &bar.barHeight, 180.f)) mark();

        if (Gw2Ui::Checkbox("Hide in combat", &bar.hideInCombat)) mark();
        if (Gw2Ui::Checkbox("Hide on full map", &bar.hideOnMap)) mark();
        if (Gw2Ui::Checkbox("Text shadow", &bar.showTextShadow)) mark();
    }
}

void InfoPanel::RegisterConfig()
{
    ConfigProfiles::RegisterLayout(ConfigProfiles::Owner::Info, {
        [](App& app, nlohmann::json& j) {
            nlohmann::json bars = nlohmann::json::array();
            for (const InfoBarConfig& bar : app.config.infoBars)
                bars.push_back(BarToJson(bar));
            j["infoBars"] = std::move(bars);
        },
        [](App& app, const nlohmann::json& j) {
            if (j.contains("infoEnabled") && j["infoEnabled"].is_boolean())
                app.config.infoEnabled = j["infoEnabled"].get<bool>();

            app.config.infoBars = {};
            const nlohmann::json& bars = Api::Json::Node(j, "infoBars");
            if (bars.is_array())
            {
                const int count = std::min(kBarCount, static_cast<int>(bars.size()));
                for (int i = 0; i < count; ++i)
                    BarFromJson(app.config.infoBars[i], bars[i], i == 0);
                for (int i = count; i < kBarCount; ++i)
                    ReconcileBar(app.config.infoBars[i], false);
            }
            else
            {
                LegacyBarFromJson(app.config.infoBars[0], j);
                for (int i = 1; i < kBarCount; ++i)
                    ReconcileBar(app.config.infoBars[i], false);
            }
            for (int i = 0; i < kBarCount; ++i)
                InvalidateBarCache(i);
        },
        [](App& app) { SeedDefaultBars(app.config); }
    });
}

unsigned InfoPanel::NeededDomains(App& app)
{
    if (!app.config.infoEnabled) return 0u;
    unsigned mask = 0u;
    for (InfoBarConfig& bar : app.config.infoBars)
    {
        if (!bar.enabled) continue;
        for (const InfoSlot& s : bar.texts.items)
        {
            const InfoData::HudText* def = InfoData::FindText(s.key.c_str());
            if (def) mask |= def->domains;
        }
    }
    return mask;
}

bool InfoPanel::HasWvwText(App& app)
{
    if (!app.config.infoEnabled) return false;
    for (const InfoBarConfig& bar : app.config.infoBars)
    {
        if (!bar.enabled) continue;
        for (const InfoSlot& s : bar.texts.items)
            if (s.key == "wvwscore" || s.key == "wvwskirmish" || s.key == "wvwppt" || s.key == "wvwkd" || s.key == "wvwcontrol")
                return true;
    }
    return false;
}

bool InfoPanel::IsEnabled(App& app)
{
    return app.config.infoEnabled;
}

void InfoPanel::SetEnabled(App& app, bool on)
{
    if (app.config.infoEnabled != on)
    {
        app.config.infoEnabled = on;
        app.settingsDirty = true;
    }
}

void InfoPanel::SetSettingsBar(int index)
{
    g_settingsBar = ClampBarIndex(index);
}

void InfoPanel::Render(App& app)
{
    if (!app.config.infoEnabled) return;
    for (int i = 0; i < kBarCount; ++i)
        RenderBar(app, i, app.config.infoBars[i]);
}

void InfoPanel::DrawSettings(App& app, const char* page)
{
    Config& cfg = app.config;
    g_settingsBar = ClampBarIndex(g_settingsBar);

    Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::Info), "infoprof",
        "The Info Panel can show up to five independent data-text bars. The master toggle is shared by the profile; "
        "each bar has its own placement, text size, and data-text layout. Existing profiles migrate into Bar 1.");

    Gw2Ui::Label("Bar selector", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("info-bar-selector");
    if (Gw2Ui::Checkbox("Enable Info Panel", &cfg.infoEnabled))
        app.settingsDirty = true;
    DrawBarSelector();
    SettingsParagraph("Bars 2-5 start disabled and empty. Select a bar here before editing placement or data-text options.",
                      IM_COL32(168, 158, 136, 255));
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    g_settingsBar = ClampBarIndex(g_settingsBar);

    if (page && *page)
    {
        DrawTextOptionsPage(app, g_settingsBar, page);
        return;
    }

    InfoBarConfig& bar = cfg.infoBars[g_settingsBar];
    ImGui::PushID(g_settingsBar);

    Gw2Ui::Label("Placement & display", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("info-placement");
    DrawPlacementRows(app, g_settingsBar, bar);
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));

    Gw2Ui::Label("Data texts", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    int moveA = -1, moveB = -1;
    int zoneIdx = -1, zoneTo = -1;
    std::string hideKey, addKey;
    Gw2Ui::BeginCard("info-datatexts");
    SettingsParagraph("Texts are grouped into the Left / Center / Right regions for the selected bar. Reorder within a "
                      "region with the arrows; click L/C/R to send a text to another region; the minus disables it.",
                      IM_COL32(168, 158, 136, 255));
    ImGui::Spacing();

    const float availW = Gw2Ui::CardInnerWidth();
    const float startX = ImGui::GetCursorScreenPos().x;
    const float rh = 28.f;

    for (int z = 0; z < 3; ++z)
    {
        std::vector<int> rows;
        for (int i = 0; i < static_cast<int>(bar.texts.items.size()); ++i)
            if (bar.texts.items[i].zone == z && InfoData::FindText(bar.texts.items[i].key.c_str()))
                rows.push_back(i);

        ImGui::Spacing();
        Gw2Ui::Label(kZones[z], Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        if (rows.empty())
        {
            Gw2Ui::Label("   (empty)", Gw2Ui::kTextDim, false, nullptr, 14.f);
            continue;
        }

        for (size_t r = 0; r < rows.size(); ++r)
        {
            const int i = rows[r];
            const InfoSlot& s = bar.texts.items[i];
            const InfoData::HudText* def = InfoData::FindText(s.key.c_str());
            Gw2Ui::ReorderRowDesc rd;
            rd.label = def->title;
            rd.note = (def->domains != 0u) ? "(needs API key)" : nullptr;
            rd.group = s.zone;
            rd.letters = kZoneLetters;
            rd.groupCount = 3;
            rd.canUp = (r > 0);
            rd.canDown = (r + 1 < rows.size());
            rd.fontSize = SettingsText::Hint;
            char rid[24];
            std::snprintf(rid, sizeof(rid), "it%d", i);
            const Gw2Ui::ReorderResult rr = Gw2Ui::ReorderRow(rid, startX, availW, rh, rd);
            if (rr.act == Gw2Ui::ReorderResult::Disable) hideKey = s.key;
            else if (rr.act == Gw2Ui::ReorderResult::Down) { moveA = i; moveB = rows[r + 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::Up) { moveA = i; moveB = rows[r - 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::SetGroup) { zoneIdx = i; zoneTo = rr.toGroup; }
        }
    }
    ImGui::Dummy(ImVec2(availW, 2.f));
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));

    if (moveA >= 0 && moveB >= 0)
    {
        std::swap(bar.texts.items[moveA], bar.texts.items[moveB]);
        app.settingsDirty = true;
        InvalidateBarCache(g_settingsBar);
    }
    else if (zoneIdx >= 0)
    {
        auto& items = bar.texts.items;
        InfoSlot moved = items[zoneIdx];
        moved.zone = zoneTo;
        items.erase(items.begin() + zoneIdx);
        int insertAt = static_cast<int>(items.size());
        for (int j = static_cast<int>(items.size()) - 1; j >= 0; --j)
            if (items[j].zone == zoneTo) { insertAt = j + 1; break; }
        items.insert(items.begin() + insertAt, moved);
        app.settingsDirty = true;
        InvalidateBarCache(g_settingsBar);
    }
    if (!hideKey.empty())
    {
        bar.texts.Remove(hideKey);
        app.settingsDirty = true;
        InvalidateBarCache(g_settingsBar);
    }

    bool anyHidden = false;
    for (const InfoData::HudText& t : InfoData::HudTexts())
        if (!bar.texts.Shown(t.key))
        {
            anyHidden = true;
            break;
        }
    if (anyHidden)
    {
        Gw2Ui::Label("Disabled datatexts", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("info-disabled");
        const float dAvail = Gw2Ui::CardInnerWidth();
        const float dStartX = ImGui::GetCursorScreenPos().x;
        SettingsParagraph("Grouped by kind, alphabetical within each. Click + to enable one on the selected bar.",
                          IM_COL32(168, 158, 136, 255));
        ImGui::Spacing();

        const InfoData::HudFam fams[] = {
            InfoData::HudFam::Location, InfoData::HudFam::Character, InfoData::HudFam::Economy, InfoData::HudFam::System
        };
        for (InfoData::HudFam fam : fams)
        {
            std::vector<std::string> group;
            for (const InfoData::HudText& t : InfoData::HudTexts())
                if (!bar.texts.Shown(t.key) && t.family == fam)
                    group.push_back(t.key);
            if (group.empty()) continue;
            std::sort(group.begin(), group.end(), [](const std::string& a, const std::string& b) {
                const InfoData::HudText* da = InfoData::FindText(a.c_str());
                const InfoData::HudText* db = InfoData::FindText(b.c_str());
                return std::string(da ? da->title : "") < std::string(db ? db->title : "");
            });
            ImGui::Spacing();
            Gw2Ui::Label(InfoData::FamilyName(fam), Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
            for (const std::string& k : group)
            {
                const InfoData::HudText* def = InfoData::FindText(k.c_str());
                char rid[48];
                std::snprintf(rid, sizeof(rid), "id_%s", k.c_str());
                if (Gw2Ui::EnableRow(rid, dStartX, dAvail, 26.f, def ? def->title : k.c_str(), -1, SettingsText::Hint))
                    addKey = k;
            }
        }
        ImGui::Dummy(ImVec2(dAvail, 2.f));
        Gw2Ui::EndCard();
    }

    if (!addKey.empty())
    {
        bar.texts.Enable(addKey, [addKey] { return MakeText(addKey); });
        app.settingsDirty = true;
        InvalidateBarCache(g_settingsBar);
    }

    if (!AnyEnabledText(bar))
        Gw2Ui::Label("This bar is empty. Add a data text above or from the bar's right-click menu.", Gw2Ui::kTextDim, false, nullptr, 14.f);

    ImGui::PopID();
}
