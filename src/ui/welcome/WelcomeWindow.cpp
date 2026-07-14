#include "ui/welcome/WelcomeWindow.h"
#include "app/App.h"                 // App, Api::Client / Api::TokenInfo
#include "ui/ApiReminder.h"          // OpenApiSettings (the "what each unlocks" link reuses the deep-link)
#include "ui/ApiScopes.h"            // the shared scope catalog (recommended-scopes line)
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace
{
    bool g_decided = false;   // the show/no-show choice is made once per session (first Draw call)
    bool g_show    = false;
    bool g_reveal  = false;   // TextBoxSecret eye toggle
    constexpr float kInnerW = 460.f;   // fixed content width -> a stable wrap width under AlwaysAutoResize

    // Wrapped on-brand paragraph at the fixed inner width.
    void Para(const char* text, ImU32 col, float px)
    {
        const float h = Gw2Ui::MeasureWrappedHeight(text, px, kInnerW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(p, ImVec2(p.x + kInnerW, p.y + h), text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       col, false, nullptr, px, kInnerW);
        ImGui::Dummy(ImVec2(kInnerW, h));
    }

    void Finish(App& app)
    {
        app.config.onboardedV1 = true;   // never auto-show the Welcome window again (toast/cards still remind)
        app.settingsDirty = true;        // entry.cpp's commit path applies any new key + persists settings.json
        g_show = false;
    }
}

void Welcome::Draw(App& app)
{
    if (!g_decided)
    {
        g_decided = true;
        g_show = (!app.config.onboardedV1 && !app.api.HasKey());
    }
    if (!g_show) return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 18, 14, 246));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(150, 124, 70, 205));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##tcwelcome", nullptr, flags))
    {
        Gw2Ui::Label("Welcome to Tyrian Codex", Gw2Ui::kGold, false, nullptr, 26.f, 1.6f);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Para("Map names and tile imagery already work with no key. Add a GW2 API key to unlock level-based zone "
             "recommendations, your personal Story, the Items browser, your wallet and more.",
             Gw2Ui::kTextSub, 17.f);

        ImGui::Dummy(ImVec2(0.f, 8.f));
        Gw2Ui::Label("API key", IM_COL32(220, 210, 185, 255), false, nullptr, 18.f);
        const float pasteW = 70.f, gap = 8.f;
        if (Gw2Ui::TextBoxSecret("##wkey", app.config.apiKey, sizeof(app.config.apiKey), kInnerW - pasteW - gap, &g_reveal))
            app.settingsDirty = true;   // applied on commit (field deselected) by entry.cpp's watcher
        ImGui::SameLine(0.f, gap);
        if (Gw2Ui::Button("Paste", pasteW))
        {
            if (const char* clip = ImGui::GetClipboardText())
            { std::snprintf(app.config.apiKey, sizeof(app.config.apiKey), "%s", clip); app.settingsDirty = true; }
        }

        // Live status (the entry.cpp watcher applies the key as it changes; this just reflects the result).
        const bool hasKey = app.api.HasKey();
        if (hasKey && app.api.ScopesResolving())
            Gw2Ui::Label("Checking key...", Gw2Ui::kGold, false, nullptr, 16.f);
        else if (hasKey)
        {
            const Api::TokenInfo ti = app.api.ScopeInfo();
            if (ti.valid)
            {
                char b[160]; std::snprintf(b, sizeof(b), "Key \"%s\" accepted.", ti.name.c_str());
                Gw2Ui::Label(b, IM_COL32(150, 210, 140, 255), false, nullptr, 16.f);
            }
            else
                Gw2Ui::Label("Key not recognized (or offline).", IM_COL32(228, 130, 110, 255), false, nullptr, 16.f);
        }
        else
            Gw2Ui::Label("Paste a key above, or skip for now.", Gw2Ui::kTextDim, false, nullptr, 16.f);

        ImGui::Dummy(ImVec2(0.f, 8.f));
        Gw2Ui::Divider(kInnerW);
        Para("Create one at account.arena.net -> Applications. It's stored locally in settings.json (Nexus has "
             "no key vault), so use a least-scope key. The guide only ever READS your account -- never changes it.",
             Gw2Ui::kTextSub, 15.f);

        // Recommended scopes, built from the shared catalog (detail lives on the full API tab).
        std::string scopes = "Recommended scopes: ";
        for (int i = 0; i < ApiScopes::kScopeCount; ++i)
        { if (i) scopes += ", "; scopes += ApiScopes::kScopes[i].name; }
        scopes += ".";
        Para(scopes.c_str(), Gw2Ui::kTextDim, 14.f);

        ImGui::Dummy(ImVec2(0.f, 10.f));
        if (Gw2Ui::Button("Save & close", 150.f)) Finish(app);
        ImGui::SameLine();
        if (Gw2Ui::Button("Skip for now", 120.f)) Finish(app);
        ImGui::SameLine();
        if (Gw2Ui::Button("What each unlocks", 170.f)) { ApiReminder::OpenApiSettings(app); Finish(app); }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
