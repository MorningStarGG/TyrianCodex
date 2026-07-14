#include "app/ItemAttributes.h"
#include "app/AccountData.h" // AccountData::ItemMeta
#include <nlohmann/json.hpp>
#include <cmath>
#include <regex>
#include <set>

// English attribute strings only (the addon fetches the API in English)
// the multi-language alternates from the source are omitted.
namespace
{
    // ---- scaledBaseAttributes ----
    const int kAttributeGrowth[80] = {
        0, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        0, 10, 0, 10, 0, 10, 0, 10, 0, 10,
        0, 14, 0, 14, 0, 15, 0, 16, 0, 16,
        0, 20, 0, 20, 0, 20, 0, 20, 0, 20,
        0, 24, 0, 24, 0, 25, 0, 26, 0, 26,
        0, 30, 0, 30, 0, 30, 0, 30, 0, 30,
        0, 34, 0, 34, 0, 35, 0, 36, 0, 36,
        0, 44, 0, 44, 0, 45, 0, 46, 0, 46};

    int ScaledBaseAttributes(int level)
    {
        int a = 37;
        for (int i = 0; i < level && i < 80; ++i)
            a += kAttributeGrowth[i];
        return a;
    }

    // ---- scaledBaseHealth (per-profession 5-band table; band = floor((i+1)/20)) ----
    double BaseHealthBand(const std::string &prof, int band)
    {
        static const double low[5] = {5, 12.5, 25, 37.5, 50};  // Elementalist / Guardian / Thief
        static const double med[5] = {18, 45, 90, 135, 180};   // Engineer / Ranger / Mesmer / Revenant
        static const double high[5] = {28, 70, 140, 210, 280}; // Warrior / Necromancer
        if (band < 0)
            band = 0;
        if (band > 4)
            band = 4;
        if (prof == "Elementalist" || prof == "Guardian" || prof == "Thief")
            return low[band];
        if (prof == "Warrior" || prof == "Necromancer")
            return high[band];
        return med[band]; // Engineer/Ranger/Mesmer/Revenant (+ default)
    }
    double ScaledBaseHealth(int level, const std::string &prof)
    {
        double h = 0;
        for (int i = 0; i < level; ++i)
            h += BaseHealthBand(prof, (i + 1) / 20);
        return h;
    }

    // ---- scaledAttributeBonus (divisor tables 15 & 21, indexed by level-1) ----
    const double kBonus15[80] = {
        0.79, 0.86, 0.93, 1.00, 1.07, 1.14, 1.21, 1.29, 1.36, 1.43,
        1.50, 1.57, 1.64, 1.71, 1.79, 1.85, 1.93, 2.00, 2.07, 2.14,
        2.29, 2.43, 2.57, 2.71, 2.86, 3.00, 3.14, 3.28, 3.42, 3.57,
        3.71, 3.86, 4.00, 4.14, 4.29, 4.43, 4.57, 4.71, 4.86, 5.00,
        5.21, 5.43, 5.64, 5.86, 6.07, 6.28, 6.50, 6.71, 6.93, 7.14,
        7.36, 7.57, 7.79, 8.00, 8.21, 8.42, 8.64, 8.85, 9.07, 9.29,
        9.57, 9.85, 10.14, 10.43, 10.71, 11.00, 11.29, 11.57, 11.86, 12.14,
        12.43, 12.71, 13.00, 13.29, 13.57, 13.86, 14.14, 14.43, 14.71, 15.00};
    const double kBonus21[80] = {
        1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0,
        2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.0,
        3.2, 3.4, 3.6, 3.8, 4.0, 4.2, 4.4, 4.6, 4.8, 5.0,
        5.2, 5.4, 5.6, 5.8, 6.0, 6.2, 6.4, 6.6, 6.8, 7.0,
        7.3, 7.6, 7.9, 8.2, 8.5, 8.8, 9.1, 9.5, 9.7, 10.0,
        10.3, 10.6, 10.9, 11.2, 11.5, 11.8, 12.1, 12.4, 12.7, 13.0,
        13.4, 13.8, 14.2, 14.6, 15.0, 15.4, 15.8, 16.2, 16.6, 17.0,
        17.4, 17.8, 18.2, 18.6, 19.0, 19.4, 19.8, 20.2, 20.6, 21.0};
    double ScaledAttributeBonus(int level, int which)
    {
        int idx = level - 1;
        if (idx < 0)
            idx = 0;
        if (idx > 79)
            idx = 79;
        return (which == 21) ? kBonus21[idx] : kBonus15[idx];
    }

