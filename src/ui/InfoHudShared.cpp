#include "InfoHudShared.h"
#include <cmath>
#include <cstdio>
#include <ctime>

namespace
{
    std::string FmtHM(int hh, int mm, bool h24, bool ampm)
    {
        char b[16];
        if (h24) { std::snprintf(b, sizeof(b), "%02d:%02d", hh, mm); return b; }
        int h = hh % 12; if (h == 0) h = 12;
        if (ampm) std::snprintf(b, sizeof(b), "%d:%02d %s", h, mm, hh < 12 ? "AM" : "PM");
        else      std::snprintf(b, sizeof(b), "%d:%02d", h, mm);
        return b;
    }
}

std::string HudShared::RealClock(bool h24, bool ampm)
{
    std::time_t now = std::time(nullptr);
    std::tm lt{}; localtime_s(&lt, &now);
    return FmtHM(lt.tm_hour, lt.tm_min, h24, ampm);
}

std::string HudShared::ServerClock(bool h24, bool ampm)
{
    std::time_t now = std::time(nullptr);
    std::tm gt{}; gmtime_s(&gt, &now);
    return FmtHM(gt.tm_hour, gt.tm_min, h24, ampm);
}

std::string HudShared::TyrianClock(bool h24, bool ampm)
{
    std::time_t now = std::time(nullptr);
    const double frac = std::fmod((double)now, 7200.0) / 7200.0;   // 2 real hours = 1 Tyrian day
    const int th = (int)(frac * 24.0);
    const int tm = (int)(std::fmod(frac * 24.0, 1.0) * 60.0);
    return FmtHM(th, tm, h24, ampm);
}

std::string HudShared::ClockByType(int type, bool h24, bool ampm)
{
    switch (type) { case 1: return ServerClock(h24, ampm); case 2: return TyrianClock(h24, ampm); default: return RealClock(h24, ampm); }
}

const char* HudShared::ClockTypeLabel(int type)
{
    switch (type) { case 1: return "Server"; case 2: return "Tyrian"; default: return "Local"; }
}

std::string HudShared::DurShort(long s)
{
    if (s < 0) s = 0;
    const long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
    char b[32];
    if (d > 0)      std::snprintf(b, sizeof(b), "%ldd %ldh", d, h);
    else if (h > 0) std::snprintf(b, sizeof(b), "%ldh %ldm", h, m);
    else if (m > 0) std::snprintf(b, sizeof(b), "%ldm", m);
    else            std::snprintf(b, sizeof(b), "%lds", sec);
    return b;
}
