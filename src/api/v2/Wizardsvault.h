#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/wizardsvault/{listings,objectives} (anonymous, day-cached): the Wizard's Vault catalogs - the reward
// listings (id -> item/count/type/acclaim cost) and the objective definitions (id -> title/track/acclaim).
// (Distinct from /v2/account/wizardsvault/*, the player's progress.) Wiki: API:2/wizardsvault.
namespace Api { class Connection; }
namespace Api::V2
{
    struct WizardsVaultListing      { int id = 0; int itemId = 0; int itemCount = 0; std::string type; int cost = 0; nlohmann::json raw; };
    struct WizardsVaultObjectiveDef { int id = 0; std::string title; std::string track; int acclaim = 0; nlohmann::json raw; };

    inline WizardsVaultListing ParseWizardsVaultListing(const nlohmann::json& j)
    {
        WizardsVaultListing l; l.id = Json::Int(j, "id"); l.itemId = Json::Int(j, "item_id"); l.itemCount = Json::Int(j, "item_count");
        l.type = Json::Str(j, "type"); l.cost = Json::Int(j, "cost"); l.raw = j; return l;
    }
    inline WizardsVaultObjectiveDef ParseWizardsVaultObjectiveDef(const nlohmann::json& j)
    {
        WizardsVaultObjectiveDef o; o.id = Json::Int(j, "id"); o.title = Json::Str(j, "title"); o.track = Json::Str(j, "track"); o.acclaim = Json::Int(j, "acclaim"); o.raw = j; return o;
    }

    class WizardsVaultListingsEndpoint   : public BulkEndpoint<WizardsVaultListing>      { public: explicit WizardsVaultListingsEndpoint(Connection* c)   : BulkEndpoint(c, "/v2/wizardsvault/listings",   &ParseWizardsVaultListing,      kStaticTtlSec) {} };
    class WizardsVaultObjectivesEndpoint : public BulkEndpoint<WizardsVaultObjectiveDef> { public: explicit WizardsVaultObjectivesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/wizardsvault/objectives", &ParseWizardsVaultObjectiveDef, kStaticTtlSec) {} };

    // /v2/wizardsvault facade: .Listings() + .Objectives().
    class WizardsVaultEndpoint
    {
    public:
        explicit WizardsVaultEndpoint(Connection* c) : _c(c) {}
        WizardsVaultListingsEndpoint   Listings()   const { return WizardsVaultListingsEndpoint(_c); }
        WizardsVaultObjectivesEndpoint Objectives() const { return WizardsVaultObjectivesEndpoint(_c); }
    private:
        Connection* _c;
    };
}
