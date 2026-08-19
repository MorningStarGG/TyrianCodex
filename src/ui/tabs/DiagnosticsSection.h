#pragma once
class App;

// The Diagnostics section body: rendered inside Options > Diagnostics (and surfaced in settings search). Live
// MumbleLink/guide state, map-scale & DPI calibration, map-assist telemetry, image-cache stats + clear buttons,
// and reset-progress. It is a SECTION inside Options, not a tab. Defined in ui/tabs/DiagnosticsSection.cpp.
void DrawDiagnosticsSection(App& app);

// The keyboard probe (collection API) lives in ui/InputProbe.h -- Gw2Ui's text boxes report into it, and a
// framework header must not include a settings-tab header. Its readout is drawn by DrawDiagnosticsSection.
