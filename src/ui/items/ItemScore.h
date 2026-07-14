#pragma once
#include <cctype>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// Shared item-search primitives -- the robust multi-token matching used by BOTH the inventory search and the
// all-items catalog. A query splits into lowercased alphanumeric tokens; each token is scored against a field
// (exact / prefix / contains). The CALLER takes the best score across its fields per token and requires every
// token to match SOME field -- so words need NOT be adjacent or in order ("coat berserker" finds "Berserker's
// Heavy Coat"). Kept byte-identical to the inventory tab's original scoring so behavior is unchanged.
// -----------------------------------------------------------------------------------------------------
namespace ItemUI
{
    inline std::string Lower(std::string s)
    {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    inline std::vector<std::string> QueryTerms(const char* query)
    {
        std::vector<std::string> terms;
        std::string term;
        for (const char* p = query ? query : ""; *p; ++p)
        {
            const unsigned char c = (unsigned char)*p;
            if (std::isalnum(c)) { term.push_back((char)std::tolower(c)); continue; }
            if (!term.empty()) { terms.push_back(term); term.clear(); }
        }
        if (!term.empty()) terms.push_back(term);
        return terms;
    }

    inline std::string JoinTerms(const std::vector<std::string>& terms)
    {
        std::string out;
        for (const std::string& term : terms)
        {
            if (!out.empty()) out.push_back(' ');
            out += term;
        }
        return out;
    }

    inline int TokenFieldScore(const std::string& field, const std::string& token,
                               int exactScore, int prefixScore, int containsScore)
    {
        if (field.empty() || token.empty()) return 0;
        if (field == token) return exactScore;
        if (field.size() >= token.size() && field.compare(0, token.size(), token) == 0) return prefixScore;
        return field.find(token) != std::string::npos ? containsScore : 0;
    }
}
