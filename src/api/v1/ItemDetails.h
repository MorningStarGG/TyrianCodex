#pragma once
#include "Common.h"

// GET /v1/item_details.json?item_id= (anonymous, locale): one item, fully typed. The type-specific block is a
// sub-object keyed by the lowercased `type` (weapon/armor/consumable/...). v1 is frozen, so `ItemDetails` is a
// single typed superset of all variant fields (the `type` field says which apply). Wiki: API:1/item_details.
namespace Api::V1
{
    struct ItemDetails
    {
        std::string type;            // sub-type ("Greatsword" / "Leggings" / "Food" ...)
        std::string weightClass;     // Armor
        std::string damageType;      // Weapon
        int    minPower = 0, maxPower = 0, defense = 0;
        double attributeAdjustment = 0;
        int    size = 0;             // Bag
        bool   noSellOrSort = false; // Bag
        int    charges = 0;          // Tool
        int    durationMs = 0;       // Consumable
        std::string description;     // Consumable
        std::string unlockType;      // Consumable (Dye / CraftingRecipe / ...)
        int    colorId = 0;          // Consumable (dye)
        int    recipeId = 0;         // Consumable (recipe unlock)
        int    suffixItemId = 0;
        std::string secondarySuffixItemId;   // string in v1
        std::vector<int> statChoices;
        int    minipetId = 0;        // MiniPet
        nlohmann::json infusionSlots;        // [{flags, item_id}]
        nlohmann::json infixUpgrade;         // {id, attributes, buff}
        nlohmann::json raw;
    };

    struct Item
    {
        int itemId = 0; std::string name; std::string type; int level = 0; std::string rarity; int vendorValue = 0;
        int iconFileId = 0; std::string iconFileSignature; int defaultSkin = 0;
        std::vector<std::string> flags; std::vector<std::string> gameTypes; std::vector<std::string> restrictions;
        ItemDetails details; nlohmann::json raw;
    };

    inline ItemDetails ParseItemDetails(const nlohmann::json& d)
    {
        ItemDetails x;
        if (!d.is_object()) return x;
        x.type = Json::Str(d, "type"); x.weightClass = Json::Str(d, "weight_class"); x.damageType = Json::Str(d, "damage_type");
        x.minPower = IntS(d, "min_power"); x.maxPower = IntS(d, "max_power"); x.defense = IntS(d, "defense");
        x.attributeAdjustment = DblS(d, "attribute_adjustment"); x.size = IntS(d, "size"); x.noSellOrSort = Json::Bool(d, "no_sell_or_sort");
        x.charges = IntS(d, "charges"); x.durationMs = IntS(d, "duration_ms"); x.description = Json::Str(d, "description");
        x.unlockType = Json::Str(d, "unlock_type"); x.colorId = IntS(d, "color_id"); x.recipeId = IntS(d, "recipe_id");
        x.suffixItemId = IntS(d, "suffix_item_id"); x.secondarySuffixItemId = Json::Str(d, "secondary_suffix_item_id");
        for (const auto& s : Json::Node(d, "stat_choices")) if (s.is_number_integer()) x.statChoices.push_back(s.get<int>());
        x.minipetId = IntS(d, "minipet_id"); x.infusionSlots = Json::Node(d, "infusion_slots"); x.infixUpgrade = Json::Node(d, "infix_upgrade");
        x.raw = d;
        return x;
    }
    inline Item ParseItem(const nlohmann::json& j)
    {
        Item it;
        it.itemId = IntS(j, "item_id"); it.name = Json::Str(j, "name"); it.type = Json::Str(j, "type");
        it.level = IntS(j, "level"); it.rarity = Json::Str(j, "rarity"); it.vendorValue = IntS(j, "vendor_value");
        it.iconFileId = IntS(j, "icon_file_id"); it.iconFileSignature = Json::Str(j, "icon_file_signature"); it.defaultSkin = IntS(j, "default_skin");
        it.flags = Json::StrArray(j, "flags"); it.gameTypes = Json::StrArray(j, "game_types"); it.restrictions = Json::StrArray(j, "restrictions");
        if (!it.type.empty()) it.details = ParseItemDetails(Json::Node(j, Lower(it.type).c_str()));
        it.raw = j;
        return it;
    }

    class ItemDetailsEndpoint
    {
    public:
        explicit ItemDetailsEndpoint(Connection* c) : _c(c) {}
        void ById(int itemId, std::function<void(Result<Item>)> cb) const { FetchQ<Item>(_c, "/v1/item_details.json", "item_id", std::to_string(itemId), kV1StaticTtl, &ParseItem, std::move(cb)); }
    private:
        Connection* _c;
    };
}
