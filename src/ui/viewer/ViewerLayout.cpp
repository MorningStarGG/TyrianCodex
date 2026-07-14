#include "ui/viewer/ViewerLayout.h"
#include "util/Draw.h"
#include "Shared.h"          // CurrentMapId
#include <algorithm>
#include <cmath>

int ViewerStyleIndex(const App& app)
{
    if (app.config.viewerStyle < 0 || app.config.viewerStyle >= kViewerStyleCount)
        return ViewerStyleCompact;
    return app.config.viewerStyle;
}

int TaskChipStyleIndex(const App& app)
{
    return std::clamp(app.config.taskChipStyle, 0, kTaskChipStyleCount - 1);
}

int TaskIconSizeIndex(const App& app)
{
    return std::clamp(app.config.taskIconSize, 0, kTaskIconSizeCount - 1);
}

int ResolvedTaskIconSize(const App& app, bool compact)
{
    const int choice = TaskIconSizeIndex(app);
    if (choice != TaskIconSizeAuto) return choice;
    return compact ? TaskIconSizeSmall : TaskIconSizeMedium;   // Auto: explicit style, not the global viewer style
}

int TaskIconArtIndex(const App& app)
{
    return std::clamp(app.config.taskIconArt, 0, kTaskIconArtCount - 1);
}

int RichSecondaryIndex(const App& app)
{
    return std::clamp(app.config.richSecondary, 0, kRichSecondaryCount - 1);
}

bool ViewerCompact(const App& app) { return ViewerStyleIndex(app) == ViewerStyleCompact; }
bool ViewerRich(const App& app)    { return ViewerStyleIndex(app) == ViewerStyleRich; }

bool FarmingMapHasData(App& app)
{
    const uint32_t mapId = CurrentMapId();
    return mapId != 0 && app.farming.find(mapId) != app.farming.end();
}

bool FarmingTabAvailable(App& app)
{
    return FarmingMapHasData(app) && !app.travel.Active() && !app.state.inDungeon;
}

bool FarmingRunActive(App& app)
{
    return app.state.farmRunActive && FarmingTabAvailable(app);
}

bool FishingMapHasData(App& app)
{
    const uint32_t mapId = CurrentMapId();
    return mapId != 0 && app.fishHoles.find(mapId) != app.fishHoles.end();
}

bool FishingTabAvailable(App& app)
{
    return FishingMapHasData(app) && !app.travel.Active() && !app.state.inDungeon;
}

bool FishingRunActive(App& app)
{
    return app.state.fishRunActive && FishingTabAvailable(app);
}

bool TaskIconsExpanded(const App& app)
{
    return TaskIconArtIndex(app) == TaskIconArtExpanded;
}

float ViewerPanelAutoWidth(const App& app)
{
    return ViewerRich(app) ? 580.f : 480.f;
}

float ViewerScale(const App& app)
{
    return std::clamp(app.config.viewerScale, 0.70f, 1.30f);
}

ImVec2 ViewerWindowPadding(const App& app)
{
    // Small LEFT/RIGHT padding so the (opaque) content reaches the frame border instead of leaving a
    // translucent strip that reads as a "gap" over the see-through panel (the travel offer card avoids this by
    // filling its bg edge-to-edge). Keep the vertical padding for top/bottom breathing room.
    const float vs = ViewerScale(app);
    return ImVec2(4.f * vs, 4.f * vs);
}

ViewerWindowSizing StepWindowSizing(const App& app)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float maxDisplayW = std::max(320.f, display.x * 0.96f);
    const float maxDisplayH = std::max(240.f, display.y * 0.92f);
    const float vs = ViewerScale(app);   // uniform zoom: the preferred/bounds track the scaled content (never past the display)
    ViewerWindowSizing s{};

    if (ViewerRich(app))
    {
        s.Aspect = 1.55f;
        s.Max.x = std::min(1500.f * vs, std::min(maxDisplayW, maxDisplayH * s.Aspect));
        s.Min.x = std::min(980.f * vs, s.Max.x);
        s.Max.y = s.Max.x / s.Aspect;
        s.Min.y = s.Min.x / s.Aspect;
        const float prefW = std::clamp(1220.f * vs, s.Min.x, s.Max.x);
        s.Preferred = ImVec2(prefW, prefW / s.Aspect);
        return s;
    }

    s.Aspect = 0.64f;
    s.Max.x = std::min(720.f * vs, std::min(maxDisplayW, maxDisplayH * s.Aspect));
    s.Min.x = std::min(430.f * vs, s.Max.x);
    s.Max.y = s.Max.x / s.Aspect;
    s.Min.y = s.Min.x / s.Aspect;
    const float prefW = std::clamp(580.f * vs, s.Min.x, s.Max.x);
    s.Preferred = ImVec2(prefW, prefW / s.Aspect);
    return s;
}

ImVec2 ClampStepWindowSize(ImVec2 size, const ViewerWindowSizing& s)
{
    if (s.Aspect > 0.f)
    {
        float w = std::clamp(size.x, s.Min.x, s.Max.x);
        return ImVec2(w, w / s.Aspect);
    }
    return ImVec2(std::clamp(size.x, s.Min.x, s.Max.x),
                  std::clamp(size.y, s.Min.y, s.Max.y));
}

static bool SavedSizeMatchesAspect(ImVec2 size, float aspect)
{
    if (aspect <= 0.f) return true;
    if (size.x < 1.f || size.y < 1.f) return false;
    return std::fabs(size.x / size.y - aspect) <= 0.08f;
}

ImVec2 InitialStepWindowSize(ImVec2 saved, const ViewerWindowSizing& s)
{
    if (s.Aspect > 0.f)
    {
        if (saved.x < s.Min.x - 1.f || saved.x > s.Max.x + 1.f || !SavedSizeMatchesAspect(saved, s.Aspect))
            return s.Preferred;
    }
    return ClampStepWindowSize(saved, s);
}

void FixedAspectResizeCallback(ImGuiSizeCallbackData* data)
{
    const ViewerAspectConstraint* c = static_cast<const ViewerAspectConstraint*>(data->UserData);
    if (!c || c->Aspect <= 0.f) return;

    const float w = std::clamp(data->DesiredSize.x, c->MinW, c->MaxW);
    data->DesiredSize = ImVec2(w, w / c->Aspect);
}

ImVec2 ResetGuidePanelSize(const App& app)
{
    return StepWindowSizing(app).Preferred;
}
