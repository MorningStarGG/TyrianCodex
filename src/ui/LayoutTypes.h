#pragma once
#include "LayoutOrder.h"
#include <string>

// -----------------------------------------------------------------------------------------------------
// Shared item types for the three ordered UI layouts (HUD buttons, Info Panel data texts, Dashboard widgets).
// Each exposes a public `key`; the rest is that surface's own per-item placement data. Defined here (json-free,
// via LayoutOrder.h) so the ONE Config struct can hold the layouts directly -- no bespoke per-surface payloads.
// -----------------------------------------------------------------------------------------------------
struct HudBtn
{
    std::string key;
    int side = 0;
}; // side: 0 Left of clock, 1 Right
struct InfoSlot
{
    std::string key;
    int zone = 0;
}; // zone: 0 Left, 1 Center, 2 Right
struct DashSlot
{
    std::string key;
    int span = 1;
    bool hoverChrome = false;
}; // span: 1 full row / 2 half; chrome: hover-only
