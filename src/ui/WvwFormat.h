#pragma once
#include <imgui.h>
#include <cstdio>
#include <string>

// Shared WvW formatting helpers (team color + large-score abbreviation) for the dashboard widgets.
// Color convention: 1=red, 2=blue, 3=green; 0/other = neutral cream. (The Info Panel WvW texts keep their
// own file-local copies of these tiny helpers.)
namespace WvwFmt
{
    inline ImU32 WvwColor(int c)
    {
        return c == 1 ? IM_COL32(228, 92, 92, 255)
             : c == 2 ? IM_COL32(92, 138, 228, 255)
             : c == 3 ? IM_COL32(96, 196, 112, 255)
                      : IM_COL32(236, 230, 212, 255);
    }
    inline std::string WvwAbbrev(long long n)
    {
        char b[24];
        if (n >= 1000000)    std::snprintf(b, sizeof(b), "%.2fM", n / 1000000.0);
        else if (n >= 10000) std::snprintf(b, sizeof(b), "%.1fk", n / 1000.0);
        else                 std::snprintf(b, sizeof(b), "%lld", n);
        return b;
    }
}
