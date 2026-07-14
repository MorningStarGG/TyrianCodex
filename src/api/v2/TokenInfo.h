#pragma once
#include <functional>
#include "../core/Permissions.h"
#include "../core/Result.h"

// GET /v2/tokeninfo  (authenticated; no specific scope - any valid key works).
// Returns the key's id, display name, and the scopes it was created with. This is the gate: the Client
// fetches it whenever the key changes and stores the result on the Connection, so every other authenticated
// accessor can be skipped up-front when the key lacks the scope it needs. Wiki: API:2/tokeninfo.
namespace Api { class Connection; }
namespace Api::V2
{
    TokenInfo ParseTokenInfo(const nlohmann::json& j);

    class TokenInfoEndpoint
    {
    public:
        explicit TokenInfoEndpoint(Connection* c) : _c(c) {}
        void Get(std::function<void(Result<TokenInfo>)> cb) const;
    private:
        Connection* _c;
    };
}
