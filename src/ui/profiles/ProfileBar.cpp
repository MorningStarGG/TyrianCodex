#include "ProfileBar.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/tabs/SettingsCommon.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// The shared profile-management bar (selector + New/Rename/Duplicate/Delete + Import-from-character). Drawn
// into whatever settings section the owning feature places it. Popup ids are scoped by PushID(idPrefix) so
// multiple bars on one screen never collide; the shared name/delete/import scratch state is additionally
// guarded by the owning host pointer.
namespace
{
    enum class PromptMode { New, Rename, Duplicate };

    const void* g_promptOwner = nullptr;   // host that opened the name prompt
    PromptMode  g_promptMode  = PromptMode::New;
    int         g_promptIdx   = -1;
    char        g_promptName[64] = "";

    const void* g_delOwner = nullptr;
    int         g_delIdx   = -1;

    const void* g_impOwner = nullptr;      // remembered import selection (per host)
    int         g_impChar  = 0;
    int         g_impProf  = 0;

    std::string DisplayName(Profiles::IProfileHost& host, int i)
    {
        std::string name = host.NameAt(i);
        if (host.IsGlobalAt(i))
            name += "  [global]";
        return name;
    }
}

void Profiles::DrawProfileBar(App& app, IProfileHost& host, const char* idPrefix, const char* helpText)
{
    if (helpText && *helpText) { SettingsParagraph(helpText, IM_COL32(190, 178, 150, 255)); ImGui::Spacing(); }

    ImGui::PushID(idPrefix);
    const int n = host.Count();
    const float ui = std::max(0.01f, Gw2Ui::GlobalScale());
    const float bgap = 6.f * ui;

    // ---- Card 1: active profile selector + management (New / Rename / Duplicate / Delete) ----
    // The management popups are opened AND begun inside this card so their ids resolve under the same scope
    // (BeginCard does PushID), and button widths use the card's inner width.
    std::string ch = host.CurrentChar();
    if (ch.empty() || ch == "default") ch = "current character";
    char hdr[96]; std::snprintf(hdr, sizeof(hdr), "Profiles for %s", ch.c_str());
    Gw2Ui::Label(hdr, IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("profcard");
    const float availW = Gw2Ui::CardInnerWidth();   // full inner width (symmetric L/R padding, clears the card's right margin)

    // ---- selector ----
    std::vector<std::string> names; names.reserve(n);
    for (int i = 0; i < n; ++i) names.push_back(DisplayName(host, i));
    std::vector<const char*> ptrs; ptrs.reserve(n);
    for (const std::string& s : names) ptrs.push_back(s.c_str());
    int sel = host.Active();
    if (!ptrs.empty() && Gw2Ui::DropdownPx("##sel", ptrs.data(), (int)ptrs.size(), &sel, std::min(availW, 320.f * ui)))
    { host.SetActive(sel); app.settingsDirty = true; }

    const int activeIdx = host.Active();
    const bool canMakeGlobal = host.CanMakeGlobal(activeIdx);
    const bool canCopyLocal = host.CanCopyToCharacter(activeIdx);
    const char* scopeLabel = canMakeGlobal ? "Make global" : (canCopyLocal ? "Copy to this character" : nullptr);
    if (scopeLabel)
    {
        const float scopeW = std::min(availW, (canCopyLocal ? 220.f : 150.f) * ui);
        if (ImGui::GetContentRegionAvail().x >= scopeW + bgap)
            ImGui::SameLine(0.f, bgap);
        else
            ImGui::Dummy(ImVec2(0.f, 6.f * ui));
        if (Gw2Ui::ButtonPx(scopeLabel, scopeW, 26.f * ui))
        {
            if (canMakeGlobal) host.MakeGlobal(activeIdx);
            else              host.CopyToCharacter(activeIdx);
            app.settingsDirty = true;
        }
        if (ImGui::IsItemHovered())
            Gw2Ui::Tooltip(canMakeGlobal
                ? "Create a shared copy and switch to it. Changes to that shared profile apply to every character using it."
                : "Create a private character copy and switch to it. Future edits will not change the shared profile.");
    }
    ImGui::Dummy(ImVec2(0.f, 6.f * ui));

    // ---- New / Rename / Duplicate / Delete ----
    const std::string activeName = (host.Active() >= 0 && host.Active() < n) ? host.NameAt(host.Active()) : "Default";
    const bool  canDelete = n > 1;
    const int   nbtn = canDelete ? 4 : 3;
    const float bw   = Gw2Ui::FillWidth(availW, nbtn, bgap, 48.f * ui);

    auto openPrompt = [&](PromptMode m, int idx, const std::string& seed) {
        g_promptOwner = &host; g_promptMode = m; g_promptIdx = idx;
        std::snprintf(g_promptName, sizeof(g_promptName), "%s", seed.c_str());
        ImGui::OpenPopup("##profPrompt");
    };
    if (Gw2Ui::ButtonPx("New", bw, 26.f * ui)) openPrompt(PromptMode::New, -1, host.Suggest("Profile"));
    ImGui::SameLine(0.f, bgap);
    if (Gw2Ui::ButtonPx("Rename", bw, 26.f * ui)) openPrompt(PromptMode::Rename, host.Active(), activeName);
    ImGui::SameLine(0.f, bgap);
    if (Gw2Ui::ButtonPx("Duplicate", bw, 26.f * ui)) openPrompt(PromptMode::Duplicate, host.Active(), host.Suggest(activeName + " copy"));
    if (canDelete)
    {
        ImGui::SameLine(0.f, bgap);
        if (Gw2Ui::ButtonPx("Delete", bw, 26.f * ui)) { g_delOwner = &host; g_delIdx = host.Active(); ImGui::OpenPopup("##profDelete"); }
    }

    // name prompt
    if (ImGui::BeginPopup("##profPrompt"))
    {
        if (g_promptOwner == &host)
        {
            const char* title = g_promptMode == PromptMode::Rename ? "Rename profile"
                              : g_promptMode == PromptMode::Duplicate ? "Duplicate profile" : "Name new profile";
            const char* okLabel = g_promptMode == PromptMode::Rename ? "Rename"
                                : g_promptMode == PromptMode::Duplicate ? "Duplicate" : "Create";
            Gw2Ui::Label(title, IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.2f);
            ImGui::Spacing();
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            Gw2Ui::TextBox("##pn", g_promptName, sizeof(g_promptName), 240.f);
            const bool enter = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) ||
                               ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeyPadEnter), false);
            ImGui::Spacing();
            bool commit = enter;
            if (Gw2Ui::Button(okLabel, 110.f, 26.f)) commit = true;
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::Button("Cancel", 90.f, 26.f)) ImGui::CloseCurrentPopup();
            if (commit && g_promptName[0] != '\0')
            {
                if      (g_promptMode == PromptMode::Rename)    host.Rename(g_promptIdx, g_promptName);
                else if (g_promptMode == PromptMode::Duplicate) host.Duplicate(g_promptIdx, g_promptName);
                else                                            host.New(g_promptName);
                app.settingsDirty = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // delete confirm
    if (ImGui::BeginPopup("##profDelete"))
    {
        if (g_delOwner == &host)
        {
            const std::string nm = (g_delIdx >= 0 && g_delIdx < host.Count()) ? host.NameAt(g_delIdx) : "this profile";
            char msg[200]; std::snprintf(msg, sizeof(msg), "Delete the profile \"%s\"? This cannot be undone.", nm.c_str());
            const float tw = 300.f, th = Gw2Ui::MeasureWrappedHeight(msg, SettingsText::Body, tw);
            const ImVec2 mp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(tw, th));
            Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), mp, ImVec2(mp.x + tw, mp.y + th), msg,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(236, 230, 212, 255), false, nullptr, SettingsText::Body, tw);
            ImGui::Spacing();
            if (Gw2Ui::Button("Delete", 100.f, 26.f)) { host.Delete(g_delIdx); g_delIdx = -1; app.settingsDirty = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::Button("Cancel", 90.f, 26.f)) { g_delIdx = -1; ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }
    Gw2Ui::EndCard();   // close the selector / management card

    // ---- Card 2: Import a profile from another character ----
    std::vector<std::string> chars = host.CharsWithProfiles();
    if (!chars.empty())
    {
        ImGui::Dummy(ImVec2(0.f, 6.f * ui));
        Gw2Ui::Label("Import from character", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("impcard");
        const float impW = Gw2Ui::CardInnerWidth();   // full inner width (symmetric L/R padding)
        SettingsParagraph("Copy a saved profile from one of your other characters into this one.", IM_COL32(168, 158, 136, 255));
        ImGui::Spacing();

        if (g_impOwner != &host) { g_impOwner = &host; g_impChar = 0; g_impProf = 0; }
        if (g_impChar < 0 || g_impChar >= (int)chars.size()) g_impChar = 0;
        std::vector<const char*> cptr; cptr.reserve(chars.size());
        for (const std::string& s : chars) cptr.push_back(s.c_str());

        Gw2Ui::Label("Character", Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        Gw2Ui::DropdownPx("##impchar", cptr.data(), (int)cptr.size(), &g_impChar, std::min(impW, 320.f * ui));

        std::vector<std::string> pnames = host.ProfileNamesOf(chars[g_impChar]);
        if (g_impProf < 0 || g_impProf >= (int)pnames.size()) g_impProf = 0;
        std::vector<const char*> pptr; pptr.reserve(pnames.size());
        for (const std::string& s : pnames) pptr.push_back(s.c_str());

        ImGui::Spacing();
        Gw2Ui::Label("Profile", Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        if (!pptr.empty()) Gw2Ui::DropdownPx("##impprof", pptr.data(), (int)pptr.size(), &g_impProf, std::min(impW, 320.f * ui));

        ImGui::Spacing();
        const float ibg = 8.f * ui;
        const float ibw = std::max(60.f * ui, (impW - ibg) / 2.f);
        if (!pptr.empty() && Gw2Ui::ButtonPx("Import this profile", ibw, 26.f * ui)) { host.CopyFrom(chars[g_impChar], g_impProf); app.settingsDirty = true; }
        ImGui::SameLine(0.f, ibg);
        if (Gw2Ui::ButtonPx("Import all", ibw, 26.f * ui)) { host.CopyAllFrom(chars[g_impChar]); app.settingsDirty = true; }
        Gw2Ui::EndCard();
    }

    ImGui::PopID();
}
