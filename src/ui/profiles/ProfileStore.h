#pragma once
#include "ProfileBar.h"
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// -----------------------------------------------------------------------------------------------------
// PerCharProfiles<Payload>: a generic, per-character collection of named "full preset" payloads with an
// active reference, optional shared/global profiles, cross-character import, and JSON persistence. The OWNER supplies capture/apply (live<->
// payload) and payload<->json callbacks via Configure(); this template owns ONLY the per-character book-
// keeping, so the owner's render/edit code (which may read globals + config) is untouched -- profiles stay
// a save/switch layer over the live state, exactly like the original Dashboard design.
//
// Character key = the character name (same scheme as ProgressStore; "default" until MumbleLink resolves).
// Shared profiles live in a separate reserved global collection and can be selected by any character.
// A reserved "__template__" bucket holds the BASELINE that seeds any character with no profiles yet (set
// during migration from a pre-per-character settings file, so existing setups are preserved and new alts
// start from the same baseline).
// -----------------------------------------------------------------------------------------------------
namespace Profiles
{
    template <class Payload>
    class PerCharProfiles : public IProfileHost
    {
    public:
        struct Named { std::string name; Payload data; };

        using CaptureFn  = std::function<Payload()>;                    // read live  -> payload
        using ApplyFn    = std::function<void(const Payload&)>;         // write payload -> live
        using ToJsonFn   = std::function<nlohmann::json(const Payload&)>;
        using FromJsonFn = std::function<Payload(const nlohmann::json&)>;
        using DefaultFn  = std::function<Payload()>;                    // a registry-default payload

        void Configure(CaptureFn cap, ApplyFn app, ToJsonFn toj, FromJsonFn fromj, DefaultFn def)
        {
            m_capture = std::move(cap); m_apply = std::move(app);
            m_toJson = std::move(toj); m_fromJson = std::move(fromj); m_default = std::move(def);
            m_configured = true;
        }
        bool Configured() const { return m_configured; }

        // ---- character binding (call every frame; cheap no-op when unchanged) ----
        // Fold the live state into the OUTGOING char's active profile, then make `charKey` current (seeding
        // from the template / a default if it has none) and apply its active profile into live.
        void BindCharacter(const std::string& charKey)
        {
            if (!m_curKey.empty() && charKey == m_curKey) return;
            if (!m_curKey.empty()) SyncLiveToActive();
            m_curKey = charKey.empty() ? std::string("default") : charKey;
            EnsureChar(m_curKey);
            ApplyActive();
        }
        std::string CurrentChar() const override { return m_curKey; }

        // ---- current-character accessors (const, no insertion) ----
        int Count() const override { return LocalCount(Find(m_curKey)) + GlobalCount(); }
        int Active() const override
        {
            const CharSet* cs = Find(m_curKey);
            if (!cs) return 0;
            const int localN = LocalCount(cs);
            const int globalN = GlobalCount();
            if (cs->activeGlobal && globalN > 0)
                return localN + std::min(std::max(cs->activeGlobalIndex, 0), globalN - 1);
            return std::min(std::max(cs->active, 0), std::max(0, localN - 1));
        }
        std::string NameAt(int i) const override
        {
            bool global = false;
            const Named* n = Visible(i, &global);
            (void)global;
            return n ? n->name : std::string();
        }

