#pragma once
#include "app/App.h"

namespace HeartHints
{
    bool Enabled(const Config& config);
    void Draw(App& app, const FrameCtx& fc, uint32_t mapId);
}
