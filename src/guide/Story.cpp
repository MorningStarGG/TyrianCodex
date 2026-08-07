#include "guide/Story.h"
#include "util/Json.h" // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    std::string JStr(const json &o, const char *k)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }
    int JInt(const json &o, const char *k, int def)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }
    bool JBool(const json &o, const char *k, bool def)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }
}

bool StoryData::Load(const std::string &path)
{
    _byRelease.clear();
    std::ifstream f(path);
    if (!f)
        return false;
    json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return false;
    }

    try
    {
        const json &rel = (j.contains("releases") && j["releases"].is_object()) ? j["releases"] : j;
        if (!rel.is_object())
            return false;
        for (auto it = rel.begin(); it != rel.end(); ++it)
        {
            const json &r = it.value();
            if (!r.is_object() || !r.contains("episodes") || !r["episodes"].is_array())
                continue;
            std::vector<StoryEpisode> eps;
            for (const auto &e : r["episodes"])
            {
                if (!e.is_object())
                    continue;
                StoryEpisode ep;
                ep.storyId = JInt(e, "storyId", 0);
                ep.name = JStr(e, "name");
                ep.order = JInt(e, "order", (int)eps.size());
                ep.description = JStr(e, "description");
                ep.any = JBool(e, "any", false);
                ep.cumulative = JBool(e, "cumulative", false);
                if (e.contains("achievementIds") && e["achievementIds"].is_array())
                    for (const auto &id : e["achievementIds"])
                        if (id.is_number_integer())
                            ep.achievementIds.push_back(id.get<int>());
                eps.push_back(std::move(ep));
            }
            std::stable_sort(eps.begin(), eps.end(),
                             [](const StoryEpisode &a, const StoryEpisode &b)
                             { return a.order < b.order; });
            _byRelease[it.key()] = std::move(eps);
        }
    }
    catch (...)
    {
        _byRelease.clear();
        return false;
    }

    return !_byRelease.empty();
}

const std::vector<StoryEpisode> *StoryData::Episodes(const std::string &release) const
{
    auto it = _byRelease.find(release);
    return it != _byRelease.end() ? &it->second : nullptr;
}

const std::vector<std::string> &StoryData::ReleaseOrder()
{
    // Canonical story/release order (mirrors the builder's RELEASE_ORDER).
    static const std::vector<std::string> order =
        {"core", "lws1", "lws2", "hot", "lws3", "pof", "lws4", "lws5", "eod", "soto", "jw", "voe"};
    return order;
}

std::string StoryData::ReleaseName(const std::string &release)
{
    if (release == "core")
        return "Core Tyria";
    if (release == "lws1")
        return "Living World Season 1";
    if (release == "lws2")
        return "Living World Season 2";
    if (release == "hot")
        return "Heart of Thorns";
    if (release == "lws3")
        return "Living World Season 3";
    if (release == "pof")
        return "Path of Fire";
    if (release == "lws4")
        return "Living World Season 4";
    if (release == "lws5")
        return "The Icebrood Saga";
    if (release == "eod")
        return "End of Dragons";
    if (release == "soto")
        return "Secrets of the Obscure";
    if (release == "jw")
        return "Janthir Wilds";
    if (release == "voe")
        return "Visions of Eternity";
    return release;
}

// Mirror of the builder's season_to_release: "Season N" is matched generically so a NEW Living World season
// needs no code change here; only a brand-new expansion name would.
std::string StoryData::ReleaseForSeason(const std::string &seasonName)
{
    std::string s;
    s.reserve(seasonName.size());
    for (char c : seasonName)
        s.push_back((char)std::tolower((unsigned char)c));
    const auto has = [&s](const char *needle)
    { return s.find(needle) != std::string::npos; };

    const size_t se = s.find("season");
    if (se != std::string::npos)
        for (size_t i = se + 6; i < s.size(); ++i)
        {
            if (s[i] == ' ')
                continue;
            if (s[i] >= '1' && s[i] <= '9')
                return std::string("lws") + s[i];
            break;
        }
    if (has("scarlet"))
        return "lws1"; // "Scarlet's War" is the Living World Season 1 arc
    if (has("icebrood"))
        return "lws5";
    if (has("heart of thorns"))
        return "hot";
    if (has("path of fire"))
        return "pof";
    if (has("end of dragons"))
        return "eod";
    if (has("secrets of the obscure"))
        return "soto";
    if (has("janthir"))
        return "jw";
    if (has("visions of eternity"))
        return "voe";
    if (has("my story") || has("personal"))
        return "core";
    return std::string();
}

