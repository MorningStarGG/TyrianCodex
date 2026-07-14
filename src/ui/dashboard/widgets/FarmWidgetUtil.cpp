#include "FarmWidgetUtil.h"
#include "app/App.h"
#include "guide/FarmCategories.h"
#include "ui/Gw2Ui.h"
#include "util/Draw.h"
#include "util/Textures.h"
#include "Shared.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

void DashFarm::DrawNodeRow(App& app, const FarmNode* nd, float w, bool big)
{
    if (!nd) return;
    const float sc = Gw2Ui::TextScale();
    const float nameFs = (big ? 17.f : 16.f);
    const float distFs = (big ? 14.f : 13.f);
    const float pillFs = 14.f;
    const float iconSz = (big ? 30.f : 22.f) * sc;

    float dist = 0.f;
    if (MumbleLink) { const float dx = nd->X - MumbleLink->AvatarPosition.X, dz = nd->Z - MumbleLink->AvatarPosition.Z; dist = std::sqrt(dx * dx + dz * dz); }
    const std::string name = Farm::MaterialPretty(nd->Material);

    const float textX = iconSz + 8.f;
    const float mntW   = nd->Mount.empty() ? 0.f : Gw2Ui::PillWidth(nd->Mount.c_str(), pillFs);
    const float reserve = nd->Mount.empty() ? 0.f : mntW + 8.f * sc;
    const float textW  = std::max(40.f, w - textX - reserve);
    const float nameH  = Gw2Ui::MeasureWrappedHeight(name.c_str(), nameFs, textW);

    char dbuf[48]; char dval[32]; FormatDistance(dval, sizeof(dval), dist);
    if (big) std::snprintf(dbuf, sizeof(dbuf), "%s    ETA %s", dval, FormatEta(dist, app.guidance.SpeedEma()).c_str());
    else     std::snprintf(dbuf, sizeof(dbuf), "%s away", dval);
    const float distH = Gw2Ui::MeasureWrappedHeight(dbuf, distFs, textW);
    const float rowH  = std::max(iconSz, nameH + distH + 2.f) + (big ? 4.f : 6.f);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, rowH));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (void* tex = nd->Icon.empty() ? nullptr : Tex::GetBundledTexture(("data\\textures\\markers\\gather\\" + nd->Icon + ".png").c_str()))
        dl->AddImage((ImTextureID)tex, ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(p.x + 2.f + iconSz, p.y + 2.f + iconSz));

    float ty = p.y + 2.f;
    Gw2Ui::LabelDL(dl, ImVec2(p.x + textX, ty), ImVec2(p.x + textX + textW, ty + nameH), name.c_str(),
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, big ? Gw2Ui::kGold : IM_COL32(150, 235, 150, 255),
                   big, nullptr, nameFs, textW, 1.1f);
    ty += nameH + 2.f;
    Gw2Ui::LabelDL(dl, ImVec2(p.x + textX, ty), ImVec2(p.x + textX + textW, ty + distH), dbuf,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(200, 194, 176, 255), false, nullptr, distFs, textW);

    if (!nd->Mount.empty())
    {
        const float pillH = Gw2Ui::PillHeight(pillFs);
        Gw2Ui::PillAt(dl, ImVec2(p.x + w - mntW, p.y + (rowH - pillH) * 0.5f), nd->Mount.c_str(), pillFs,
                      IM_COL32(96, 170, 200, 200), IM_COL32(196, 230, 245, 255), IM_COL32(20, 44, 56, 150));
    }
}
