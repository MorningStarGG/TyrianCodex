#pragma once
#include "app/AccountData.h"
#include "ui/Gw2Ui.h"
#include "ui/UiStrings.h"
#include "ui/SettingsWindow.h"      // OpenSettingsTab + OpenOptionsSection (the "Add API key" button target)
#include "ui/tabs/SettingsModel.h"  // SEC_API
#include "guide/CurrentChar.h"
#include "api/core/Permissions.h"
#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>

// Shared helpers for the API-backed dashboard widgets: the key/scope gate (so a widget shows a clear
// "add API key" note instead of a blank panel), the "as of / updating" freshness line, gold formatting,
// and the ONE reusable character picker (so alt widgets don't each duplicate selection logic).
namespace DashApi
{
    // A widget state/notice line that WRAPS to the current widget width, so long messages (the no-key / no-scope
    // notes, an "unavailable" state, ...) don't clip off the right edge of a half-width widget. Use this for any
    // multi-word widget message instead of a bare single-line Gw2Ui::Label.
    inline void Note(const char* text, ImU32 color = IM_COL32(180, 150, 110, 255), float fontSize = 14.f)
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = Gw2Ui::MeasureWrappedHeight(text, fontSize, w);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        Gw2Ui::LabelIn(p, ImVec2(p.x + w, p.y + h), text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, color, false, nullptr, fontSize, w);
    }

    // Open the Options tab on the API-key section (the "Add API key" button target).
    inline void OpenApiSettings(App& app) { OpenSettingsTab(app, SettingsTabOptions); OpenOptionsSection(SEC_API); }

    // Returns true if the widget may render (key present + scope granted). Otherwise draws a (wrapped) note plus a
    // button that jumps to the API-key field in Options, and returns false.
    inline bool Gate(App& app, Api::TokenPermission scope, const char* scopeLabel)
    {
        const bool noKey   = !AccountData::HasKey();
        const bool noScope = !noKey && !AccountData::HasScope(scope);
        if (!noKey && !noScope) return true;
        Note(noKey ? UiStr::NoKeyGeneric : UiStr::NeedScope(scopeLabel));
        ImGui::Spacing();
        if (Gw2Ui::ActionButtonPx(noKey ? "Add API key" : "Open API settings", ImGui::GetContentRegionAvail().x, Gw2Ui::Scaled(24.f),
                                  Gw2Ui::ActionButtonVariant::Primary, "Open the API key field in Options"))
            OpenApiSettings(app);
        return false;
    }

    // A subtle "as of HH:MM" / "updating..." footer. `at` is the domain's ImGui-time stamp (0 = never).
    inline void Freshness(double at)
    {
        if (AccountData::Refreshing() && at <= 0.0) { Gw2Ui::Label("updating...", IM_COL32(140, 132, 116, 255), false, nullptr, 14.f, 0.4f); return; }
        const char* tail = AccountData::Refreshing() ? "  (updating...)" : "";
        char b[48];
        if (at <= 0.0) std::snprintf(b, sizeof(b), "last known%s", tail);
        else           std::snprintf(b, sizeof(b), "updated %.0fs ago%s", ImGui::GetTime() - at, tail);
        Gw2Ui::Label(b, IM_COL32(130, 122, 106, 255), false, nullptr, 14.f, 0.4f);
    }

    // Copper -> "Ng Ms Kc" (gold/silver/copper), trimming leading zero parts.
    inline std::string Gold(long long copper)
    {
        const long long g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
        char b[48];
        if (g > 0)      std::snprintf(b, sizeof(b), "%lldg %llds %lldc", g, s, c);
        else if (s > 0) std::snprintf(b, sizeof(b), "%llds %lldc", s, c);
        else            std::snprintf(b, sizeof(b), "%lldc", c);
        return b;
    }

    // Reusable account-character dropdown. `sel` is caller-owned (a static); defaults to the active character.
    // Triggers EnsureCharDetail when the choice changes. Returns the chosen name (may be empty if none cached).
    inline std::string CharacterPicker(const char* id, std::string& sel, float width)
    {
        const AccountData::Model& m = AccountData::Get();
        if (m.characters.empty()) { Gw2Ui::Label("No characters cached yet.", Gw2Ui::kTextDim, false, nullptr, 14.f); return std::string(); }

        if (sel.empty()) sel = CurrentCharName();
        std::vector<const char*> names; int idx = 0;
        for (size_t i = 0; i < m.characters.size(); ++i) { names.push_back(m.characters[i].name.c_str()); if (m.characters[i].name == sel) idx = (int)i; }
        if (idx == 0 && (sel.empty() || sel != m.characters[0].name)) sel = m.characters[0].name;   // fall back to first cached

        if (Gw2Ui::DropdownPx(id, names.data(), (int)names.size(), &idx, width))
            sel = m.characters[idx].name;
        AccountData::EnsureCharDetail(sel);   // no-op once cached (guarded inside)
        return sel;
    }
}
