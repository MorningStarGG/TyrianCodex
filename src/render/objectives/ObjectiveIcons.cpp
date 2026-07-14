#include "render/objectives/ObjectiveIcons.h"
#include "model/ObjectiveTypes.h"
#include "util/Textures.h"
#include <cstdint>

void Render::PaintObjectiveTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 topLeft,
                                      float size, bool medallion)
{
    if (!dl || size <= 0.f) return;
    uint32_t aid = 0;
    const bool hasIcon = Objective::TryIconAssetId(type, aid);
    const ImU32 accent = Objective::ColorOf(type, 230);
    if (medallion)
    {
        const ImVec2 c(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
        dl->AddCircleFilled(c, size * 0.5f, IM_COL32(0, 0, 0, 105), 28);
        dl->AddCircle(c, size * 0.5f - 1.f, accent, 28, 1.4f);
    }
    if (hasIcon)
        if (void* tx = Tex::GetTextureFromAssetId(aid))
        {
            const float is = medallion ? size * 0.62f : size;
            const ImVec2 c(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
            dl->AddImage((ImTextureID)tx, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f),
                         ImVec2(c.x + is * 0.5f, c.y + is * 0.5f));
        }
}
