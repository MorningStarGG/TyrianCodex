#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/Connection.h"
#include "core/Permissions.h"
#include "v2/Account.h"
#include "v2/Achievements.h"
#include "v2/Characters.h"
#include "v2/Items.h"
#include "v2/Maps.h"
#include "v2/Stories.h"
#include "v2/TokenInfo.h"
// --- world / static reference (one header per root) ---
#include "v2/Worlds.h"
#include "v2/Colors.h"
#include "v2/Currencies.h"
#include "v2/Files.h"
#include "v2/Quaggans.h"
#include "v2/MailCarriers.h"
#include "v2/Dungeons.h"
#include "v2/Raids.h"
#include "v2/MapChests.h"
#include "v2/WorldBosses.h"
#include "v2/DailyCrafting.h"
#include "v2/Build.h"
// --- profession / build reference ---
#include "v2/Professions.h"
#include "v2/Races.h"
#include "v2/Specializations.h"
#include "v2/Skills.h"
#include "v2/Traits.h"
#include "v2/Legends.h"
#include "v2/Pets.h"
#include "v2/Masteries.h"
#include "v2/Titles.h"
// --- wardrobe / cosmetics ---
#include "v2/Skins.h"
#include "v2/Outfits.h"
#include "v2/Gliders.h"
#include "v2/Finishers.h"
#include "v2/Novelties.h"
#include "v2/Emotes.h"
#include "v2/Minis.h"
#include "v2/Mounts.h"
// --- item-adjacent + map reference ---
#include "v2/ItemStats.h"
#include "v2/Recipes.h"
#include "v2/Materials.h"
#include "v2/Continents.h"
// --- commerce / guild / emblem / pvp / wvw ---
#include "v2/Commerce.h"
#include "v2/Guild.h"
#include "v2/Emblem.h"
#include "v2/Pvp.h"
#include "v2/Wvw.h"
// --- v1 (legacy) facade: the full fully-typed v1 surface ---
#include "v1/V1.h"
// --- story / biography (personal-story reference) ---
#include "v2/Quests.h"
#include "v2/Backstory.h"
// --- home / homestead / unlock catalogs ---
#include "v2/Home.h"
#include "v2/Homestead.h"
#include "v2/JadeBots.h"
#include "v2/Skiffs.h"
#include "v2/LegendaryArmory.h"
#include "v2/Logos.h"
#include "v2/Wizardsvault.h"
// --- subtoken minting ---
#include "v2/Subtoken.h"

// The public face of the GW2 API client. Owns one connection (worker thread + pipeline)
// and exposes the v2 endpoint families through a `V2()` facade, exactly
// like `Gw2ApiClient.V2.Characters[name]`. A single global instance lives in entry.cpp: Init() on load,
// Pump() each frame, SetApiKey() from the settings field, Shutdown() on unload.
//
// Scope handling re-resolves /v2/tokeninfo; consumers register OnScopesChanged
// (the SubtokenUpdated equivalent) to retry once the scopes are known, and check HasPermission before an
// authenticated call. Anonymous endpoints (maps, items, ...) work with no key at all.
namespace Api
{
    namespace V2
    {
        // Thin facade: `client.V2().Characters().Get(...)`. Each accessor binds the shared Connection.
        struct Endpoints
        {
            Connection *c = nullptr;
            TokenInfoEndpoint TokenInfo() const { return TokenInfoEndpoint(c); }
            CharactersEndpoint Characters() const { return CharactersEndpoint(c); }
            AccountEndpoint Account() const { return AccountEndpoint(c); }
            MapsEndpoint Maps() const { return MapsEndpoint(c); }
            StoriesEndpoint Stories() const { return StoriesEndpoint(c); }
            AchievementsEndpoint Achievements() const { return AchievementsEndpoint(c); }
            ItemsEndpoint Items() const { return ItemsEndpoint(c); }

            // --- World / static reference data (anonymous, day-cached) ---
            WorldsEndpoint Worlds() const { return WorldsEndpoint(c); }
            ColorsEndpoint Colors() const { return ColorsEndpoint(c); }
            CurrenciesEndpoint Currencies() const { return CurrenciesEndpoint(c); }
            FilesEndpoint Files() const { return FilesEndpoint(c); }
            QuaggansEndpoint Quaggans() const { return QuaggansEndpoint(c); }
            MailCarriersEndpoint MailCarriers() const { return MailCarriersEndpoint(c); }
            DungeonsApiEndpoint Dungeons() const { return DungeonsApiEndpoint(c); }
            RaidsEndpoint Raids() const { return RaidsEndpoint(c); }
            MapChestsEndpoint MapChests() const { return MapChestsEndpoint(c); }
            WorldBossesEndpoint WorldBosses() const { return WorldBossesEndpoint(c); }
            DailyCraftingEndpoint DailyCrafting() const { return DailyCraftingEndpoint(c); }
            BuildEndpoint Build() const { return BuildEndpoint(c); }

