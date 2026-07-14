#include "Widgets.h"
#include "app/App.h"
#include "app/SessionTracker.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <cstdio>
#include <string>

// This-character session progress (time / objectives / levels), read from the single SessionTracker -- the SAME
// state the Sessions tab + the session data texts use (no separate per-surface tracking). Money is the sibling
// Session Earnings widget.
namespace
{
    void Row(const char* k, const std::string& v, float w) { Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f); }
}

void DashW::SessionStats(App& app, float w)
{
    if (!app.state.sessionEarnings.active)
    {
        Gw2Ui::Label("Waiting for character.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }
    const int s = SessionTracker::SessionDurationSec(app);
    char tbuf[24]; std::snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    Row("Session time", tbuf, w);
    Row("Objectives done", std::to_string(SessionTracker::ObjectivesGained(app)), w);
    Row("Levels gained", std::to_string(SessionTracker::LevelsGained(app)), w);
    if (app.state.charLevel > 0) Row("Current level", std::to_string(app.state.charLevel), w);
}
