#pragma once
#include "app/App.h"
#include <imgui.h>
#include <cstdint>

// Per-surface Zone Header configuration. The same scenic banner is used by the guide viewer AND the dashboard
// "Zone Header" widget, each configured independently (six Config fields: zhViewer* / zhWidget*). The two pills
// (level-range + progress) always show; the completion bar is Off / Integrated (slim fill on the banner's
// bottom edge, no text) / Below (a Gw2Ui::ProgressBar under the banner whose text placement is barText).
struct ZoneHeaderOpts
{
    int   pills    = 0;      // 0 Side-by-side (level left, progress furthest right), 1 Stacked (level top, progress bottom)
    int   bar      = 2;      // 0 Off, 1 Integrated, 2 Below
    int   barText  = 1;      // 0 On bar, 1 Above, 2 Below, 3 None  (meaningful when bar == Below)
    bool  isWidget = false;  // true = dashboard widget (enables the half-width reflow + width-driven sizing)
    float width    = 0.f;    // surface content width; 0 => fall back to ImGui::GetContentRegionAvail().x
};

// The viewer's scenic / hero header: the map-art banner, compass rose, and the mode-aware header bar.
// Defined in ui/viewer/ViewerHeader.cpp. frac/hasFrac drive the Integrated bar (drawn inside the art);
// regionText is the dim sub-line under the title ("" = none, e.g. dungeon / off-coverage).
void DrawRichHeroHeaderArt(App& app, uint32_t mapId, const char* title, const char* regionText,
                           const char* levelText, const char* progressText, float fs,
                           const ZoneHeaderOpts& o, float frac, bool hasFrac);
void DrawRichHeroHeader(App& app, int done, int total, float fs, const ZoneHeaderOpts& o);
// Mode-aware header for the whole viewer: zone+Lv+progress (Step/Travel/Complete), map name + "No guide
// route" + level (Recommend / off-coverage), dungeon name + path + step + bar (Dungeon). Never "GW2 Guide".
void DrawViewerHeader(App& app, int done, int total, float fs, bool compact, const ZoneHeaderOpts& o);
