#pragma once
class App;

// The Diagnostics section body: rendered inside Options > Diagnostics (and surfaced in settings search). Live
// MumbleLink/guide state, map-scale & DPI calibration, map-assist telemetry, image-cache stats + clear buttons,
// and reset-progress. It is a SECTION inside Options, not a tab. Defined in ui/tabs/DiagnosticsSection.cpp.
void DrawDiagnosticsSection(App& app);
