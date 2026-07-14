#include "Widgets.h"
#include "app/App.h"
#include "app/State.h"
#include "render/route/RouteArt.h"
#include "ui/Gw2Ui.h"
#include "ui/QuickMenu.h"       // shared nav-arrow menu
#include "ui/GuideViewer.h"     // CurrentTargetOverride
#include "util/Draw.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    const char* StatusText(const GuidanceSnapshot& g)
    {
        if (g.arrived) return "Arrived";
        if (g.offRoute) return "Off route";
        if (g.travel) return "Travel target";
        if (g.dungeon) return "Dungeon route";
        return "Current objective";
    }

    float RouteTextHeight(float w, const GuidanceSnapshot& g, bool showIcon)
    {
        const float sc = Gw2Ui::TextScale();
        const float titleFs = 16.f;
        const char* title = g.caption.empty() ? "Route target" : g.caption.c_str();
        const float tw = std::max(40.f, w - ((g.iconTex && showIcon) ? titleFs + 8.f : 0.f));   // a type icon to the left shrinks the title's wrap width
        const float titleH = std::max(22.f * sc, Gw2Ui::MeasureWrappedHeight(title, titleFs, tw));
        const float bodyH = 18.f * sc;
        return titleH + bodyH * 2.f;
    }

    void DrawRouteText(ImDrawList* dl, ImVec2 p, float w, const GuidanceSnapshot& g, bool centered, bool showIcon)
    {
        const float sc = Gw2Ui::TextScale();
        const float titleFs = 16.f;
        const float bodyFs = 14.f;
        const float statusFs = 14.f;
        const char* title = g.caption.empty() ? "Route target" : g.caption.c_str();
        // Optional TYPE icon to the left of the title (shrinks + shifts the title; the body/status stay full-width).
        const bool  ico = g.iconTex && showIcon;
        const float titleShift = ico ? titleFs + 8.f : 0.f;
        const float titleX = p.x + titleShift, titleW = std::max(40.f, w - titleShift);
        const float titleH = std::max(22.f * sc, Gw2Ui::MeasureWrappedHeight(title, titleFs, titleW));
        const float bodyH = 18.f * sc;
        const Gw2Ui::HAlign hAlign = centered ? Gw2Ui::HAlign::Center : Gw2Ui::HAlign::Left;

        char dist[32]; FormatDistance(dist, sizeof(dist), g.distance);
        const std::string eta = FormatEta(g.distance, g.speed);
        char line[96];
        std::snprintf(line, sizeof(line), "%s    ETA %s", dist, eta.c_str());

        if (ico)
        {
            const float is = titleFs + 3.f;
            dl->AddImage((ImTextureID)g.iconTex, ImVec2(p.x, p.y + (titleH - is) * 0.5f), ImVec2(p.x + is, p.y + (titleH + is) * 0.5f));
        }
        Gw2Ui::LabelDL(dl, ImVec2(titleX, p.y), ImVec2(titleX + titleW, p.y + titleH), title,
                       hAlign, Gw2Ui::VAlign::Middle,
                       g.arrived ? IM_COL32(150, 245, 160, 255) : Gw2Ui::kGold,
                       true, nullptr, titleFs, titleW, 1.1f);
        Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y + titleH), ImVec2(p.x + w, p.y + titleH + bodyH), line,
                       hAlign, Gw2Ui::VAlign::Middle,
                       IM_COL32(215, 207, 184, 255), false, nullptr, bodyFs, 0.f, 1.0f);
        Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y + titleH + bodyH), ImVec2(p.x + w, p.y + titleH + bodyH * 2.f),
                       StatusText(g), hAlign, Gw2Ui::VAlign::Middle,
                       g.offRoute ? IM_COL32(255, 155, 90, 255) : IM_COL32(170, 162, 140, 255),
                       false, nullptr, statusFs, 0.f, 1.0f);
    }
}

void DashW::RouteArrow(App& app, float w)
{
    const GuidanceSnapshot& g = app.state.guidanceSnapshot;
    if (!g.active)
    {
        Gw2Ui::Label("No active route.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        Gw2Ui::Label("Start guidance or set a travel target.", IM_COL32(140, 132, 116, 255), false, nullptr, 14.f);
        return;
    }

    const float sc = Gw2Ui::TextScale();
    const bool stack = w < 230.f * sc;
    const float arrowSize = std::min(stack ? 88.f * sc : 78.f * sc, std::max(56.f * sc, w * 0.42f));
    const float gap = 10.f * sc;
    const float textW = stack ? std::max(40.f, w - 8.f * sc) : std::max(40.f, w - arrowSize - gap);
    const float textH = RouteTextHeight(textW, g, app.config.wRouteArrowIcon);
    const float h = stack ? (arrowSize + gap * 0.5f + textH) : std::max(arrowSize, textH);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##routeArrowBtn", ImVec2(w, h));   // reserve the row AND catch a right-click for the arrow menu
    const bool menuOpenNow = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const Render::DirectIndicatorStyle dstyle{ app.config.wRouteArrowChevron, app.config.wRouteArrowPing,
                                               app.config.wRouteArrowEverySecs, app.config.wRouteArrowShowSecs };
    if (stack)
    {
        Render::DrawRouteArrow(dl, ImVec2(p.x + w * 0.5f, p.y + arrowSize * 0.5f), arrowSize, g, app.config.wRouteArrowDirect, dstyle);
        DrawRouteText(dl, ImVec2(p.x + (w - textW) * 0.5f, p.y + arrowSize + gap * 0.5f), textW, g, true, app.config.wRouteArrowIcon);
    }
    else
    {
        Render::DrawRouteArrow(dl, ImVec2(p.x + arrowSize * 0.5f, p.y + h * 0.5f), arrowSize, g, app.config.wRouteArrowDirect, dstyle);
        DrawRouteText(dl, ImVec2(p.x + arrowSize + gap, p.y + (h - textH) * 0.5f),
                      textW, g, false, app.config.wRouteArrowIcon);
    }

    // Right-click -> the shared nav-arrow menu (no lock/reset here -- the widget's placement is the dashboard's job).
    std::vector<Gw2Ui::MenuNode> nodes;
    QuickMenu::ArrowNodes(app.config, CurrentTargetOverride(app), /*dungeon*/ false,
                          app.state.activeContent == ContentType::Farming,
                          app.state.activeContent == ContentType::Fishing, /*inWorld*/ false, nodes);
    const int id = Gw2Ui::ContextMenuTree("##routeArrowMenu", nodes, menuOpenNow);
    if (id >= 0) QuickMenu::ArrowDispatch(app, id);
}
