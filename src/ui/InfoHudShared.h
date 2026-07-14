#pragma once
#include <string>

// -----------------------------------------------------------------------------------------------------
// InfoHudShared: the small layer SHARED by the HUD launcher bar (src/ui/hud) and the Info Panel data-text bar
// (src/ui/infopanel) -- so neither folder reaches into the other's. Currently the clock/time formatters used by
// the HUD's centre clock AND the Info Panel's clock/reset/session data-texts.
// -----------------------------------------------------------------------------------------------------
namespace HudShared
{
    std::string RealClock(bool h24 = true, bool ampm = true);     // local wall-clock (h24=false -> 12-hour; ampm adds AM/PM)
    std::string ServerClock(bool h24 = true, bool ampm = true);   // server time = UTC (GW2 servers run on UTC)
    std::string TyrianClock(bool h24 = true, bool ampm = true);   // in-game day clock (a full Tyrian day = 2 real hours)

    std::string ClockByType(int type, bool h24 = true, bool ampm = true);   // 0 Local, 1 Server, 2 Tyrian
    const char* ClockTypeLabel(int type);                                   // "Local" / "Server" / "Tyrian"

    std::string DurShort(long secs);   // "1d 5h" / "3h 4m" / "12m" / "45s"
}
