#include "guide/FishHoles.h"

#include "app/App.h"
#include "app/FishData.h"

#include <algorithm>
#include <cctype>

namespace
{
    std::string Lower(const std::string& s)
    {
        std::string o; o.reserve(s.size());
        for (char c : s) o += (char)std::tolower((unsigned char)c);
        return o;
    }
}

namespace FishHoles
{
    const char* Pretty(const std::string& type)
    {
        // special-cased compounds; everything else is title-cased from the kind below.
        if (type == "fracfresh")   return "Fractured Freshwater";
        if (type == "fracchannel") return "Fractured Channel";
        if (type == "fraclake")    return "Fractured Lake";
        if (type == "fracdesert")  return "Fractured Desert";
        if (type == "lowbrackish") return "Brackish";
        if (type == "lowoffshore") return "Offshore";
        if (type == "lowshore")    return "Shore";
        if (type == "lowfresh")    return "Freshwater";
        if (type == "saltwater")   return "Saltwater";
        if (type == "freshwater")  return "Freshwater";
        if (type == "offshore")    return "Offshore";
        if (type == "coastal")     return "Coastal";
        if (type == "shore")       return "Shore";
        if (type == "desert")      return "Desert";
        if (type == "channel")     return "Channel";
        if (type == "boreal")      return "Boreal";
        if (type == "volcanic")    return "Volcanic";
        if (type == "river")       return "River";
        if (type == "noxious")     return "Noxious";
        if (type == "cavern")      return "Cavern";
        if (type == "polluted")    return "Polluted";
        if (type == "quarry")      return "Quarry";
        if (type == "nayosian")    return "Nayosian";
        if (type == "grotto")      return "Grotto";
        if (type == "dream")       return "Dream";
        if (type == "astral")      return "Astral";
        if (type == "spire")       return "Spire";
        if (type == "lake")        return "Lake";
        return type.empty() ? "Hole" : type.c_str();
    }

    ImU32 Color(const std::string& t, int a)
    {
        // by water family
        if (t == "lake" || t == "river" || t == "freshwater" || t == "lowfresh" || t == "channel" || t == "fracfresh"
            || t == "fraclake" || t == "fracchannel")                              return IM_COL32(96, 156, 206, a);   // fresh blue
        if (t == "offshore" || t == "lowoffshore" || t == "coastal" || t == "shore" || t == "lowshore"
            || t == "saltwater" || t == "boreal")                                  return IM_COL32(86, 176, 186, a);   // sea teal
        if (t == "desert" || t == "fracdesert" || t == "quarry")                   return IM_COL32(196, 166, 106, a);  // desert tan
        if (t == "volcanic")                                                       return IM_COL32(206, 116, 86, a);   // volcanic red
        if (t == "noxious" || t == "polluted")                                     return IM_COL32(126, 176, 96, a);   // toxic green
        if (t == "cavern" || t == "grotto")                                        return IM_COL32(146, 126, 186, a);  // cave purple
        if (t == "astral" || t == "dream" || t == "spire" || t == "nayosian")      return IM_COL32(176, 136, 196, a);  // exotic violet
        if (t == "lowbrackish")                                                    return IM_COL32(126, 156, 156, a);  // brackish gray-teal
        return IM_COL32(120, 170, 210, a);                                                                             // default water blue
    }

    std::vector<std::string> TypesForHole(const std::string& fishHole)
    {
        const std::string h = Lower(fishHole);
        std::vector<std::string> out;
        auto add = [&](const char* t) { for (const std::string& s : out) if (s == t) return; out.push_back(t); };
        const bool frac = h.find("fractured") != std::string::npos;
        auto inh = [&](const char* k) { return h.find(k) != std::string::npos; };

        // "shore" is a substring of "offshore" -> match offshore first, gate shore on its absence.
        if (inh("offshore"))   { add("offshore"); add("lowoffshore"); }
        else if (inh("shore")) { add("shore"); add("lowshore"); }

        if (inh("freshwater")) { if (frac) add("fracfresh"); else { add("freshwater"); add("lowfresh"); } }
        if (inh("channel"))    add(frac ? "fracchannel" : "channel");
        if (inh("lake"))       add(frac ? "fraclake" : "lake");
        if (inh("desert"))     add(frac ? "fracdesert" : "desert");
        if (inh("coastal"))    add("coastal");
        if (inh("saltwater"))  add("saltwater");
        if (inh("boreal"))     add("boreal");
        if (inh("volcanic"))   add("volcanic");
        if (inh("river"))      add("river");
        if (inh("noxious"))    add("noxious");
        if (inh("polluted"))   add("polluted");
        if (inh("cavern"))     add("cavern");
        if (inh("grotto"))     add("grotto");
        if (inh("quarry"))     add("quarry");
        if (inh("nayosian"))   add("nayosian");
        if (inh("astral"))     add("astral");
        if (inh("dream"))      add("dream");
        if (inh("spire"))      add("spire");
        if (inh("brackish"))   add("lowbrackish");
        return out;   // empty => any/open-water/unknown -> matches every hole
    }

    std::set<std::string> WatchedTypes(const App& app)
    {
        std::set<std::string> out;
        for (int id : app.state.fishWatch)
        {
            const FishData::Fish* f = app.fish.Find(id);
            if (!f) continue;
            const std::vector<std::string> ts = TypesForHole(f->hole);
            if (ts.empty()) return {};   // an any/open-water watched fish -> every hole qualifies
            for (const std::string& t : ts) out.insert(t);
        }
        return out;   // empty => watchlist empty -> overlay shows all; nav uses nearest of any kind
    }
}
