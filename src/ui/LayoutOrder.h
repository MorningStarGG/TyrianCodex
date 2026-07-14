#pragma once
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>
// NOTE: deliberately json-free so this can live in Config.h (included ~everywhere) without dragging the heavy
// <nlohmann/json.hpp> into every TU. Serialization is free functions in LayoutOrderJson.h (json-heavy, included
// only where a layout is actually read/written).

// -----------------------------------------------------------------------------------------------------
// UiLayout::Ordered<Slot> -- the shared layout model behind the Dashboard widgets, the Info Panel data
// texts, and the HUD buttons. ONE place for the "ordered, keyed, add/remove/reorder-able list" so the three
// surfaces stop duplicating (and diverging on) the same logic.
//
// The list holds ONLY the entries that are currently shown, in display order -- there are NO hidden "holes".
// A widget is shown iff it is in `items`. Removing ERASES it (so reorder is a plain swap of neighbours and a
// re-add lands at the bottom); a small `removed` set records the keys the user explicitly removed so that
// Reconcile() won't resurrect a default-on entry that was turned off.
//
// `Slot` must expose a public `std::string key`; everything else on it (span/zone/side/opts/...) is the
// surface's own business and only ever touched through the to/from-json hooks the surface supplies.
// -----------------------------------------------------------------------------------------------------
namespace UiLayout
{
    template <class Slot>
    struct Ordered
    {
        std::vector<Slot>     items;     // shown entries, in display order (no holes)
        std::set<std::string> removed;   // keys the user explicitly removed (kept out by Reconcile)

        // A registry entry the model needs to reconcile against: its key, whether it ships on by default, and
        // a factory that builds a fresh Slot for it (the surface sets the slot's default extras in `make`).
        struct Reg { std::string key; bool defaultOn = false; std::function<Slot()> make; };

        Slot* Find(const std::string& key)
        {
            for (Slot& s : items) if (s.key == key) return &s;
            return nullptr;
        }
        const Slot* Find(const std::string& key) const
        {
            for (const Slot& s : items) if (s.key == key) return &s;
            return nullptr;
        }
        bool Shown(const std::string& key) const { return Find(key) != nullptr; }

        // Reconcile against the live registry: apply key renames, drop entries/removed-keys whose widget no
        // longer exists, and append any default-on registry entry that is neither present nor user-removed
        // (this is what surfaces new widgets across versions, and seeds a first-run layout).
        void Reconcile(const std::vector<Reg>& reg, const std::map<std::string, std::string>& remap = {})
        {
            auto fix = [&](std::string& k) { auto it = remap.find(k); if (it != remap.end()) k = it->second; };
            for (Slot& s : items) fix(s.key);
            if (!remap.empty())
            {
                std::set<std::string> nr;
                for (std::string k : removed) { fix(k); nr.insert(k); }
                removed.swap(nr);
            }
            std::set<std::string> valid;
            for (const Reg& r : reg) valid.insert(r.key);
            // Drop entries whose widget no longer exists, AND any entry whose key is also in `removed` --
            // `removed` always wins, so a both-state (key in items AND removed) can never stay shown.
            items.erase(std::remove_if(items.begin(), items.end(),
                        [&](const Slot& s) { return valid.find(s.key) == valid.end() || removed.count(s.key) != 0; }), items.end());
            for (auto it = removed.begin(); it != removed.end(); )
                it = (valid.find(*it) == valid.end()) ? removed.erase(it) : std::next(it);
            for (const Reg& r : reg)
                if (r.defaultOn && !Shown(r.key) && removed.find(r.key) == removed.end() && r.make)
                    items.push_back(r.make());
        }

        // Remove a widget: erase it and remember the removal (so Reconcile keeps it off).
        // `key` is taken BY VALUE on purpose: callers pass a reference INTO `items` (e.g. items[bi].key), and
        // erase()/remove_if() below mutate or destroy that very element -- a const& would dangle and, for the
        // LAST element, read back EMPTY, silently recording "" in `removed` and letting the button reappear.
        void Remove(std::string key)
        {
            items.erase(std::remove_if(items.begin(), items.end(),
                        [&](const Slot& s) { return s.key == key; }), items.end());
            removed.insert(std::move(key));
        }

        // Enable a widget: clear the removal and, if it isn't already present, append a fresh slot at the
        // BOTTOM. Returns the (existing or new) slot so the caller can tweak its extras / reposition it.
        // (By value, like Remove: push_back below can reallocate `items` and dangle a reference-into-items key.)
        Slot* Enable(std::string key, const std::function<Slot()>& make)
        {
            removed.erase(key);
            if (Slot* s = Find(key)) return s;
            if (make) { items.push_back(make()); return &items.back(); }
            return nullptr;
        }

        // Move items[i] one step within its OWN group: swap with the nearest same-group neighbour in `dir`
        // (dir < 0 = earlier/up, dir > 0 = later/down). groupOf(slot) -> an int group id (region / side; a
        // surface with a single global order returns a constant). No-op at the group boundary.
        template <class GroupOf>
        void Move(int i, int dir, GroupOf groupOf)
        {
            if (i < 0 || i >= (int)items.size() || dir == 0) return;
            const int g = groupOf(items[i]);
            const int step = dir < 0 ? -1 : 1;
            for (int j = i + step; j >= 0 && j < (int)items.size(); j += step)
                if (groupOf(items[j]) == g) { std::swap(items[i], items[j]); return; }
        }

        // Move `fromKey` to just BEFORE `toKey` (before=true) or just AFTER it (false). Used by the panel
        // drag-reorder, the Info Panel zone move, and the Dashboard half-pair drop.
        void MoveRelative(const std::string& fromKey, const std::string& toKey, bool before)
        {
            if (fromKey == toKey) return;
            int fi = -1, ti = -1;
            for (int i = 0; i < (int)items.size(); ++i) { if (items[i].key == fromKey) fi = i; if (items[i].key == toKey) ti = i; }
            if (fi < 0 || ti < 0) return;
            Slot moved = items[fi];
            items.erase(items.begin() + fi);
            if (fi < ti) --ti;          // erase shifted the target left
            if (!before) ++ti;
            ti = std::min(std::max(ti, 0), (int)items.size());
            items.insert(items.begin() + ti, std::move(moved));
        }

        // Persistence lives in LayoutOrderJson.h (OrderedToJson / OrderedFromJson free functions) so this header
        // stays json-free and Config.h can hold Ordered<> members cheaply.
    };
}
