#pragma once
#include "LayoutOrder.h"
#include <nlohmann/json.hpp>

// -----------------------------------------------------------------------------------------------------
// JSON serialization for UiLayout::Ordered<Slot>. Kept OUT of LayoutOrder.h (and therefore out of Config.h)
// so the heavy <nlohmann/json.hpp> is only pulled into the few TUs that actually read/write a layout.
//
// The per-slot callable is a DEDUCED template parameter (not std::function): passing a bare lambda to a
// std::function<> parameter can't deduce Slot under MSVC /permissive- C++17. Callers always supply a callable,
// so there is no null-check.
// -----------------------------------------------------------------------------------------------------
namespace UiLayout
{
    // Stores { "items":[{key, ...extra}], "removed":[keys] }. slotToJson(const Slot&, nlohmann::json&) writes the extras.
    template <class Slot, class SlotToJson>
    nlohmann::json OrderedToJson(const Ordered<Slot>& o, SlotToJson slotToJson)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const Slot& s : o.items) { nlohmann::json e = { {"key", s.key} }; slotToJson(s, e); arr.push_back(std::move(e)); }
        nlohmann::json rm = nlohmann::json::array();
        for (const std::string& k : o.removed) rm.push_back(k);
        return { {"items", std::move(arr)}, {"removed", std::move(rm)} };
    }

    // Reads the above AND the legacy bare-array shape ([{ key, show, ...extra }]), folding any legacy show:false
    // entry into `removed` -- so existing settings migrate with no entries lost or resurrected.
    // slotFromJson(const nlohmann::json&) -> Slot builds one slot from its json object.
    template <class Slot, class SlotFromJson>
    void OrderedFromJson(Ordered<Slot>& o, const nlohmann::json& j, SlotFromJson slotFromJson)
    {
        o.items.clear(); o.removed.clear();
        const nlohmann::json* arr = nullptr;
        if (j.is_object() && j.contains("items") && j["items"].is_array())
        {
            arr = &j["items"];
            if (j.contains("removed") && j["removed"].is_array())
                for (const auto& k : j["removed"]) if (k.is_string()) o.removed.insert(k.get<std::string>());
        }
        else if (j.is_array()) arr = &j;   // legacy: bare [{key, show, ...}]
        if (!arr) return;
        for (const auto& e : *arr)
        {
            if (!e.is_object() || !e.contains("key") || !e["key"].is_string()) continue;
            if (!e.value("show", true)) { o.removed.insert(e["key"].get<std::string>()); continue; }   // legacy hidden -> removed
            o.items.push_back(slotFromJson(e));
        }
        // `removed` WINS: a key the user removed must never be shown, even if a stale/corrupt save also listed it
        // under items. Self-heals any items-AND-removed both-state on load (HUD/Dashboard/Info Panel).
        if (!o.removed.empty())
            o.items.erase(std::remove_if(o.items.begin(), o.items.end(),
                          [&](const Slot& s) { return o.removed.find(s.key) != o.removed.end(); }), o.items.end());
    }
}
