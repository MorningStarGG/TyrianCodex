#pragma once
#include "app/App.h"

// What the viewer's LIST AREA ("step area") shows this frame, by mode: the zone objective checklist, the
// "head here next" recommended-zone list (zone complete or off-coverage), or the dungeon up-next instructions.
enum class ViewerListKind { Objectives, Recommended, DungeonUpNext };
ViewerListKind CurrentListKind(App& app);

// Whether the viewer shows a bottom/side DETAIL card this frame. Travel target, the current objective (Step),
// and the dungeon instruction get one; Complete/Recommend do not (the header conveys their state and the
// recommended list fills the area).
bool ViewerHasDetailCard(App& app, int dispStep);

bool ShouldHideGuidePanel(App& app);
int CountCompletedObjectives(App& app);
int DisplayStepIndex(App& app, int total);