            // --- Profession / build reference data (anonymous, day-cached) ---
            ProfessionsEndpoint Professions() const { return ProfessionsEndpoint(c); }
            RacesEndpoint Races() const { return RacesEndpoint(c); }
            SpecializationsEndpoint Specializations() const { return SpecializationsEndpoint(c); }
            SkillsEndpoint Skills() const { return SkillsEndpoint(c); }
            TraitsEndpoint Traits() const { return TraitsEndpoint(c); }
            LegendsEndpoint Legends() const { return LegendsEndpoint(c); }
            PetsEndpoint Pets() const { return PetsEndpoint(c); }
            MasteriesEndpoint Masteries() const { return MasteriesEndpoint(c); }
            TitlesEndpoint Titles() const { return TitlesEndpoint(c); }

            // --- Wardrobe / cosmetics (anonymous, day-cached) ---
            SkinsEndpoint Skins() const { return SkinsEndpoint(c); }
            OutfitsEndpoint Outfits() const { return OutfitsEndpoint(c); }
            GlidersEndpoint Gliders() const { return GlidersEndpoint(c); }
            FinishersEndpoint Finishers() const { return FinishersEndpoint(c); }
            NoveltiesEndpoint Novelties() const { return NoveltiesEndpoint(c); }
            EmotesEndpoint Emotes() const { return EmotesEndpoint(c); }
            MinisEndpoint Minis() const { return MinisEndpoint(c); }
            MountsEndpoint Mounts() const { return MountsEndpoint(c); }

            // --- Item-adjacent + map reference data (anonymous, day-cached) ---
            ItemStatsEndpoint ItemStats() const { return ItemStatsEndpoint(c); }
            RecipesEndpoint Recipes() const { return RecipesEndpoint(c); }
            MaterialsEndpoint Materials() const { return MaterialsEndpoint(c); }
            ContinentsEndpoint Continents() const { return ContinentsEndpoint(c); }

            // --- Trading post (prices/listings anon; delivery/transactions need tradingpost) ---
            CommerceEndpoint Commerce() const { return CommerceEndpoint(c); }

            // --- Guild (public card anon; sub-resources need guilds) + emblem art ---
            GuildEndpoint Guild() const { return GuildEndpoint(c); }
            EmblemEndpoint Emblem() const { return EmblemEndpoint(c); }

            // --- Structured PvP (catalogs anon; stats/games/standings need pvp) + WvW (anon) ---
            PvpEndpoint Pvp() const { return PvpEndpoint(c); }
            WvwEndpoint Wvw() const { return WvwEndpoint(c); }

            // --- story / biography reference (anonymous) ---
            QuestsEndpoint Quests() const { return QuestsEndpoint(c); }
            BackstoryEndpoint Backstory() const { return BackstoryEndpoint(c); }

            // --- home / homestead / unlock catalogs (anonymous) ---
            HomeEndpoint Home() const { return HomeEndpoint(c); }
            HomesteadEndpoint Homestead() const { return HomesteadEndpoint(c); }
            JadeBotsEndpoint JadeBots() const { return JadeBotsEndpoint(c); }
            SkiffsEndpoint Skiffs() const { return SkiffsEndpoint(c); }
            LegendaryArmoryEndpoint LegendaryArmory() const { return LegendaryArmoryEndpoint(c); }
            LogosEndpoint Logos() const { return LogosEndpoint(c); }
            WizardsVaultEndpoint WizardsVault() const { return WizardsVaultEndpoint(c); }

            // --- subtoken minting (authenticated) ---
            CreateSubtokenEndpoint CreateSubtoken() const { return CreateSubtokenEndpoint(c); }
        };
    }

    class Client
    {
    public:
        void Init() { _conn.Init(); }
        void Shutdown() { _conn.Shutdown(); }
        void Pump() { _conn.Pump(); } // delivers completed callbacks on the calling (main) thread

        // Set/replace the API key (from the settings field). Re-resolves the granted scopes, then fires the
        // OnScopesChanged listeners. An empty key clears scopes (anonymous endpoints still work).
        void SetApiKey(const std::string &key);

        bool HasKey() const { return const_cast<Connection &>(_conn).HasKey(); }
        bool HasPermission(TokenPermission p) const { return _conn.HasPermission(p); }
        Api::TokenInfo ScopeInfo() const { return _conn.GetTokenInfo(); }
        bool ScopesResolving() const { return _scopesResolving; }

        // Register a listener fired whenever the key's scopes are (re)resolved (on success OR failure) -
        // consumers re-attempt their authenticated fetches here.
        void OnScopesChanged(std::function<void()> cb) { _scopeListeners.push_back(std::move(cb)); }

        V2::Endpoints V2() { return V2::Endpoints{&_conn}; }
        V1::Endpoints V1() { return V1::Endpoints{&_conn}; } // the legacy v1 surface (event locations, ...)
        Connection &Conn() { return _conn; }                 // generic GetJson escape hatch for un-wrapped endpoints

    private:
        void FireScopes();

        Connection _conn;
        bool _scopesResolving = false;
        std::vector<std::function<void()>> _scopeListeners;
    };
}
