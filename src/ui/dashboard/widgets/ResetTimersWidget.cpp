#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <string>

// Daily / weekly reset countdowns + Tyrian (in-game) time. Pure clock math, no API.
namespace
{
    std::string Dur(long s)
    {
        if (s < 0) s = 0;
        char b[32];
        if (s >= 86400) std::snprintf(b, sizeof(b), "%ldd %ldh", s / 86400, (s % 86400) / 3600);
        else            std::snprintf(b, sizeof(b), "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
        return b;
    }
    void Row(const char* label, const std::string& val, float w)
    {
        Gw2Ui::TextValueRow(label, val.c_str(), w, 21.f, 15.f);
    }
}

void DashW::ResetTimers(App& /*app*/, float w)
{
    const std::time_t now = std::time(nullptr);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &now);
#else
    g = *std::gmtime(&now);
#endif
    const long nowSec = g.tm_hour * 3600L + g.tm_min * 60 + g.tm_sec;

    const long toDaily = 86400L - nowSec;
    int daysToMon = ((1 - g.tm_wday) + 7) % 7;            // 0 = today is Monday
    long toWeekly = (long)daysToMon * 86400L + (7 * 3600L + 30 * 60L) - nowSec;   // Monday 07:30 UTC
    if (toWeekly <= 0) toWeekly += 7 * 86400L;

    Row("Daily reset",  Dur(toDaily),  w);
    Row("Weekly reset", Dur(toWeekly), w);

    // Tyrian time (approx): a full in-game day is 2 real hours.
    const double frac = std::fmod((double)now, 7200.0) / 7200.0;
    const int th = (int)(frac * 24.0), tm = (int)(std::fmod(frac * 24.0, 1.0) * 60.0);
    char tt[24]; std::snprintf(tt, sizeof(tt), "%02d:%02d", th, tm);
    Row("Tyrian time", tt, w);
}
