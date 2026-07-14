#include "ui/tabs/SettingsCommon.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/Glue.h"
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

// Draw a status line CENTERED across the current content width (wraps if long), as a small styled banner.
// Used for the Story / Zones tab top status text (was small + left-aligned) so it reads as a heading.
void CenteredLabel(const char* text, ImU32 color, float size, float weight, bool stroke)
{
    if (!text || !*text) return;
    const float  w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  h = Gw2Ui::MeasureWrappedHeight(text, size, w);
    Gw2Ui::LabelIn(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + h), text,
                   Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top, color, stroke, nullptr, size, w, weight);
    ImGui::Dummy(ImVec2(w, h));
}

// Left-aligned wrapped help/description paragraph (the recurring settings prose block). One implementation so
// every section's intro/help text wraps + sizes identically instead of re-deriving the measure/Dummy/LabelIn.
void SettingsParagraph(const char* text, ImU32 color, float size)
{
    if (!text || !*text) return;
    // Wrap to the current card's inner width when inside a card (CardInnerWidth == GetContentRegionAvail when
    // not in a card), so paragraph text never overruns the card's right border.
    const float  w = Gw2Ui::CardInnerWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  h = Gw2Ui::MeasureWrappedHeight(text, size, w);
    ImGui::Dummy(ImVec2(w, h));
    Gw2Ui::LabelIn(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + h), text,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, color, false, nullptr, size, w);
}
