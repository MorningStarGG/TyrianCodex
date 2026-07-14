#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// CraftCart: the persistent, account-wide crafting cart. Multiple NAMED projects, each a list of (output item,
// quantity) the player wants to craft, plus a per-project "got" set (materials checked off as acquired). Modeled
// on guide/Progress (JSON keyed lists, best-effort Save() on every mutation, a Version() change token). One file
// at the addon root -- craft_cart.json -- so the cart is shared across characters. Pure store: it does NOT know
// about prices/recipes (CraftCartSummary layers that on top).
// -----------------------------------------------------------------------------------------------------
namespace CraftCart
{
    using Item = std::pair<int, long long>;   // (itemId, qty)

    void     Init(const std::string& path);   // load craft_cart.json (seeds a default project if absent/empty)
    void     Shutdown();
    uint64_t Version();                        // bumps on every mutation -- UI change token

    // -- projects --
    std::vector<std::string> Names();          // project names, in creation order
    std::string Active();                      // active project name (Init guarantees >= 1 project)
    void        SetActive(const std::string& name);
    std::string New(const std::string& name);  // create (auto-name if blank/duplicate); makes it active; returns final name
    bool        Rename(const std::string& oldName, const std::string& newName);
    void        Delete(const std::string& name);   // always keeps >= 1 project (recreates a default if the last is removed)

    // -- items of a project ("" = the active project) --
    std::vector<Item> Items(const std::string& project = "");
    int  Count(const std::string& project = "");     // number of distinct items
    void Add(const std::string& project, int itemId, long long qty);   // merges duplicates (qty <= 0 ignored)
    void SetQty(const std::string& project, int itemId, long long qty); // qty <= 0 removes the item
    void Remove(const std::string& project, int itemId);
    void Clear(const std::string& project);          // empties items + got

    // -- per-project material check-off (keyed by item id) --
    bool IsGot(const std::string& project, int itemId);
    void SetGot(const std::string& project, int itemId, bool on);
}
