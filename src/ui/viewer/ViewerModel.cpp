#include "ui/viewer/ViewerModel.h"
#include "guide/StepStatus.h"

// LIST AREA content for this frame. Dungeon -> the up-next instructions; off-coverage (no zone) or a fully
// finished zone -> the recommended-zone list; otherwise the normal objective checklist.
ViewerListKind CurrentListKind(App& app)
{
    if (app.state.viewerMode == ViewerMode::Dungeon) return ViewerListKind::DungeonUpNext;
    if (!app.state.zone.Loaded || app.state.zone.Steps.empty()) return ViewerListKind::Recommended;
    for (const Step& s : app.state.zone.Steps)
        if (!StepIsDone(app.progress, app.state.currentChar, s))
            return ViewerListKind::Objectives;
    return ViewerListKind::Recommended;   // every objective done -> recommend where to go next
}

bool ViewerHasDetailCard(App& app, int dispStep)
{
    if (app.travel.Active()) return true;                              // travel target card
    if (app.state.viewerMode == ViewerMode::Dungeon) return true;     // "Do this" instruction card
    if (app.state.viewerMode == ViewerMode::Step && dispStep >= 0) return true;   // current-objective card
    return false;                                                     // Complete / Recommend -> header conveys it
}

bool ShouldHideGuidePanel(App& app)
{
    if (!app.state.zone.Loaded || !app.config.hideWhenComplete || !app.state.manualTarget.empty() ||
        app.travel.Active() || app.state.zone.Steps.empty())
        return false;

    for (const Step& s : app.state.zone.Steps)
        if (!StepIsDone(app.progress, app.state.currentChar, s))
            return false;
    return true;
}

int CountCompletedObjectives(App& app)
{
    int done = 0;
    for (const Step& s : app.state.zone.Steps)
        if (StepIsDone(app.progress, app.state.currentChar, s))
            ++done;
    return done;
}

int DisplayStepIndex(App& app, int total)
{
    int dispStep = (app.state.curStep >= 0 && app.state.curStep < total &&
                    !StepIsDone(app.progress, app.state.currentChar, app.state.zone.Steps[app.state.curStep])) ? app.state.curStep : -1;
    if (dispStep < 0)
        for (int i = 0; i < total; ++i)
            if (!StepIsDone(app.progress, app.state.currentChar, app.state.zone.Steps[i]))
            {
                dispStep = i;
                break;
            }
    return dispStep;
}
