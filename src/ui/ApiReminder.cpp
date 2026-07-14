#include "ui/ApiReminder.h"
#include "app/App.h"
#include "ui/SettingsWindow.h"
#include "ui/tabs/SettingsModel.h"   // SEC_API
#include "ui/UiStrings.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <set>
#include <string>

namespace
{
    // Surfaces the user clicked "Not now" on, this session only (resets on addon reload = "until next login").
    std::set<std::string> g_dismissed;
}

void ApiReminder::OpenApiSettings(App& app)
{
    OpenSettingsTab(app, SettingsTabOptions);
    OpenOptionsSection(SEC_API);
}

bool ApiReminder::Card(App& app, const char* surfaceId, const char* purpose, bool gated)
{
    if (!gated && surfaceId && g_dismissed.count(surfaceId)) return false;

    Gw2Ui::BeginAccentCard("apirem", 0.f, IM_COL32(150, 124, 70, 220), IM_COL32(0, 0, 0, 30),
                           IM_COL32(120, 100, 60, 120));
    Gw2Ui::Label("API key needed", IM_COL32(220, 210, 185, 255), false, nullptr, 18.f);

    // Wrapped body so it survives narrow cards.
    const float availW = ImGui::GetContentRegionAvail().x;
    const char* body = UiStr::NoKey(purpose);
    const float bh = Gw2Ui::MeasureWrappedHeight(body, 16.f, availW);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    Gw2Ui::LabelIn(p, ImVec2(p.x + availW, p.y + bh), body, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                   Gw2Ui::kTextSub, false, nullptr, 16.f, availW);
    ImGui::Dummy(ImVec2(availW, bh));
    Gw2Ui::Label("Map names + tiles work without one.", Gw2Ui::kTextDim, false, nullptr, 14.f);

    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (Gw2Ui::Button("Open API settings", 150.f)) ApiReminder::OpenApiSettings(app);
    if (!gated)
    {
        ImGui::SameLine();
        if (Gw2Ui::Button("Not now", 90.f) && surfaceId) g_dismissed.insert(surfaceId);
    }
    Gw2Ui::EndCard();
    return true;
}