    // ---- officialAttributeMap (API attribute name -> internal key) ----
    std::string OfficialAttr(const std::string &a)
    {
        if (a == "BoonDuration")
            return "Concentration";
        if (a == "ConditionDuration")
            return "Expertise";
        if (a == "CritDamage")
            return "Ferocity";
        if (a == "Healing")
            return "HealingPower";
        // Power/Precision/Toughness/Vitality/ConditionDamage/AgonyResistance pass through
        return a;
    }

    void Merge(std::map<std::string, double> &into, const std::map<std::string, double> &add)
    {
        for (const auto &kv : add)
            into[kv.first] += kv.second;
    }

    // ---- (English-only): pull "+N Attr" / "Attr +N%" out of rune/buff text ----
    struct AttrStr
    {
        const char *key;
        const char *name;
    };
    const AttrStr kAttrStrings[] = {
        {"Power", "Power"}, {"Toughness", "Toughness"}, {"Vitality", "Vitality"}, {"Precision", "Precision"}, {"Ferocity", "Ferocity"}, {"ConditionDamage", "Condition Damage"}, {"ConditionDuration", "Condition Duration"}, {"HealingPower", "Healing"}, {"BoonDuration", "Boon Duration"}, {"AgonyResistance", "Agony Resistance"}, {"Concentration", "Concentration"}, {"Expertise", "Expertise"}, {"CritChance", "Critical Chance"}, {"__AllStats__", "to All Stats"}};
    const char *kAllStats[] = {"Power", "Toughness", "Vitality", "Precision", "Ferocity", "ConditionDamage", "HealingPower"};

    std::map<std::string, double> ParseString(const std::string &text, const nlohmann::json *infixAttrs)
    {
        std::map<std::string, double> out;
        if (text.empty())
            return out;
        // dedup set: "+<modifier> <ApiAttribute>" (so a buff description doesn't double-count the infix attributes)
        std::set<std::string> infixSet;
        if (infixAttrs && infixAttrs->is_array())
            for (const auto &a : *infixAttrs)
                if (a.is_object() && a.contains("modifier") && a["modifier"].is_number_integer() && a.contains("attribute") && a["attribute"].is_string())
                    infixSet.insert("+" + std::to_string(a["modifier"].get<int>()) + " " + a["attribute"].get<std::string>());

        for (const AttrStr &as : kAttrStrings)
        {
            std::string pat = std::string("(?:\\+ ?(\\d*) ?%? )?(?:") + as.name + ")(?: \\+ ?(\\d*) ?%?)?";
            std::regex re(pat, std::regex::icase);
            for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
            {
                const std::smatch &m = *it;
                int value = 0;
                if (m[1].matched && m[1].length() > 0)
                    value = std::atoi(m[1].str().c_str());
                if (!value && m[2].matched && m[2].length() > 0)
                    value = std::atoi(m[2].str().c_str());
                if (!value)
                    continue;
                const bool all = std::string(as.key) == "__AllStats__";
                const int n = all ? (int)(sizeof(kAllStats) / sizeof(kAllStats[0])) : 1;
                for (int k = 0; k < n; ++k)
                {
                    const char *key = all ? kAllStats[k] : as.key;
                    if (infixSet.count("+" + std::to_string(value) + " " + key))
                        continue;
                    out[key] += value;
                }
            }
        }
        return out;
    }

