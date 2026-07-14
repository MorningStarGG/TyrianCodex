#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "api/v2/Common.h" // Api::V2::ItemSlot

namespace AccountData
{
    struct ItemMeta;
}

// Equipment-based character attributes: EQUIPMENT ONLY: base (level/profession) + gear + runes/sigils/infusions
// + the derived secondaries (Armor, Health, CritChance, CritDamage, Condition/Boon Duration). It does NOT
// include traits / food / boons, Does not always match the in-game Hero panel.
// Scaling tables are taken verbatim from the gw2efficiency source.
namespace ItemAttributes
{
    // metaFor(id) -> the item's catalog ItemMeta (its `details`: defense / infix_upgrade / bonuses / buff), or
    // null if not yet loaded. `equipment` is the character's equipped ItemSlots (each with id + selected `stats`
    // in raw + upgrade/infusion ids). Returns the full attribute map (keys: Power/Toughness/Vitality/Precision/
    // Ferocity/Armor/ConditionDamage/ConditionDuration/HealingPower/BoonDuration/AgonyResistance/Concentration/
    // Expertise/CritChance/Health/CritDamage). CritChance/durations are fractions (0.42 = 42%).
    std::map<std::string, double> ParseCharacter(
        int level, const std::string &profession,
        const std::vector<Api::V2::ItemSlot> &equipment,
        const std::function<const AccountData::ItemMeta *(int)> &metaFor);
}
