#include "app/DecorationCatalog.h"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    // Null-safe readers (decorations.json is ours, but never let a bad field throw into GW2's render loop).
    std::string JS(const json& o, const char* k)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }
    int JI(const json& o, const char* k, int def = 0)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }
    float JF(const json& o, const char* k, float def = 0.f)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_number()) ? it->get<float>() : def;
    }
}

void DecorationCatalog::Load(const std::string& path)
{
    decorations_.clear();
    glyphs_.clear();
    cats_.clear();
    nodes_.clear();
    categories_.clear();
    decoIndex_.clear();

    std::ifstream f(path);
    if (!f) return;
    json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;

    if (auto c = j.find("categories"); c != j.end() && c->is_object())
        for (auto it = c->begin(); it != c->end(); ++it)
            if (it.value().is_string()) categories_[std::atoi(it.key().c_str())] = it.value().get<std::string>();

    if (auto d = j.find("decorations"); d != j.end() && d->is_object())
    {
        decorations_.reserve(d->size());
        for (auto it = d->begin(); it != d->end(); ++it)
        {
            const json& e = it.value();
            if (!e.is_object()) continue;
            Decoration dec;
            dec.id        = std::atoi(it.key().c_str());
            dec.name      = JS(e, "name");
            dec.desc      = JS(e, "desc");
            dec.icon      = JS(e, "icon");
            dec.max       = JI(e, "max");
            dec.source    = JS(e, "source");
            dec.wiki      = JS(e, "wiki");
            dec.sheet     = JS(e, "sheet");
            dec.handiwork = JI(e, "handiwork");
            dec.recipeId  = JI(e, "recipeId");
            dec.premium   = e.contains("premium") && e["premium"].is_boolean() && e["premium"].get<bool>();   // Black-Lion / gem-store
            if (auto cat = e.find("categories"); cat != e.end() && cat->is_array())
                for (const auto& cv : *cat) if (cv.is_number_integer()) dec.categories.push_back(cv.get<int>());
            if (auto r = e.find("recipe"); r != e.end() && r->is_array())
                for (const auto& ri : *r)
                    if (ri.is_object()) dec.recipe.push_back({ JI(ri, "itemId"), JS(ri, "name"), JI(ri, "count") });
            decoIndex_[dec.id] = (int)decorations_.size();
            decorations_.push_back(std::move(dec));
        }
    }

    if (auto g = j.find("glyphs"); g != j.end() && g->is_object())
        for (auto it = g->begin(); it != g->end(); ++it)
            if (it.value().is_object()) glyphs_.push_back({ it.key(), JS(it.value(), "slot"), JI(it.value(), "itemId") });

    if (auto c = j.find("cats"); c != j.end() && c->is_object())
        for (auto it = c->begin(); it != c->end(); ++it)
        {
            const json& e = it.value();
            if (!e.is_object()) continue;
            Cat ct;
            ct.id         = std::atoi(it.key().c_str());
            ct.hint       = JS(e, "hint");
            ct.name       = JS(e, "name");
            ct.identity   = JS(e, "identity");
            ct.lure       = JS(e, "lure");
            ct.location   = JS(e, "location");
            ct.region     = JS(e, "region");
            ct.waypoint   = JS(e, "waypoint");
            ct.wpChatLink = JS(e, "wpChatLink");
            ct.notes      = JS(e, "notes");
            ct.cx         = JF(e, "cx");
            ct.cy         = JF(e, "cy");
            ct.mapId      = (uint32_t)JI(e, "mapId");
            cats_.push_back(std::move(ct));
        }

    if (auto n = j.find("nodes"); n != j.end() && n->is_object())
        for (auto it = n->begin(); it != n->end(); ++it)
            if (it.value().is_object()) nodes_.push_back({ it.key(), JS(it.value(), "name") });
}

const DecorationCatalog::Decoration* DecorationCatalog::ById(int id) const
{
    auto it = decoIndex_.find(id);
    return it != decoIndex_.end() ? &decorations_[it->second] : nullptr;
}

std::string DecorationCatalog::CategoryName(int id) const
{
    auto it = categories_.find(id);
    return it != categories_.end() ? it->second : std::string();
}
