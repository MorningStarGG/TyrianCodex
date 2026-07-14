#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

// Null-safe JSON readers. The GW2 API returns `null` for many optional fields (e.g. a character's `guild`,
// a step's `recommendedLevel`), and nlohmann's `value(key, default)` THROWS type_error.302 on a present-
// but-null key - which, uncaught on the worker thread, would be a silent dropped request at best. These
// return the default for missing OR null OR wrong-typed fields, the same defensive pattern already proven in
// src/Dataset.cpp (kept separate so the API layer doesn't depend on the dataset layer).
namespace Api::Json
{
    using nlohmann::json;

    inline std::string Str(const json& o, const char* key, const std::string& def = "")
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : def;
    }
    inline int Int(const json& o, const char* key, int def = 0)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }
    inline int64_t Int64(const json& o, const char* key, int64_t def = 0)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number_integer()) ? it->get<int64_t>() : def;
    }
    inline double Num(const json& o, const char* key, double def = 0.0)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number()) ? it->get<double>() : def;
    }
    inline bool Bool(const json& o, const char* key, bool def = false)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }
    // A nested object/array, or a null json if absent (callers test .is_object()/.is_array()).
    inline const json& Node(const json& o, const char* key)
    {
        static const json kNull;
        auto it = o.find(key);
        return it != o.end() ? *it : kNull;
    }
    // A string array field -> vector<string> (skips non-string entries).
    inline std::vector<std::string> StrArray(const json& o, const char* key)
    {
        std::vector<std::string> out;
        auto it = o.find(key);
        if (it != o.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_string()) out.push_back(e.get<std::string>());
        return out;
    }
    // An int array field -> vector<int> (skips non-integer entries).
    inline std::vector<int> IntArray(const json& o, const char* key)
    {
        std::vector<int> out;
        auto it = o.find(key);
        if (it != o.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_number_integer()) out.push_back(e.get<int>());
        return out;
    }
}
