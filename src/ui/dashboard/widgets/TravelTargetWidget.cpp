#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"   // SetClipboard, ViewerAlert
#include "ui/dashboard/widgets/WidgetUtil.h"
#include <imgui.h>
#include <algorithm>
#include <string>

namespace
{
    void WrappedLabel(const char* text, ImU32 color, float fontSize, float width, float weight = -1.f)
    {
        if (!text || !*text) return;
        const float wrapW = std::max(48.f, width);
        const float h = std::max(fontSize + 4.f, Gw2Ui::MeasureWrappedHeight(text, fontSize, wrapW));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, h));
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + width, p.y + h),
                       text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       color, false, nullptr, fontSize, wrapW, weight);
    }

    bool ActionButton(const char* label, float width, Gw2Ui::ActionButtonVariant variant = Gw2Ui::ActionButtonVariant::Normal)
    {
        return Gw2Ui::ActionButton(label, width, 24.f, variant);
    }
}

// The active travel / personal target: where you're headed, the resolved hop, and copy / clear actions.
// Reuses the Travel controller (no new resolver) + the shared clipboard/toast.
void DashW::TravelTarget(App& app, float w)
{
    Travel::Controller& t = app.travel;
    if (!t.Active())
    {
        WrappedLabel("No travel target.", Gw2Ui::kTextDim, 14.f, w);
        WrappedLabel("Alt+click the map or click a zone to set one.", IM_COL32(140, 132, 116, 255), 12.f, w, 0.6f);
        return;
    }

    const std::string title = t.IsPersonal() ? t.PersonalTitle() : ("Travel to " + t.GoalZoneName());
    WrappedLabel(title.c_str(), Gw2Ui::Alpha(Gw2Ui::kGold, 245), 15.f, w, 1.1f);

    const Travel::WpRef& known = t.Known();
    if (known.valid)
    {
        char b[96]; std::snprintf(b, sizeof(b), "Hop: %s", known.name.c_str());
        WrappedLabel(b, IM_COL32(196, 190, 172, 255), 13.f, w);
    }
    else
    {
        WrappedLabel("Follow the arrow / trail on foot.", IM_COL32(196, 190, 172, 255), 13.f, w);
    }

    const bool narrow = DashUtil::Narrow(w);
    if (known.valid && !known.chatLink.empty())
    {
        if (narrow)
        {
            if (ActionButton("Copy waypoint", w, Gw2Ui::ActionButtonVariant::Primary)) { SetClipboard(known.chatLink); ViewerAlert("Copied travel waypoint link - paste/click to teleport."); }
            ImGui::Spacing();
            if (ActionButton("Clear target", w)) t.Clear();
            return;
        }

        const float bw = (w - 8.f) * 0.5f;
        if (ActionButton("Copy waypoint", bw, Gw2Ui::ActionButtonVariant::Primary)) { SetClipboard(known.chatLink); ViewerAlert("Copied travel waypoint link - paste/click to teleport."); }
        ImGui::SameLine(0.f, 8.f);
        if (ActionButton("Clear target", bw)) t.Clear();
    }
    else
    {
        if (ActionButton("Clear target", w)) t.Clear();
    }
}