        // Fold live -> the current char's active profile (call before SAVE / before switching away).
        void SyncLiveToActive()
        {
            if (!m_capture || m_curKey.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
            if (cs.activeGlobal)
            {
                CharSet& g = Global();
                if (cs.activeGlobalIndex >= 0 && cs.activeGlobalIndex < (int)g.profiles.size())
                    g.profiles[cs.activeGlobalIndex].data = m_capture();
            }
            else if (cs.active >= 0 && cs.active < (int)cs.profiles.size())
            {
                cs.profiles[cs.active].data = m_capture();
            }
        }

        // ---- CRUD on the current character ----
        void SetActive(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            const int localN = (int)cs.profiles.size();
            const int globalN = GlobalCount();
            if (i < 0 || i >= localN + globalN || i == Active()) return;
            SyncLiveToActive();
            if (i < localN)
            {
                cs.activeGlobal = false;
                cs.active = i;
            }
            else
            {
                cs.activeGlobal = true;
                cs.activeGlobalIndex = i - localN;
            }
            ApplyActive();
        }
        void New(const std::string& name) override   // clone the live state into a new active profile
        {
            CharSet& cs = EnsureChar(m_curKey);
            SyncLiveToActive();
            Named n; n.name = UniqueName(name, -1);
            n.data = m_capture ? m_capture() : (m_default ? m_default() : Payload{});
            cs.profiles.push_back(std::move(n));
            cs.activeGlobal = false;
            cs.active = (int)cs.profiles.size() - 1;   // live already equals it -> no ApplyActive
        }
        void Rename(int i, const std::string& name) override
        {
            if (name.empty()) return;
            bool global = false;
            Named* n = VisibleMutable(i, &global);
            if (!n) return;
            n->name = global ? UniqueGlobalName(name, GlobalIndexFromVisible(i)) : UniqueName(name, i);
        }
        void Duplicate(int i, const std::string& name) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src) return;
            if (i == Active()) SyncLiveToActive();
            Named n = *src;
            if (global)
            {
                CharSet& g = Global();
                n.name = UniqueGlobalName(name, -1);
                g.profiles.push_back(std::move(n));
                cs.activeGlobal = true;
                cs.activeGlobalIndex = (int)g.profiles.size() - 1;
            }
            else
            {
                n.name = UniqueName(name, -1);
                cs.profiles.push_back(std::move(n));
                cs.activeGlobal = false;
                cs.active = (int)cs.profiles.size() - 1;
            }
            ApplyActive();
        }
        void Delete(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            bool global = false;
            Named* target = VisibleMutable(i, &global);
            if (!target) return;
            const bool wasActive = (i == Active());
            if (global)
            {
                const int gi = GlobalIndexFromVisible(i);
                CharSet& g = Global();
                if (gi < 0 || gi >= (int)g.profiles.size()) return;
                g.profiles.erase(g.profiles.begin() + gi);
                for (auto& kv : m_byChar)
                {
                    if (kv.first == kTemplateKey || kv.first == kGlobalKey) continue;
                    CharSet& other = kv.second;
                    if (!other.activeGlobal) continue;
                    if (other.activeGlobalIndex == gi)
                    {
                        other.activeGlobal = false;
                        other.active = std::min(std::max(other.active, 0), std::max(0, (int)other.profiles.size() - 1));
                    }
                    else if (other.activeGlobalIndex > gi)
                    {
                        --other.activeGlobalIndex;
                    }
                }
                if (wasActive) ApplyActive();
            }
            else
            {
                cs.profiles.erase(cs.profiles.begin() + i);
                if (cs.profiles.empty())
                {
                    Named d; d.name = "Default"; d.data = m_default ? m_default() : Payload{};
                    cs.profiles.push_back(std::move(d));
                    cs.active = 0;
                    cs.activeGlobal = false;
                    ApplyActive();
                }
                else
                {
                    if (cs.active > i)      --cs.active;
                    else if (wasActive)     cs.active = std::min(i, (int)cs.profiles.size() - 1);
                    cs.active = std::min(std::max(cs.active, 0), (int)cs.profiles.size() - 1);
                    if (wasActive) ApplyActive();
                }
            }
        }
        std::string Suggest(const std::string& base) const override { return UniqueNameConst(m_curKey, base, -1); }

