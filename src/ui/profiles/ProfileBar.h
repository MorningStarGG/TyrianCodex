#pragma once
#include <set>
#include <string>
#include <vector>
class App;

// -----------------------------------------------------------------------------------------------------
// Profiles: the shared per-character "named full preset" UI. A profile bar (selector + New / Rename /
// Duplicate / Delete + Import-from-character) that any feature can drop into its settings section. The
// feature backs it with a PerCharProfiles<Payload> (ProfileStore.h) which implements IProfileHost, so the
// UI here is decoupled from the payload type. One UI, four consumers (Dashboard, HUD, Info Panel,
// General Settings).
// -----------------------------------------------------------------------------------------------------
namespace Profiles
{
    // Type-erased view of a per-character profile collection (PerCharProfiles<Payload> implements this).
    // All indices are into the CURRENT character's profile list.
    struct IProfileHost
    {
        virtual ~IProfileHost() = default;
        virtual int         Count() const = 0;                                   // profiles for the current char
        virtual int         Active() const = 0;                                  // active index (current char)
        virtual std::string NameAt(int i) const = 0;
        virtual void        SetActive(int i) = 0;                                // fold live -> old, apply new -> live
        virtual void        New(const std::string& name) = 0;                    // clone the live state
        virtual void        Rename(int i, const std::string& name) = 0;
        virtual void        Duplicate(int i, const std::string& name) = 0;
        virtual void        Delete(int i) = 0;
        virtual std::string Suggest(const std::string& base) const = 0;          // a collision-free name
        virtual std::string CurrentChar() const = 0;                             // bound character key
        // cross-character import
        virtual std::vector<std::string> CharsWithProfiles() const = 0;          // other chars that have profiles
        virtual std::vector<std::string> ProfileNamesOf(const std::string& ch) const = 0;
        virtual void        CopyFrom(const std::string& srcChar, int srcIdx) = 0;
        virtual void        CopyAllFrom(const std::string& srcChar) = 0;
        // per-character maintenance (rename detector + Diagnostics cleanup). Default no-op so a host that is not
        // per-character keyed (e.g. the bespoke Loadouts host) need not implement them.
        virtual void        RenameChar(const std::string& /*from*/, const std::string& /*to*/) {}
        virtual void        PurgeChar(const std::string& /*name*/) {}
        virtual void        CollectCharNames(std::set<std::string>& /*out*/) const {}
    };

    // Render the management bar. `idPrefix` MUST be unique per host (drives ImGui ids + popup names).
    // `helpText` is an optional intro paragraph (nullptr = none). Sets app.settingsDirty on any change.
    void DrawProfileBar(App& app, IProfileHost& host, const char* idPrefix, const char* helpText);
}
