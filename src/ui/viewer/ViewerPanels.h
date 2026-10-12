#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>

// The viewer's detail-card content panels: the current-objective panel (active step + heart chips + actions)
// and the travel-target panel, plus their auto-height estimators. Defined in ui/viewer/ViewerPanels.cpp.
// `compact` selects the element's look explicitly (the compact viewer + dashboard pass true; the rich viewer
// passes false), so these never read the global viewer style -- the dashboard stays independent of it.
float EstimateTravelTargetPanelHeight(App& app, float fs, float cardW, bool compact);
float EstimateCurrentObjectivePanelHeight(App& app, const Step& c, float fs, float cardW, bool compact);
// The dashboard supplies the frame and requests InlineAccent; viewer callers retain standalone cards.
void DrawCurrentObjectivePanel(App& app, const Step& c, float fs, ImU32 subCol, bool compact,
                              Gw2Ui::SectionPresentation presentation = Gw2Ui::SectionPresentation::Card);
void DrawTravelTargetPanel(App& app, float fs, ImU32 subCol, bool compact,
                          Gw2Ui::SectionPresentation presentation = Gw2Ui::SectionPresentation::Card);

// Shared current-objective metadata: the dist / ETA / Lv chip row with a right-aligned "Mark Complete" pill on
// the SAME row. Full text when it fits; a checkmark-only pill when the row is too narrow (never a 2nd line, so
// the row height is unchanged). One implementation for the rich viewer, the compact viewer AND the dashboard
// widget. Clicking the pill marks the step done (the guide advances).
void DrawObjectiveChipRow(App& app, const Step& c, float fs, ImU32 typeCol);
