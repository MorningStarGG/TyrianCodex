#include "ui/tabs/AboutTab.h"
#include "render/glyphs/Glyphs.h"
#include "ui/GuideViewer.h"
#include "ui/Gw2Ui.h"
#include "ui/tabs/SettingsCommon.h"
#include "Version.h"
#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <cstddef>
#include <cstdio>
#include <string>

namespace
{
    struct Credit
    {
        const char *name;
        const char *detail;
        const char *donationUrl = nullptr;
    };

    struct CreditGroup
    {
        const char *title;
        ImU32 accent;
        const Credit *items;
        int count;
    };

    template <std::size_t N>
    constexpr int CountOf(const Credit (&)[N])
    {
        return (int)N;
    }

    const Credit kPlatformCredits[] = {
        {"ArenaNet",
         "For creating Guild Wars 2, and for the official API, map tiles, game icons, interface fonts, wiki/API reference data, and other game data Tyrian Codex builds on."},
        {"Nexus / RaidcoreGG",
         "For the addon framework that lets Tyrian Codex run inside Guild Wars 2.",
         "https://www.patreon.com/raidcore"},
        {"Blish HUD",
         "For UI design ideas, addon interaction patterns, and implementation methods used as reference.",
         "https://ko-fi.com/freesnow"},
    };

    const Credit kTrailCredits[] = {
        {"Lady Elyssa",
         "For route trails, markers, trail art, heart notes, and gathering data used by parts of Tyrian Codex."},
        {"Tekkit",
         "For route trails and marker-pack data, including source material used by open-world and farming routes.",
         "https://www.tekkitsworkshop.net/donate"},
        {"QuitarHero",
         "For route and marker-pack data used by some trails and gathering paths."},
        {"Metallis",
         "For farming and fishing data, including fishing-hole locations and trail-style source material from Metal-Marker-Myriad."},
    };

    const Credit kDataCredits[] = {
        {"Immortius",
         "For Wardrobe unlock data from the GW2 Wardrobe Unlock Analyser."},
        {"GW2StoryTimes.com",
         "For community story completion-time estimates shown in the story and journal surfaces."},
        {"GW2dat",
         "For Guild Wars 2 asset references used by some icons and visual assets.",
         "https://ko-fi.com/freesnow"},
        {"Guild Wars 2 Wiki contributors",
         "For wiki article content, objective descriptions, and reference material rendered in the in-addon wiki reader."},
    };

    const Credit kFontCredits[] = {
        {"ArenaNet",
         "For the Menomonia font used by Tyrian Codex's UI."},
        {"MSCHF",
         "For the Times Newer Roman font used by the wiki quote styling."},
        {"ProperDave and ArenaNet",
         "For the New Krytan font used by Tyrian Codex's Krytan-styled zone-display effects."},
    };

    const Credit kLibraryCredits[] = {
        {"litehtml and Gumbo",
         "For the native HTML and CSS parsing/rendering stack used by the in-addon wiki reader.",
         "http://www.litehtml.com/donate.html"},
        {"Dear ImGui",
         "For the immediate-mode UI layer used by Nexus addons and Tyrian Codex's custom GW2-styled controls.",
         "https://github.com/ocornut/imgui/wiki/Funding"},
        {"nlohmann/json",
         "For JSON parsing of settings, bundled datasets, and API-derived data.",
         "https://paypal.me/nlohmann"},
    };

    std::string s_donationName;
    std::string s_donationUrl;
    bool s_donationPromptRequested = false;

