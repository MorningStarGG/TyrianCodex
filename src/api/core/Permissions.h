#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// API-key permission scopes. We hold the user's key directly and resolve its scopes ourselves from /v2/tokeninfo.
// Every authenticated accessor declares the scope it needs; the Connection skips the call
// and the consumer shows "needs X permission" when the key lacks it.
namespace Api
{
    // The full set of scopes /v2/tokeninfo can report (GW2 wiki API:API_key). `Unknown` absorbs any future
    // scope name we don't recognize so parsing never drops the rest.
    enum class TokenPermission
    {
        Account,
        Builds,
        Characters,
        Guilds,
        Inventories,
        Progression,
        Pvp,
        Tradingpost,
        Unlocks,
        Wallet,
        Wvw,
        Unknown
    };

    inline const char *PermissionName(TokenPermission p)
    {
        switch (p)
        {
        case TokenPermission::Account:
            return "account";
        case TokenPermission::Builds:
            return "builds";
        case TokenPermission::Characters:
            return "characters";
        case TokenPermission::Guilds:
            return "guilds";
        case TokenPermission::Inventories:
            return "inventories";
        case TokenPermission::Progression:
            return "progression";
        case TokenPermission::Pvp:
            return "pvp";
        case TokenPermission::Tradingpost:
            return "tradingpost";
        case TokenPermission::Unlocks:
            return "unlocks";
        case TokenPermission::Wallet:
            return "wallet";
        case TokenPermission::Wvw:
            return "wvw";
        default:
            return "unknown";
        }
    }

    inline TokenPermission ParsePermission(const std::string &s)
    {
        if (s == "account")
            return TokenPermission::Account;
        if (s == "builds")
            return TokenPermission::Builds;
        if (s == "characters")
            return TokenPermission::Characters;
        if (s == "guilds")
            return TokenPermission::Guilds;
        if (s == "inventories")
            return TokenPermission::Inventories;
        if (s == "progression")
            return TokenPermission::Progression;
        if (s == "pvp")
            return TokenPermission::Pvp;
        if (s == "tradingpost")
            return TokenPermission::Tradingpost;
        if (s == "unlocks")
            return TokenPermission::Unlocks;
        if (s == "wallet")
            return TokenPermission::Wallet;
        if (s == "wvw")
            return TokenPermission::Wvw;
        return TokenPermission::Unknown;
    }

    // /v2/tokeninfo response: the key's id, display name, and granted scopes. (The `valid` flag is true once
    // we have successfully read tokeninfo for the current key; consumers gate auth calls on HasPermission.)
    struct TokenInfo
    {
        std::string id;
        std::string name;
        std::vector<TokenPermission> permissions;
        bool valid = false;

        bool Has(TokenPermission p) const
        {
            for (TokenPermission x : permissions)
                if (x == p)
                    return true;
            return false;
        }
    };
}
