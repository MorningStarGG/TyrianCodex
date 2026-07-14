#pragma once
#include "Shared.h"      // MumbleIdent
#include "app/State.h"   // GuideState
#include <cstring>       // strnlen
#include <string>

// The active character's name from the MumbleLink identity (empty on char-select / loading).
inline std::string CurrentCharName()
{
    if (!MumbleIdent) return std::string();
    return std::string(MumbleIdent->Name, strnlen(MumbleIdent->Name, sizeof(MumbleIdent->Name)));
}

// Keep the last known character name through loading-screen blanks (MumbleLink reports empty during
// transitions; flipping to a blank key would briefly read the zone as reset). No-op while blank.
inline void UpdateCurrentChar(GuideState& st)
{
    if (!MumbleIdent) return;
    const size_t len = strnlen(MumbleIdent->Name, sizeof(MumbleIdent->Name));
    if (len > 0) st.currentChar.assign(MumbleIdent->Name, len);
}
