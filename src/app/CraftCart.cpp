#include "app/CraftCart.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

// All state + mutations run on the main thread (UI). Best-effort Save() + a Version() bump on every mutation,
// exactly like guide/Progress. The file is account-wide (one per addon install).
namespace
{
    struct Project
    {
        std::string                    name;
        std::vector<CraftCart::Item>   items;   // (itemId, qty)
        std::vector<int>               got;     // material item ids checked off
    };

    std::vector<Project> g_projects;            // creation order (drives the picker)
    std::string          g_active;
    std::string          g_path;
    uint64_t             g_version = 0;

    Project* Find(const std::string& name)
    {
        for (Project& p : g_projects) if (p.name == name) return &p;
        return nullptr;
    }
    Project* Resolve(const std::string& name) { return Find(name.empty() ? g_active : name); }

    std::string UniqueName(std::string base)
    {
        if (base.empty()) base = "Project";
        if (!Find(base)) return base;
        for (int n = 2; ; ++n) { std::string c = base + " " + std::to_string(n); if (!Find(c)) return c; }
    }

    void Save()
    {
        ++g_version;
        if (g_path.empty()) return;
        nlohmann::json j;
        j["schema"] = 1;
        j["active"] = g_active;
        nlohmann::json arr = nlohmann::json::array();
        for (const Project& p : g_projects)
        {
            nlohmann::json items = nlohmann::json::array();
            for (const CraftCart::Item& it : p.items) items.push_back({ it.first, it.second });
            nlohmann::json got = nlohmann::json::array();
            for (int id : p.got) got.push_back(id);
            arr.push_back({ { "name", p.name }, { "items", std::move(items) }, { "got", std::move(got) } });
        }
        j["projects"] = std::move(arr);

        Json::WriteAtomic(g_path, j.dump());
    }

    void Load()
    {
        g_projects.clear();
        g_active.clear();
        if (!g_path.empty())
        {
            std::ifstream f(g_path, std::ios::binary);
            if (f)
            {
                nlohmann::json j;
                try { f >> j; } catch (...) { j = nlohmann::json(); }
                if (j.is_object())
                {
                    if (auto a = j.find("active"); a != j.end() && a->is_string()) g_active = a->get<std::string>();
                    if (auto pr = j.find("projects"); pr != j.end() && pr->is_array())
                        for (const auto& e : *pr)
                        {
                            if (!e.is_object()) continue;
                            Project p;
                            if (auto n = e.find("name"); n != e.end() && n->is_string()) p.name = n->get<std::string>();
                            if (p.name.empty() || Find(p.name)) continue;
                            if (auto it = e.find("items"); it != e.end() && it->is_array())
                                for (const auto& pr2 : *it)
                                    if (pr2.is_array() && pr2.size() == 2 && pr2[0].is_number_integer() && pr2[1].is_number_integer())
                                    {
                                        const int id = pr2[0].get<int>();
                                        const long long q = pr2[1].get<long long>();
                                        if (id > 0 && q > 0) p.items.emplace_back(id, q);
                                    }
                            if (auto g = e.find("got"); g != e.end() && g->is_array())
                                for (const auto& x : *g) if (x.is_number_integer()) { const int id = x.get<int>(); if (id > 0) p.got.push_back(id); }
                            g_projects.push_back(std::move(p));
                        }
                }
            }
        }
        if (g_projects.empty()) g_projects.push_back({ "My Cart", {}, {} });
        if (!Find(g_active)) g_active = g_projects.front().name;
    }
}

void CraftCart::Init(const std::string& path)
{
    g_path = path;
    ++g_version;
    if (!path.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    }
    Load();
}

void CraftCart::Shutdown()
{
    g_projects.clear();
    g_active.clear();
    g_path.clear();
    ++g_version;
}

uint64_t CraftCart::Version() { return g_version; }

std::vector<std::string> CraftCart::Names()
{
    std::vector<std::string> out;
    out.reserve(g_projects.size());
    for (const Project& p : g_projects) out.push_back(p.name);
    return out;
}

std::string CraftCart::Active() { return g_active; }

void CraftCart::SetActive(const std::string& name)
{
    if (name == g_active || !Find(name)) return;
    g_active = name;
    Save();
}

std::string CraftCart::New(const std::string& name)
{
    std::string nm = name;
    if (nm.empty()) nm = "Project " + std::to_string((int)g_projects.size() + 1);
    nm = UniqueName(nm);
    g_projects.push_back({ nm, {}, {} });
    g_active = nm;
    Save();
    return nm;
}

bool CraftCart::Rename(const std::string& oldName, const std::string& newName)
{
    if (newName.empty()) return false;
    Project* p = Find(oldName);
    if (!p) return false;
    if (newName == oldName) return true;
    if (Find(newName)) return false;
    const bool wasActive = (g_active == oldName);
    p->name = newName;
    if (wasActive) g_active = newName;
    Save();
    return true;
}

void CraftCart::Delete(const std::string& name)
{
    auto it = std::find_if(g_projects.begin(), g_projects.end(), [&](const Project& p) { return p.name == name; });
    if (it == g_projects.end()) return;
    const bool wasActive = (g_active == name);
    g_projects.erase(it);
    if (g_projects.empty()) { g_projects.push_back({ "My Cart", {}, {} }); g_active = "My Cart"; }
    else if (wasActive) g_active = g_projects.front().name;
    Save();
}

std::vector<CraftCart::Item> CraftCart::Items(const std::string& project)
{
    Project* p = Resolve(project);
    return p ? p->items : std::vector<Item>{};
}

int CraftCart::Count(const std::string& project)
{
    Project* p = Resolve(project);
    return p ? (int)p->items.size() : 0;
}

void CraftCart::Add(const std::string& project, int itemId, long long qty)
{
    if (itemId <= 0 || qty <= 0) return;
    Project* p = Resolve(project);
    if (!p) return;
    for (Item& it : p->items) if (it.first == itemId) { it.second += qty; Save(); return; }
    p->items.emplace_back(itemId, qty);
    Save();
}

void CraftCart::SetQty(const std::string& project, int itemId, long long qty)
{
    Project* p = Resolve(project);
    if (!p) return;
    for (size_t i = 0; i < p->items.size(); ++i)
        if (p->items[i].first == itemId)
        {
            if (qty <= 0) p->items.erase(p->items.begin() + i);
            else          p->items[i].second = qty;
            Save();
            return;
        }
    if (qty > 0) { p->items.emplace_back(itemId, qty); Save(); }
}

void CraftCart::Remove(const std::string& project, int itemId)
{
    Project* p = Resolve(project);
    if (!p) return;
    for (size_t i = 0; i < p->items.size(); ++i)
        if (p->items[i].first == itemId) { p->items.erase(p->items.begin() + i); Save(); return; }
}

void CraftCart::Clear(const std::string& project)
{
    Project* p = Resolve(project);
    if (!p) return;
    p->items.clear();
    p->got.clear();
    Save();
}

bool CraftCart::IsGot(const std::string& project, int itemId)
{
    Project* p = Resolve(project);
    if (!p) return false;
    return std::find(p->got.begin(), p->got.end(), itemId) != p->got.end();
}

void CraftCart::SetGot(const std::string& project, int itemId, bool on)
{
    Project* p = Resolve(project);
    if (!p || itemId <= 0) return;
    auto it = std::find(p->got.begin(), p->got.end(), itemId);
    if (on)  { if (it == p->got.end()) { p->got.push_back(itemId); Save(); } }
    else     { if (it != p->got.end()) { p->got.erase(it); Save(); } }
}
