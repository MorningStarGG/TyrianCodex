#include "Client.h"

namespace Api
{
    void Client::SetApiKey(const std::string& key)
    {
        _conn.SetApiKey(key);

        if (key.empty())
        {
            _conn.SetTokenInfo(Api::TokenInfo{});   // no key -> no scopes
            FireScopes();
            return;
        }

        _scopesResolving = true;
        V2().TokenInfo().Get([this](Result<Api::TokenInfo> r) {
            _scopesResolving = false;
            _conn.SetTokenInfo(r.ok ? r.value : Api::TokenInfo{});   // failure -> treat as no scopes
            FireScopes();
        });
    }

    void Client::FireScopes()
    {
        for (auto& cb : _scopeListeners)
            if (cb) { try { cb(); } catch (...) { /* a listener must never break key handling */ } }
    }
}
