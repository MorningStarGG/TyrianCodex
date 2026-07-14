#pragma once
#include <string>
#include <vector>
#include "ui/Gw2Ui.h" // Gw2Ui::Dye

// The GW2 dye palette (the /v2/colors data) that backs the ColorBox / ColorPicker.
// We load the bundled snapshot at <addonDir>/data/colors.json
// (fetched from https://api.guildwars2.com/v2/colors?ids=all); it is static game data.
namespace Dyes
{
    // Parse the dye palette into `out`. Each Dye.name points into `nameStore`, which MUST outlive `out`
    // (keep both alive for the addon's lifetime). Uses each dye's CLOTH rgb.
    bool Load(const std::string &addonDir, std::vector<Gw2Ui::Dye> &out, std::vector<std::string> &nameStore);
}
