#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Json.h"

// Shared cross-root model types used by more than one v2 root endpoint. This is NOT an endpoint module - the
// one-header-per-root rule governs endpoints; small models reused across roots (the id-only catalog row, the
// item-instance slot) live here so each root header stays focused on its own endpoint.
namespace Api::V2
{
    // The id-only catalog row shape: /v2/mapchests, /v2/worldbosses, /v2/dailycrafting each return a bare
    // array of id strings (the "completed today" style catalogs). One model, three endpoints (each its own file).
    struct NamedList { std::string id; nlohmann::json raw; };

    inline NamedList ParseNamedList(const nlohmann::json& j)
    {
        NamedList n;
        if (j.is_string()) n.id = j.get<std::string>();   // these catalogs are bare id strings
        else               n.id = Json::Str(j, "id");
        n.raw = j;
        return n;
    }

    // A generic {id, count} row, reused by account unlock counts (legendaryarmory, homestead/decorations) and
    // guild storage.
    struct CountedId { int id = 0; int count = 0; nlohmann::json raw; };

    inline CountedId ParseCountedId(const nlohmann::json& j)
    {
        CountedId c; c.id = Json::Int(j, "id"); c.count = Json::Int(j, "count"); c.raw = j; return c;
    }

    // An item INSTANCE in a character's equipment / inventory or an account bank / shared-inventory slot. The
    // stable common fields are typed; the type-varying extras (selected `stats`, `upgrade_slot_indices`, ...)
    // stay in `raw` - the documented Option-1 tail for a live tagged-union payload, so new content never drops.
    // An empty slot (JSON `null`) parses to a default ItemSlot (id == 0), keeping array indices aligned.
    struct ItemSlot
    {
        int              id = 0;          // item id (0 == empty slot)
        int              count = 0;
        int              charges = 0;     // for stackable consumables / salvage kits
        int              skin = 0;        // applied skin id (0 == none)
        std::string      slot;            // equipment slot name (e.g. "Helm"); empty for bag/bank slots
        std::string      binding;         // "Character" / "Account" / "" (none)
        std::string      boundTo;         // character name, when binding == "Character"
        std::vector<int> upgrades;        // slotted upgrade-component item ids
        std::vector<int> infusions;       // slotted infusion item ids
        std::vector<int> dyes;            // applied dye ids (equipped armor/back/weapon)
        nlohmann::json   raw;             // stats{}, upgrade_slot_indices, per-type extras
    };

    inline ItemSlot ParseItemSlot(const nlohmann::json& j)
    {
        ItemSlot s;
        if (!j.is_object()) return s;     // null / empty slot -> default (id 0)
        s.id       = Json::Int(j, "id");
        s.count    = Json::Int(j, "count");
        s.charges  = Json::Int(j, "charges");
        s.skin     = Json::Int(j, "skin");
        s.slot     = Json::Str(j, "slot");
        s.binding  = Json::Str(j, "binding");
        s.boundTo  = Json::Str(j, "bound_to");
        s.upgrades = Json::IntArray(j, "upgrades");
        s.infusions= Json::IntArray(j, "infusions");
        s.dyes     = Json::IntArray(j, "dyes");
        s.raw      = j;
        return s;
    }
}
