#include "Widgets.h"
#include "ui/dashboard/Notify.h"
#include "app/App.h"
#include "ui/ApiReminder.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
    std::string AgeText(double secs)
    {
        if (secs < 0) secs = 0;
        char b[32];
        if (secs < 60)        std::snprintf(b, sizeof(b), "%ds ago", (int)secs);
        else if (secs < 3600) std::snprintf(b, sizeof(b), "%dm ago", (int)(secs / 60));
        else if (secs < 86400)std::snprintf(b, sizeof(b), "%dh ago", (int)(secs / 3600));
        else                  std::snprintf(b, sizeof(b), "%dd ago", (int)(secs / 86400));
        return b;
    }
}

void DashW::Notifications(App& app, float w)
{
    const std::vector<Notify::Item>& hist = Notify::History();
    const double now = ImGui::GetTime();

    int liveCount = 0;
    for (const Notify::Item& it : hist) if (!it.dismissed) ++liveCount;

    // header row: count + Clear all
    {
        char hdr[48]; std::snprintf(hdr, sizeof(hdr), "%d recent", liveCount);
        Gw2Ui::Label(hdr, IM_COL32(180, 172, 150, 255), false, nullptr, 14.f);
        if (liveCount > 0)
        {
            ImGui::SameLine();
            const char* cl = "Clear all";
            const float cw = Gw2Ui::MeasureWidth(cl, 14.f) + 4.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::GetCursorPosX()) - cw - 2.f);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##clrall", ImVec2(cw, 20.f));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) Notify::ClearAll();
            if (hov) Gw2Ui::Tooltip("Clear all notifications");
            Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + cw, p.y + 20.f), cl,
                           Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                           hov ? Gw2Ui::kGold : IM_COL32(170, 150, 110, 255), false, nullptr, 14.f);
        }
    }
    Gw2Ui::Divider(w);

    if (liveCount == 0)
    {
        Gw2Ui::Label("No notifications yet.", Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    int shown = 0;
    for (int i = (int)hist.size() - 1; i >= 0; --i)   // newest first
    {
        const Notify::Item& it = hist[i];
        if (it.dismissed) continue;

        const float accentW = 3.f, padL = 10.f, padR = 22.f, padV = 5.f;
        const float textX = accentW + padL;
        const float textW = std::max(40.f, w - textX - padR);
        const float titleH = it.title.empty() ? 0.f : Gw2Ui::MeasureWrappedHeight(it.title.c_str(), 16.f, textW);
        const float bodyH  = it.body.empty()  ? 0.f : Gw2Ui::MeasureWrappedHeight(it.body.c_str(),  14.f, textW);
        const std::string age = AgeText(now - it.created);
        const float ageH = 14.f;
        float rowH = padV + titleH + (bodyH > 0.f ? 2.f + bodyH : 0.f) + 2.f + ageH + padV;
        if (rowH < 30.f) rowH = 30.f;

        ImVec2 p = ImGui::GetCursorScreenPos();
        // The whole row is a click target when the item carries an action (the dismiss X, drawn later, wins its
        // own corner via SetItemAllowOverlap). Non-action rows are inert (the InvisibleButton just reserves space).
        const bool actionable = it.action != Notify::Action::None;
        ImGui::PushID((int)it.id);
        ImGui::InvisibleButton("##row", ImVec2(w, rowH));
        ImGui::SetItemAllowOverlap();
        if (actionable && ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); Gw2Ui::Tooltip("Open API settings"); }
        if (actionable && ImGui::IsItemClicked() && it.action == Notify::Action::OpenApiSettings)
            ApiReminder::OpenApiSettings(app);
        ImGui::PopID();
        ImVec2 after = ImGui::GetCursorScreenPos();

        if (shown & 1) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), IM_COL32(255, 255, 255, 8));  // alt stripe
        const ImU32 accent = Notify::Meta(it.kind).accent;
        dl->AddRectFilled(p, ImVec2(p.x + accentW, p.y + rowH), accent);

        float ty = p.y + padV;
        if (!it.title.empty())
        {
            Gw2Ui::LabelDL(dl, ImVec2(p.x + textX, ty), ImVec2(p.x + textX + textW, ty + titleH), it.title.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(238, 232, 212, 255), false, nullptr, 16.f, textW, 0.9f);
            ty += titleH + 2.f;
        }
        if (!it.body.empty())
        {
            Gw2Ui::LabelDL(dl, ImVec2(p.x + textX, ty), ImVec2(p.x + textX + textW, ty + bodyH), it.body.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(196, 190, 172, 255), false, nullptr, 14.f, textW);
            ty += bodyH + 2.f;
        }
        Gw2Ui::LabelDL(dl, ImVec2(p.x + textX, ty), ImVec2(p.x + textX + textW, ty + ageH), age.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(140, 132, 116, 255), false, nullptr, 14.f);

        // per-row dismiss X (top-right)
        ImGui::PushID((int)it.id);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - 18.f, p.y + 4.f));
        ImGui::InvisibleButton("##x", ImVec2(14.f, 14.f));
        const bool xhov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) Notify::Dismiss(it.id);
        if (xhov) Gw2Ui::Tooltip("Dismiss");
        ImGui::PopID();
        const ImU32 xc = xhov ? IM_COL32(255, 200, 200, 255) : Gw2Ui::kTextDim;
        const ImVec2 xc0(p.x + w - 16.f, p.y + 6.f), xc1(p.x + w - 6.f, p.y + 16.f);
        dl->AddLine(xc0, xc1, xc, 1.4f);
        dl->AddLine(ImVec2(xc0.x, xc1.y), ImVec2(xc1.x, xc0.y), xc, 1.4f);

        ImGui::SetCursorScreenPos(after);
        ++shown;
    }
}