        // ---- cross-character import ----
        std::vector<std::string> CharsWithProfiles() const override
        {
            std::vector<std::string> out;
            for (const auto& kv : m_byChar)
                if (kv.first != m_curKey && kv.first != kTemplateKey && kv.first != kGlobalKey && kv.first != kPreLoginKey && !kv.second.profiles.empty())
                    out.push_back(kv.first);
            std::sort(out.begin(), out.end());
            return out;
        }
        std::vector<std::string> ProfileNamesOf(const std::string& ch) const override
        {
            std::vector<std::string> out; const CharSet* cs = Find(ch);
            if (cs) for (const Named& n : cs->profiles) out.push_back(n.name);
            return out;
        }
        void CopyFrom(const std::string& srcChar, int srcIdx) override
        {
            const CharSet* src = Find(srcChar);
            if (!src || srcIdx < 0 || srcIdx >= (int)src->profiles.size()) return;
            CharSet& cs = EnsureChar(m_curKey);
            SyncLiveToActive();
            Named n = src->profiles[srcIdx]; n.name = UniqueName(n.name, -1);
            cs.profiles.push_back(std::move(n));
            cs.activeGlobal = false;
            cs.active = (int)cs.profiles.size() - 1; ApplyActive();
        }
        void CopyAllFrom(const std::string& srcChar) override
        {
            const CharSet* src = Find(srcChar);
            if (!src || src->profiles.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
            SyncLiveToActive();
            for (const Named& s : src->profiles) { Named n = s; n.name = UniqueName(n.name, -1); cs.profiles.push_back(std::move(n)); }
            cs.activeGlobal = false;
            cs.active = (int)cs.profiles.size() - 1; ApplyActive();
        }

        bool IsGlobalAt(int i) const override
        {
            bool global = false;
            (void)Visible(i, &global);
            return global;
        }
        ProfileRef RefAt(int i) const override
        {
            bool global = false;
            const Named* n = Visible(i, &global);
            return ProfileRef{ global ? Scope::Global : Scope::Character, n ? n->name : std::string() };
        }
        int FindRef(const ProfileRef& ref) const override
        {
            if (ref.name.empty()) return -1;
            if (ref.scope == Scope::Global)
            {
                const CharSet* g = GlobalC();
                if (!g) return -1;
                for (int i = 0; i < (int)g->profiles.size(); ++i)
                    if (g->profiles[i].name == ref.name)
                        return LocalCount(Find(m_curKey)) + i;
                return -1;
            }
            const CharSet* cs = Find(m_curKey);
            if (!cs) return -1;
            for (int i = 0; i < (int)cs->profiles.size(); ++i)
                if (cs->profiles[i].name == ref.name)
                    return i;
            return -1;
        }
        bool CanMakeGlobal(int i) const override
        {
            bool global = false;
            return Visible(i, &global) && !global;
        }
        bool CanCopyToCharacter(int i) const override
        {
            bool global = false;
            return Visible(i, &global) && global;
        }
        void MakeGlobal(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src || global) return;
            if (i == Active()) SyncLiveToActive();
            CharSet& g = Global();
            Named n = *src;
            n.name = UniqueGlobalName(n.name, -1);
            g.profiles.push_back(std::move(n));
            cs.activeGlobal = true;
            cs.activeGlobalIndex = (int)g.profiles.size() - 1;
            ApplyActive();
        }
        void CopyToCharacter(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src || !global) return;
            if (i == Active()) SyncLiveToActive();
            Named n = *src;
            n.name = UniqueName(n.name, -1);
            cs.profiles.push_back(std::move(n));
            cs.activeGlobal = false;
            cs.active = (int)cs.profiles.size() - 1;
            ApplyActive();
        }

        // ---- per-character maintenance (rename detector + Diagnostics cleanup) ----
        // REPLACE: the renamed character IS the same character, so its profiles overwrite any (just-seeded default)
        // profiles under the new name. If the new name is the active character, re-apply so live picks up its config.
        void RenameChar(const std::string& from, const std::string& to) override
        {
            if (from.empty() || to.empty() || from == to) return;
            auto itF = m_byChar.find(from);
            if (itF == m_byChar.end()) return;
            m_byChar[to] = std::move(itF->second);
            m_byChar.erase(itF);
            if (m_curKey == from) m_curKey = to;
            if (m_curKey == to) ApplyActive();
        }
        void PurgeChar(const std::string& name) override
        {
            if (name == m_curKey || name == kTemplateKey || name == kGlobalKey) return;   // never purge the active char, baseline, or shared profiles
            m_byChar.erase(name);
        }
        void CollectCharNames(std::set<std::string>& out) const override
        {
            for (const auto& kv : m_byChar)
                if (kv.first != kTemplateKey && kv.first != kGlobalKey && kv.first != kPreLoginKey) out.insert(kv.first);
        }

        // ---- migration / persistence ----
        // Seed the baseline cloned into any character that has no profiles yet (preserves a legacy setup).
        void SeedTemplate(std::vector<Named> profiles, int active = 0)
        {
            if (profiles.empty()) return;
            CharSet& t = m_byChar[kTemplateKey];
            t.profiles = std::move(profiles);
            t.active = std::min(std::max(active, 0), (int)t.profiles.size() - 1);
            t.activeGlobal = false;
            t.activeGlobalIndex = 0;
        }
        bool HasAnyChar() const
        {
            for (const auto& kv : m_byChar)
                if (kv.first != kTemplateKey && kv.first != kGlobalKey && !kv.second.profiles.empty()) return true;
            return false;
        }

        void Serialize(nlohmann::json& j, const char* key)
        {
            SyncLiveToActive();
            nlohmann::json root = nlohmann::json::object();
            for (const auto& kv : m_byChar)
            {
                if (kv.first == kGlobalKey)
                    continue;
                nlohmann::json arr = nlohmann::json::array();
                for (const Named& n : kv.second.profiles)
                    arr.push_back({ {"name", n.name}, {"data", m_toJson ? m_toJson(n.data) : nlohmann::json::object()} });
                nlohmann::json o = { {"active", kv.second.active}, {"profiles", arr} };
                if (kv.second.activeGlobal)
                {
                    const CharSet* g = GlobalC();
                    if (g && kv.second.activeGlobalIndex >= 0 && kv.second.activeGlobalIndex < (int)g->profiles.size())
                    {
                        o["activeScope"] = "global";
                        o["activeName"] = g->profiles[kv.second.activeGlobalIndex].name;
                    }
                }
                else if (kv.second.active >= 0 && kv.second.active < (int)kv.second.profiles.size())
                {
                    o["activeScope"] = "character";
                    o["activeName"] = kv.second.profiles[kv.second.active].name;
                }
                root[kv.first] = std::move(o);
            }
            j[key] = root;

            nlohmann::json global = nlohmann::json::array();
            if (const CharSet* g = GlobalC())
                for (const Named& n : g->profiles)
                    global.push_back({ {"name", n.name}, {"data", m_toJson ? m_toJson(n.data) : nlohmann::json::object()} });
            j[GlobalStoreKey(key)] = { {"profiles", std::move(global)} };
        }
        // Returns true if the key existed (so the owner knows whether to run legacy migration).
        bool Deserialize(const nlohmann::json& j, const char* key)
        {
            m_byChar.clear();
            auto it = j.find(key);
            if (it == j.end() || !it->is_object()) return false;

            auto readProfiles = [&](const nlohmann::json& arr, const char* fallbackName) {
                std::vector<Named> out;
                if (!arr.is_array()) return out;
                for (const auto& e : arr)
                {
                    Named n; n.name = e.value("name", std::string(fallbackName ? fallbackName : "Profile"));
                    if (e.contains("data") && m_fromJson) n.data = m_fromJson(e["data"]);
                    out.push_back(std::move(n));
                }
                return out;
            };

            if (auto git = j.find(GlobalStoreKey(key)); git != j.end())
            {
                const nlohmann::json& go = *git;
                std::vector<Named> profiles = go.is_array()
                    ? readProfiles(go, "Shared profile")
                    : (go.is_object() ? readProfiles(go.value("profiles", nlohmann::json::array()), "Shared profile") : std::vector<Named>{});
                if (!profiles.empty())
                    m_byChar[kGlobalKey].profiles = std::move(profiles);
            }

            struct PendingActive { std::string key; std::string scope; std::string name; };
            std::vector<PendingActive> pending;
            for (auto el = it->begin(); el != it->end(); ++el)
            {
                const nlohmann::json& o = el.value();
                if (!o.is_object()) continue;
                CharSet cs; cs.active = o.value("active", 0);
                cs.profiles = readProfiles(o.value("profiles", nlohmann::json::array()), "Profile");
                if (!cs.profiles.empty())
                {
                    cs.active = std::min(std::max(cs.active, 0), (int)cs.profiles.size() - 1);
                    const std::string keyName = el.key();
                    if (keyName == kGlobalKey)
                    {
                        if (m_byChar[kGlobalKey].profiles.empty())
                            m_byChar[kGlobalKey].profiles = std::move(cs.profiles); // legacy reserved-bucket support
                    }
                    else
                    {
                        if (o.contains("activeScope") && o["activeScope"].is_string() &&
                            o.contains("activeName") && o["activeName"].is_string())
                            pending.push_back({ keyName, o["activeScope"].get<std::string>(), o["activeName"].get<std::string>() });
                        m_byChar[keyName] = std::move(cs);
                    }
                }
            }
            for (const PendingActive& p : pending)
            {
                CharSet& cs = m_byChar[p.key];
                if (p.scope == "global")
                {
                    if (const CharSet* g = GlobalC())
                        for (int i = 0; i < (int)g->profiles.size(); ++i)
                            if (g->profiles[i].name == p.name)
                            {
                                cs.activeGlobal = true;
                                cs.activeGlobalIndex = i;
                                break;
                            }
                }
                else
                {
                    for (int i = 0; i < (int)cs.profiles.size(); ++i)
                        if (cs.profiles[i].name == p.name)
                        {
                            cs.activeGlobal = false;
                            cs.active = i;
                            break;
                        }
                }
            }
            return true;
        }

    private:
        static constexpr const char* kTemplateKey = "__template__";
        static constexpr const char* kGlobalKey = "__global__";
        static constexpr const char* kPreLoginKey = "default";   // pre-login bucket (currentChar before MumbleLink resolves) -- not a real character, never an import source
        struct CharSet
        {
            std::vector<Named> profiles;
            int active = 0;                 // active character-local profile index
            bool activeGlobal = false;      // current character is using a shared profile
            int activeGlobalIndex = 0;      // active shared profile index when activeGlobal is true
        };

        static std::string GlobalStoreKey(const char* key)
        {
            std::string out = key ? key : "";
            constexpr const char* suffix = "ByChar";
            constexpr size_t suffixLen = 6;
            if (out.size() >= suffixLen && out.compare(out.size() - suffixLen, suffixLen, suffix) == 0)
                out.replace(out.size() - suffixLen, suffixLen, "Global");
            else
                out += "Global";
            return out;
        }

        const CharSet* Find(const std::string& key) const
        {
            auto it = m_byChar.find(key);
            return it == m_byChar.end() ? nullptr : &it->second;
        }
        const CharSet* GlobalC() const { return Find(kGlobalKey); }
        CharSet& Global() { return m_byChar[kGlobalKey]; }
        int LocalCount(const CharSet* cs) const { return cs ? (int)cs->profiles.size() : 0; }
        int GlobalCount() const { return LocalCount(GlobalC()); }
        int GlobalIndexFromVisible(int i) const
        {
            const int gi = i - LocalCount(Find(m_curKey));
            return (gi >= 0 && gi < GlobalCount()) ? gi : -1;
        }
        const Named* Visible(int i, bool* global) const
        {
            if (global) *global = false;
            const CharSet* cs = Find(m_curKey);
            const int localN = LocalCount(cs);
            if (i >= 0 && i < localN)
                return &cs->profiles[i];
            const int gi = i - localN;
            const CharSet* g = GlobalC();
            if (g && gi >= 0 && gi < (int)g->profiles.size())
            {
                if (global) *global = true;
                return &g->profiles[gi];
            }
            return nullptr;
        }
        Named* VisibleMutable(int i, bool* global)
        {
            if (global) *global = false;
            CharSet& cs = EnsureChar(m_curKey);
            const int localN = (int)cs.profiles.size();
            if (i >= 0 && i < localN)
                return &cs.profiles[i];
            const int gi = i - localN;
            CharSet& g = Global();
            if (gi >= 0 && gi < (int)g.profiles.size())
            {
                if (global) *global = true;
                return &g.profiles[gi];
            }
            return nullptr;
        }
        CharSet& EnsureChar(const std::string& key)
        {
            CharSet& cs = m_byChar[key];
            if (cs.profiles.empty())
            {
                auto tmpl = m_byChar.find(kTemplateKey);
                if (tmpl != m_byChar.end() && !tmpl->second.profiles.empty())
                {
                    cs.profiles = tmpl->second.profiles;
                    cs.active = std::min(std::max(tmpl->second.active, 0), (int)cs.profiles.size() - 1);
                    cs.activeGlobal = false;
                    cs.activeGlobalIndex = 0;
                }
                else
                {
                    Named d; d.name = "Default"; d.data = m_default ? m_default() : Payload{};
                    cs.profiles.push_back(std::move(d)); cs.active = 0; cs.activeGlobal = false; cs.activeGlobalIndex = 0;
                }
            }
            return cs;
        }
        void ApplyActive()
        {
            if (!m_apply || m_curKey.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
            if (cs.activeGlobal)
            {
                CharSet& g = Global();
                if (cs.activeGlobalIndex >= 0 && cs.activeGlobalIndex < (int)g.profiles.size())
                {
                    m_apply(g.profiles[cs.activeGlobalIndex].data);
                    return;
                }
                cs.activeGlobal = false;
            }
            if (cs.active >= 0 && cs.active < (int)cs.profiles.size()) m_apply(cs.profiles[cs.active].data);
        }
        std::string UniqueNameConst(const std::string& charKey, const std::string& base, int ignoreIdx) const
        {
            const CharSet* cs = Find(charKey);
            std::string root = base.empty() ? "Profile" : base;
            auto taken = [&](const std::string& n) {
                if (!cs) return false;
                for (int i = 0; i < (int)cs->profiles.size(); ++i)
                    if (i != ignoreIdx && cs->profiles[i].name == n) return true;
                return false;
            };
            if (!taken(root)) return root;
            for (int n = 2; ; ++n) { std::string c = root + " " + std::to_string(n); if (!taken(c)) return c; }
        }
        std::string UniqueName(const std::string& base, int ignoreIdx) { return UniqueNameConst(m_curKey, base, ignoreIdx); }
        std::string UniqueGlobalName(const std::string& base, int ignoreIdx) const { return UniqueNameConst(kGlobalKey, base, ignoreIdx); }

        std::map<std::string, CharSet> m_byChar;
        std::string m_curKey;
        bool        m_configured = false;
        CaptureFn   m_capture; ApplyFn m_apply; ToJsonFn m_toJson; FromJsonFn m_fromJson; DefaultFn m_default;
    };
}
