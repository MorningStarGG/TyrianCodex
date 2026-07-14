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
// active index, cross-character import, and JSON persistence. The OWNER supplies capture/apply (live<->
// payload) and payload<->json callbacks via Configure(); this template owns ONLY the per-character book-
// keeping, so the owner's render/edit code (which may read globals + config) is untouched -- profiles stay
// a save/switch layer over the live state, exactly like the original Dashboard design.
//
// Character key = the character name (same scheme as ProgressStore; "default" until MumbleLink resolves).
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
        int Count() const override { const CharSet* cs = Find(m_curKey); return cs ? (int)cs->profiles.size() : 0; }
        int Active() const override { const CharSet* cs = Find(m_curKey); return cs ? cs->active : 0; }
        std::string NameAt(int i) const override
        {
            const CharSet* cs = Find(m_curKey);
            if (!cs || i < 0 || i >= (int)cs->profiles.size()) return std::string();
            return cs->profiles[i].name;
        }

        // Fold live -> the current char's active profile (call before SAVE / before switching away).
        void SyncLiveToActive()
        {
            if (!m_capture || m_curKey.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
            if (cs.active >= 0 && cs.active < (int)cs.profiles.size()) cs.profiles[cs.active].data = m_capture();
        }

        // ---- CRUD on the current character ----
        void SetActive(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            if (i < 0 || i >= (int)cs.profiles.size() || i == cs.active) return;
            SyncLiveToActive();
            cs.active = i;
            ApplyActive();
        }
        void New(const std::string& name) override   // clone the live state into a new active profile
        {
            CharSet& cs = EnsureChar(m_curKey);
            SyncLiveToActive();
            Named n; n.name = UniqueName(name, -1);
            n.data = m_capture ? m_capture() : (m_default ? m_default() : Payload{});
            cs.profiles.push_back(std::move(n));
            cs.active = (int)cs.profiles.size() - 1;   // live already equals it -> no ApplyActive
        }
        void Rename(int i, const std::string& name) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            if (i < 0 || i >= (int)cs.profiles.size() || name.empty()) return;
            cs.profiles[i].name = UniqueName(name, i);
        }
        void Duplicate(int i, const std::string& name) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            if (i < 0 || i >= (int)cs.profiles.size()) return;
            if (i == cs.active) SyncLiveToActive();
            Named n = cs.profiles[i]; n.name = UniqueName(name, -1);
            cs.profiles.push_back(std::move(n));
            cs.active = (int)cs.profiles.size() - 1;
            ApplyActive();
        }
        void Delete(int i) override
        {
            CharSet& cs = EnsureChar(m_curKey);
            if (i < 0 || i >= (int)cs.profiles.size()) return;
            const bool wasActive = (i == cs.active);
            cs.profiles.erase(cs.profiles.begin() + i);
            if (cs.profiles.empty())
            {
                Named d; d.name = "Default"; d.data = m_default ? m_default() : Payload{};
                cs.profiles.push_back(std::move(d)); cs.active = 0; ApplyActive();
            }
            else
            {
                if (cs.active > i)      --cs.active;
                else if (wasActive)     cs.active = std::min(i, (int)cs.profiles.size() - 1);
                cs.active = std::min(std::max(cs.active, 0), (int)cs.profiles.size() - 1);
                if (wasActive) ApplyActive();
            }
        }
        std::string Suggest(const std::string& base) const override { return UniqueNameConst(m_curKey, base, -1); }

        // ---- cross-character import ----
        std::vector<std::string> CharsWithProfiles() const override
        {
            std::vector<std::string> out;
            for (const auto& kv : m_byChar)
                if (kv.first != m_curKey && kv.first != kTemplateKey && kv.first != kPreLoginKey && !kv.second.profiles.empty())
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
            cs.active = (int)cs.profiles.size() - 1; ApplyActive();
        }
        void CopyAllFrom(const std::string& srcChar) override
        {
            const CharSet* src = Find(srcChar);
            if (!src || src->profiles.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
            SyncLiveToActive();
            for (const Named& s : src->profiles) { Named n = s; n.name = UniqueName(n.name, -1); cs.profiles.push_back(std::move(n)); }
            cs.active = (int)cs.profiles.size() - 1; ApplyActive();
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
            if (name == m_curKey || name == kTemplateKey) return;   // never purge the active char or the baseline
            m_byChar.erase(name);
        }
        void CollectCharNames(std::set<std::string>& out) const override
        {
            for (const auto& kv : m_byChar)
                if (kv.first != kTemplateKey && kv.first != kPreLoginKey) out.insert(kv.first);
        }

        // ---- migration / persistence ----
        // Seed the baseline cloned into any character that has no profiles yet (preserves a legacy setup).
        void SeedTemplate(std::vector<Named> profiles, int active = 0)
        {
            if (profiles.empty()) return;
            CharSet& t = m_byChar[kTemplateKey];
            t.profiles = std::move(profiles);
            t.active = std::min(std::max(active, 0), (int)t.profiles.size() - 1);
        }
        bool HasAnyChar() const
        {
            for (const auto& kv : m_byChar)
                if (kv.first != kTemplateKey && !kv.second.profiles.empty()) return true;
            return false;
        }

        void Serialize(nlohmann::json& j, const char* key)
        {
            SyncLiveToActive();
            nlohmann::json root = nlohmann::json::object();
            for (const auto& kv : m_byChar)
            {
                nlohmann::json arr = nlohmann::json::array();
                for (const Named& n : kv.second.profiles)
                    arr.push_back({ {"name", n.name}, {"data", m_toJson ? m_toJson(n.data) : nlohmann::json::object()} });
                root[kv.first] = { {"active", kv.second.active}, {"profiles", arr} };
            }
            j[key] = root;
        }
        // Returns true if the key existed (so the owner knows whether to run legacy migration).
        bool Deserialize(const nlohmann::json& j, const char* key)
        {
            m_byChar.clear();
            auto it = j.find(key);
            if (it == j.end() || !it->is_object()) return false;
            for (auto el = it->begin(); el != it->end(); ++el)
            {
                const nlohmann::json& o = el.value();
                if (!o.is_object()) continue;
                CharSet cs; cs.active = o.value("active", 0);
                if (o.contains("profiles") && o["profiles"].is_array())
                    for (const auto& e : o["profiles"])
                    {
                        Named n; n.name = e.value("name", std::string("Profile"));
                        if (e.contains("data") && m_fromJson) n.data = m_fromJson(e["data"]);
                        cs.profiles.push_back(std::move(n));
                    }
                if (!cs.profiles.empty())
                {
                    cs.active = std::min(std::max(cs.active, 0), (int)cs.profiles.size() - 1);
                    m_byChar[el.key()] = std::move(cs);
                }
            }
            return true;
        }

    private:
        static constexpr const char* kTemplateKey = "__template__";
        static constexpr const char* kPreLoginKey = "default";   // pre-login bucket (currentChar before MumbleLink resolves) -- not a real character, never an import source
        struct CharSet { std::vector<Named> profiles; int active = 0; };

        const CharSet* Find(const std::string& key) const
        {
            auto it = m_byChar.find(key);
            return it == m_byChar.end() ? nullptr : &it->second;
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
                }
                else
                {
                    Named d; d.name = "Default"; d.data = m_default ? m_default() : Payload{};
                    cs.profiles.push_back(std::move(d)); cs.active = 0;
                }
            }
            return cs;
        }
        void ApplyActive()
        {
            if (!m_apply || m_curKey.empty()) return;
            CharSet& cs = EnsureChar(m_curKey);
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

        std::map<std::string, CharSet> m_byChar;
        std::string m_curKey;
        bool        m_configured = false;
        CaptureFn   m_capture; ApplyFn m_apply; ToJsonFn m_toJson; FromJsonFn m_fromJson; DefaultFn m_default;
    };
}