    void OpenBrowser(const std::string &url)
    {
        if (!url.empty())
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void QueueDonationPrompt(const Credit &credit)
    {
        if (!credit.donationUrl || !*credit.donationUrl)
            return;
        s_donationName = credit.name ? credit.name : "Support link";
        s_donationUrl = credit.donationUrl;
        SetClipboard(s_donationUrl);
        ViewerAlert("Copied donation link.");
        s_donationPromptRequested = true;
    }

    void WrappedModalText(const char *text, ImU32 color, float fontSize, float width)
    {
        if (!text || !*text)
            return;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = Gw2Ui::MeasureWrappedHeight(text, fontSize, width);
        ImGui::Dummy(ImVec2(width, h));
        Gw2Ui::LabelIn(p, ImVec2(p.x + width, p.y + h), text,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, color, false, nullptr, fontSize, width);
    }

    void DrawDonationPrompt()
    {
        if (s_donationPromptRequested)
        {
            ImGui::OpenPopup("Open donation link?");
            s_donationPromptRequested = false;
        }

        if (ImGui::BeginPopupModal("Open donation link?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            Render::DrawGlyph(ImGui::GetWindowDrawList(), ImVec2(hp.x + 9.f, hp.y + 11.f), 18.f,
                              Render::Glyph::ExternalLink, Gw2Ui::kTextSelected);
            ImGui::Indent(24.f);
            Gw2Ui::Label("External donation link", Gw2Ui::kTextSelected, true, nullptr, 18.f, 1.6f);
            ImGui::Unindent(24.f);

            WrappedModalText("The donation link has been copied to your clipboard. Do you also want to open it in your browser?",
                             Gw2Ui::kTextSub, 16.f, 520.f);
            ImGui::Dummy(ImVec2(1.f, 4.f));
            WrappedModalText(s_donationName.c_str(), Gw2Ui::kTextSelected, 16.f, 520.f);
            WrappedModalText(s_donationUrl.c_str(), IM_COL32(135, 190, 255, 255), 14.f, 520.f);
            ImGui::Dummy(ImVec2(1.f, 8.f));

            if (Gw2Ui::ActionButton("Open browser", 130.f, 28.f, Gw2Ui::ActionButtonVariant::Primary))
            {
                OpenBrowser(s_donationUrl);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Copy link", 110.f, 28.f))
            {
                SetClipboard(s_donationUrl);
                ViewerAlert("Copied donation link.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Cancel", 90.f, 28.f))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void DrawIntro()
    {
        Gw2Ui::BeginAccentCard("about_intro", 0.f, Gw2Ui::kGold);
        Gw2Ui::Label("Tyrian Codex", Gw2Ui::kGold, true, nullptr, 30.f, 1.5f);
        Gw2Ui::Label("Guild Wars 2 companion addon  -  v" TC_VERSION_STRING, Gw2Ui::kTextDim, false, nullptr, SettingsText::Hint);
        ImGui::Dummy(ImVec2(0.f, 8.f));
        SettingsParagraph(
            "Tyrian Codex exists because of Guild Wars 2, Nexus, Blish, community marker packs, public data projects, open-source libraries, and the players who maintain reference data for Tyria.",
            Gw2Ui::kTextSub, SettingsText::Body);
        Gw2Ui::EndCard();
        ImGui::Dummy(ImVec2(0.f, 10.f));
    }

    void DrawCreditRow(const Credit &credit, int index)
    {
        if (index > 0)
        {
            ImGui::Dummy(ImVec2(0.f, 5.f));
            Gw2Ui::Divider(Gw2Ui::CardInnerWidth(), IM_COL32(196, 176, 128, 45));
        }

        Gw2Ui::Label(credit.name, Gw2Ui::kTextSelected, true, nullptr, SettingsText::Body, 1.2f);
        ImGui::Dummy(ImVec2(0.f, 2.f));
        SettingsParagraph(credit.detail, Gw2Ui::kTextSub, SettingsText::Hint);

        if (credit.donationUrl && *credit.donationUrl)
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            ImGui::PushID(credit.name ? credit.name : "credit");
            if (Gw2Ui::ActionButton("Donate", 92.f, 26.f, Gw2Ui::ActionButtonVariant::Primary,
                                    "Copy the donation link and choose whether to open it in your browser."))
                QueueDonationPrompt(credit);
            ImGui::Dummy(ImVec2(0.f, 3.f));
            SettingsParagraph("Copies the link first, then asks before opening a browser.", Gw2Ui::kTextDim, 14.f);
            ImGui::PopID();
        }
    }

    void DrawCreditGroup(const CreditGroup &group, const char *id)
    {
        Gw2Ui::BeginAccentCard(id, 0.f, group.accent);
        Gw2Ui::SectionHeader(group.title, nullptr, SettingsText::Header, Gw2Ui::kGold, false);
        for (int i = 0; i < group.count; ++i)
            DrawCreditRow(group.items[i], i);
        Gw2Ui::EndCard();
        ImGui::Dummy(ImVec2(0.f, 10.f));
    }
}

void DrawAboutContent()
{
    DrawIntro();

    const CreditGroup groups[] = {
        {"Guild Wars 2, Platform, and Design", IM_COL32(255, 221, 130, 255), kPlatformCredits, CountOf(kPlatformCredits)},
        {"Trails, Markers, Farming, and Fishing", IM_COL32(150, 220, 150, 255), kTrailCredits, CountOf(kTrailCredits)},
        {"Data, Assets, and Reference", IM_COL32(120, 190, 240, 255), kDataCredits, CountOf(kDataCredits)},
        {"Fonts", IM_COL32(210, 170, 255, 255), kFontCredits, CountOf(kFontCredits)},
        {"Libraries", IM_COL32(235, 170, 105, 255), kLibraryCredits, CountOf(kLibraryCredits)},
    };

    for (int i = 0; i < (int)(sizeof(groups) / sizeof(groups[0])); ++i)
    {
        char id[32];
        std::snprintf(id, sizeof(id), "about_group_%d", i);
        DrawCreditGroup(groups[i], id);
    }

    Gw2Ui::BeginCard("about_thanks");
    SettingsParagraph(
        "Thank you to everyone whose tools, research, route work, data, libraries, and design ideas made Tyrian Codex possible.",
        Gw2Ui::kTextSub, SettingsText::Body);
    Gw2Ui::EndCard();

    DrawDonationPrompt();
}
