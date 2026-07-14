#include "app/CosmeticCatalog.h"

#include <array>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace
{
    constexpr int kKindCount = (int)CosmeticCatalog::Kind::Count;

    // file basename per kind (data/<base>.json), aligned with CosmeticCatalog::Kind order.
    const std::array<const char*, kKindCount> kBase = {
        "mounts", "outfits", "gliders", "jadebots", "skiffs", "novelties",
        "finishers", "mailcarriers", "minis", "mistchampions", "titles", "emotes"
    };

    struct KindData
    {
        std::vector<CosmeticCatalog::Cosmetic> items;
        std::unordered_map<int, int> index;   // id -> index in items
    };

    std::array<KindData, kKindCount> g_kinds;
    bool      g_loaded = false;
    uint64_t  g_version = 1;

    CosmeticCatalog::Cosmetic g_empty;

    void BuildKey(CosmeticCatalog::Cosmetic& c)
    {
        std::string k = c.name + " " + c.sub;
        for (char& ch : k) ch = (char)std::tolower((unsigned char)ch);
        c.keyLower = std::move(k);
    }

    // Read data/<base>.json into `out` (JSON only; CBOR dropped). Returns true if non-empty.
    bool LoadFile(const std::string& path, std::vector<CosmeticCatalog::Cosmetic>& out)
    {
        if (path.empty()) return false;
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss; ss << f.rdbuf();
        const std::string bytes = ss.str();
        if (bytes.empty()) return false;
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(bytes);   // JSON only (CBOR fully dropped)
        }
        catch (...) { return false; }
        if (!j.is_object()) return false;
        auto arr = j.find("items");
        if (arr == j.end() || !arr->is_array()) return false;

        out.clear();
        out.reserve(arr->size());
        for (const auto& e : *arr)
        {
            if (!e.is_object()) continue;
            CosmeticCatalog::Cosmetic c;
            c.id         = e.value("id", 0);
            c.unlockItem = e.value("unlockItem", 0);
            c.isDefault  = e.value("default", false);
            c.name = e.value("name", std::string());
            c.icon = e.value("icon", std::string());
            c.sub  = e.value("sub", std::string());
            c.premium = e.value("premium", false);
            if (c.id <= 0 || c.name.empty()) continue;
            BuildKey(c);
            out.push_back(std::move(c));
        }
        return !out.empty();
    }

    void LoadKind(int k, const std::string& dataDir)
    {
        KindData& kd = g_kinds[k];
        kd.items.clear();
        kd.index.clear();
        if (dataDir.empty()) return;
        const std::string base = std::string(dataDir) + "\\" + kBase[k];
        LoadFile(base + ".json", kd.items);   // JSON-only (CBOR dropped across the addon)
        kd.index.reserve(kd.items.size() * 2);
        for (int i = 0; i < (int)kd.items.size(); ++i) kd.index[kd.items[i].id] = i;
    }

    std::unordered_map<std::string, CosmeticCatalog::Anim> g_anim;   // "<kind>_<id>" -> multi-outcome cycling animation

    // data/cosmetic_anim.json: { "<kind>_<id>": { "frames": N, "delayMs": [...] } } (build_cosmetic_anim.py).
    void LoadAnim(const std::string& dataDir)
    {
        g_anim.clear();
        if (dataDir.empty()) return;
        std::ifstream f(dataDir + "\\cosmetic_anim.json", std::ios::binary);
        if (!f) return;
        std::ostringstream ss; ss << f.rdbuf();
        nlohmann::json j;
        try { j = nlohmann::json::parse(ss.str()); } catch (...) { return; }
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_object()) continue;
            CosmeticCatalog::Anim a;
            a.frames = it.value().value("frames", 0);
            if (it.value().contains("delayMs") && it.value()["delayMs"].is_array())
                for (const auto& d : it.value()["delayMs"]) if (d.is_number_integer()) a.delayMs.push_back(d.get<int>());
            if (a.frames > 0) g_anim[it.key()] = std::move(a);
        }
    }
}

void CosmeticCatalog::Init(const std::string& dataDir)
{
    for (int k = 0; k < kKindCount; ++k) LoadKind(k, dataDir);
    LoadAnim(dataDir);   // multi-outcome (tonic) cycling-render manifest (data/cosmetic_anim.json)
    g_loaded = true;
    ++g_version;
}

int CosmeticCatalog::Anim::FrameAt(double seconds) const
{
    if (frames <= 0) return 0;
    int total = 0;
    for (int d : delayMs) total += d;
    if (total <= 0) return 0;
    int ms = (int)(seconds * 1000.0) % total;
    int acc = 0;
    for (int i = 0; i < frames && i < (int)delayMs.size(); ++i) { acc += delayMs[i]; if (ms < acc) return i; }
    return frames - 1;
}

const CosmeticCatalog::Anim* CosmeticCatalog::AnimFor(Kind k, int id)
{
    auto it = g_anim.find(std::to_string((int)k) + "_" + std::to_string(id));
    return it == g_anim.end() ? nullptr : &it->second;
}

void CosmeticCatalog::Shutdown()
{
    for (KindData& kd : g_kinds) { std::vector<Cosmetic>().swap(kd.items); kd.index.clear(); }
    g_anim.clear();
    g_loaded = false;
    ++g_version;
}

bool     CosmeticCatalog::IsReady() { return g_loaded; }
uint64_t CosmeticCatalog::Version() { return g_version; }

int CosmeticCatalog::Count(Kind k)
{
    const int i = (int)k;
    return (i >= 0 && i < kKindCount) ? (int)g_kinds[i].items.size() : 0;
}

const std::vector<CosmeticCatalog::Cosmetic>& CosmeticCatalog::Items(Kind k)
{
    static const std::vector<Cosmetic> kNone;
    const int i = (int)k;
    return (i >= 0 && i < kKindCount) ? g_kinds[i].items : kNone;
}

const CosmeticCatalog::Cosmetic& CosmeticCatalog::ById(Kind k, int id)
{
    const int i = (int)k;
    if (i < 0 || i >= kKindCount) return g_empty;
    const KindData& kd = g_kinds[i];
    auto it = kd.index.find(id);
    return (it != kd.index.end()) ? kd.items[it->second] : g_empty;
}