// The /v2/account access[] token that grants a release, or "" when nothing gates it. Living World seasons are
// deliberately absent: their episodes unlock per-account outside access[], so we can never prove a player
// cannot play one.
static const char *ReleaseAccessToken(const std::string &release)
{
    if (release == "core")
        return "GuildWars2";
    if (release == "hot")
        return "HeartOfThorns";
    if (release == "pof")
        return "PathOfFire";
    if (release == "eod")
        return "EndOfDragons";
    if (release == "soto")
        return "SecretsOfTheObscure";
    if (release == "jw")
        return "JanthirWilds";
    return nullptr; // lws1-5 (not gated) + voe (token unverified -> never gate on a guess)
}

bool StoryData::ReleasePlayable(const std::string &release, const std::vector<std::string> &access)
{
    if (access.empty())
        return true; // unknown -> fail open
    const char *tok = ReleaseAccessToken(release);
    if (!tok)
        return true; // nothing gates this release
    return std::find(access.begin(), access.end(), std::string(tok)) != access.end();
}

// ---- StoryProgressStore -------------------------------------------------------------------------------
void StoryProgressStore::Load(const std::string &accountPath, const std::string &charPath)
{
    _acctPath = accountPath;
    _charPath = charPath;
    _account.clear();
    _byChar.clear();

    try
    {
        std::ifstream af(_acctPath);
        if (af)
        {
            json j;
            af >> j;
            if (j.is_array())
                for (const auto &e : j)
                    if (e.is_string())
                        _account.insert(e.get<std::string>());
        }
    }
    catch (...)
    {
        _account.clear();
    }

    try
    {
        std::ifstream cf(_charPath);
        if (cf)
        {
            json j;
            cf >> j;
            if (j.is_object())
                for (auto it = j.begin(); it != j.end(); ++it)
                    if (it.value().is_array())
                    {
                        std::vector<std::string> keys;
                        for (const auto &e : it.value())
                            if (e.is_string())
                                keys.push_back(e.get<std::string>());
                        _byChar[it.key()] = std::move(keys);
                    }
        }
    }
    catch (...)
    {
        _byChar.clear();
    }
}

bool StoryProgressStore::IsDone(const std::string &key, const std::string &character) const
{
    if (_account.count(key))
        return true;
    if (character.empty())
        return false;
    auto it = _byChar.find(character);
    return it != _byChar.end() && std::find(it->second.begin(), it->second.end(), key) != it->second.end();
}

void StoryProgressStore::Set(const std::string &key, bool done, const std::string &character)
{
    if (key.empty())
        return;
    if (!character.empty())
    {
        auto &list = _byChar[character];
        auto pos = std::find(list.begin(), list.end(), key);
        bool changed;
        if (done)
        {
            changed = pos == list.end();
            if (changed)
                list.push_back(key);
        }
        else
        {
            changed = pos != list.end();
            if (changed)
                list.erase(pos);
        }
        if (changed)
        {
            ++_ver;
            SaveChars();
        }
    }
    else
    {
        bool changed = done ? _account.insert(key).second : (_account.erase(key) > 0);
        if (changed)
        {
            ++_ver;
            SaveAccount();
        }
    }
}

void StoryProgressStore::SaveAccount() const
{
    if (_acctPath.empty())
        return;
    json j = json::array();
    for (const std::string &k : _account)
        j.push_back(k);
    Json::WriteAtomic(_acctPath, j.dump());
}

void StoryProgressStore::SaveChars() const
{
    if (_charPath.empty())
        return;
    json j = json::object();
    for (const auto &kv : _byChar)
        j[kv.first] = kv.second;
    Json::WriteAtomic(_charPath, j.dump());
}

void StoryProgressStore::RenameChar(const std::string &from, const std::string &to)
{
    if (from.empty() || to.empty() || from == to)
        return;
    auto itF = _byChar.find(from);
    if (itF == _byChar.end())
        return;
    std::vector<std::string> &dst = _byChar[to];
    for (const std::string &k : itF->second)
        if (std::find(dst.begin(), dst.end(), k) == dst.end())
            dst.push_back(k);
    _byChar.erase(itF);
    ++_ver;
    SaveChars();
}

void StoryProgressStore::PurgeChar(const std::string &name)
{
    if (_byChar.erase(name))
    {
        ++_ver;
        SaveChars();
    }
}

void StoryProgressStore::CollectCharNames(std::set<std::string> &out) const
{
    for (const auto &kv : _byChar)
        out.insert(kv.first);
}