    // ---- parseItem (one gear/rune/infusion "item") ----
    void ParseItem(std::map<std::string, double> &attrs, const AccountData::ItemMeta *meta,
                   const Api::V2::ItemSlot *slot, std::map<int, int> &runeCount, int itemId, const std::string &rarity)
    {
        if (!meta || !meta->details.is_object())
        {
            // Even with no catalog details, selectable stats on the slot still count.
            if (slot && slot->raw.is_object() && slot->raw.contains("stats") && slot->raw["stats"].is_object() && slot->raw["stats"].contains("attributes") && slot->raw["stats"]["attributes"].is_object())
                for (auto it = slot->raw["stats"]["attributes"].begin(); it != slot->raw["stats"]["attributes"].end(); ++it)
                    if (it.value().is_number())
                        attrs[OfficialAttr(it.key())] += it.value().get<double>();
            return;
        }
        const nlohmann::json &d = meta->details;

        if (d.contains("defense") && d["defense"].is_number())
            attrs["Armor"] += d["defense"].get<double>();

        const nlohmann::json *infix = (d.contains("infix_upgrade") && d["infix_upgrade"].is_object()) ? &d["infix_upgrade"] : nullptr;
        if (infix && infix->contains("attributes") && (*infix)["attributes"].is_array())
            for (const auto &a : (*infix)["attributes"])
                if (a.is_object() && a.contains("attribute") && a.contains("modifier") && a["modifier"].is_number())
                    attrs[OfficialAttr(a["attribute"].get<std::string>())] += a["modifier"].get<double>();
        if (infix && infix->contains("buff") && (*infix)["buff"].is_object() && (*infix)["buff"].contains("description") && (*infix)["buff"]["description"].is_string())
            Merge(attrs, ParseString((*infix)["buff"]["description"].get<std::string>(),
                                     infix->contains("attributes") ? &(*infix)["attributes"] : nullptr));

        if (d.contains("bonuses") && d["bonuses"].is_array())
        {
            int c = (runeCount.count(itemId) ? runeCount[itemId] + 1 : 0);
            runeCount[itemId] = c;
            if (c >= 0 && c <= 5 && c < (int)d["bonuses"].size() && d["bonuses"][c].is_string())
                Merge(attrs, ParseString(d["bonuses"][c].get<std::string>(), nullptr));
        }

        if (slot && slot->raw.is_object() && slot->raw.contains("stats") && slot->raw["stats"].is_object() && slot->raw["stats"].contains("attributes") && slot->raw["stats"]["attributes"].is_object())
            for (auto it = slot->raw["stats"]["attributes"].begin(); it != slot->raw["stats"]["attributes"].end(); ++it)
                if (it.value().is_number())
                    attrs[OfficialAttr(it.key())] += it.value().get<double>();

        if (rarity == "Ascended" && d.contains("type") && d["type"].is_string() && d["type"].get<std::string>() != "Default")
            attrs.erase("AgonyResistance");
    }
}

std::map<std::string, double> ItemAttributes::ParseCharacter(
    int level, const std::string &profession,
    const std::vector<Api::V2::ItemSlot> &equipment,
    const std::function<const AccountData::ItemMeta *(int)> &metaFor)
{
    std::map<std::string, double> a;
    std::map<int, int> runeCount;

    // Flatten: each gear piece (defense + infix + selectable stats), then its upgrades + infusions (own details).
    for (const Api::V2::ItemSlot &s : equipment)
    {
        if (s.id <= 0)
            continue;
        const AccountData::ItemMeta *gm = metaFor(s.id);
        ParseItem(a, gm, &s, runeCount, s.id, gm ? gm->rarity : std::string());
        for (int u : s.upgrades)
        {
            const AccountData::ItemMeta *um = metaFor(u);
            ParseItem(a, um, nullptr, runeCount, u, um ? um->rarity : std::string());
        }
        for (int f : s.infusions)
        {
            const AccountData::ItemMeta *fm = metaFor(f);
            ParseItem(a, fm, nullptr, runeCount, f, fm ? fm->rarity : std::string());
        }
    }

    // normalizeAttributes: the percentage attributes are accumulated in whole-percent units -> fractions.
    for (const char *p : {"ConditionDuration", "BoonDuration", "CritChance"})
        if (a.count(p))
            a[p] /= 100.0;

    // emptyAttributes: ensure every key exists so the += below is well-defined.
    for (const char *k : {"Power", "Toughness", "Vitality", "Precision", "Ferocity", "Armor", "ConditionDamage",
                          "ConditionDuration", "HealingPower", "BoonDuration", "AgonyResistance", "Concentration",
                          "Expertise", "CritChance", "Health", "CritDamage"})
        a.emplace(k, 0.0);

    // base attributes + derived secondaries.
    const int base = ScaledBaseAttributes(level);
    a["Power"] += base;
    a["Toughness"] += base;
    a["Vitality"] += base;
    a["Precision"] += base;
    a["Health"] = std::floor(ScaledBaseHealth(level, profession) + 10.0 * a["Vitality"]);
    a["Armor"] += a["Toughness"];
    a["CritChance"] += 0.04 + ((a["Precision"] - base) / ScaledAttributeBonus(level, 21)) / 100.0;
    a["CritDamage"] = 1.5 + ((a["Ferocity"]) / ScaledAttributeBonus(level, 15)) / 100.0;
    a["ConditionDuration"] += (a["Expertise"] / ScaledAttributeBonus(level, 15)) / 100.0;
    a["BoonDuration"] += (a["Concentration"] / ScaledAttributeBonus(level, 15)) / 100.0;
    return a;
}
