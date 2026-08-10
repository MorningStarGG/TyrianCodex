#include "app/CharData.h"
#include "app/App.h"
#include "app/AccountData.h"
#include "app/AccountLevels.h"
#include "ui/SettingsWindow.h"        // SaveSettings
#include "ui/profiles/ProfileBar.h"   // Profiles::IProfileHost (RenameChar/PurgeChar/CollectCharNames)
#include "ui/profiles/Loadouts.h"

void CharData::OnCharRename(App& app, const std::string& fromName, const std::string& toName)
{
    if (fromName.empty() || toName.empty() || fromName == toName) return;

    // Accumulated data -> UNION (each store dedups + persists itself).
    app.progress.RenameChar(fromName, toName);
    app.sessionHistory.RenameChar(fromName, toName);
    app.storyStore.RenameChar(fromName, toName);
    app.characterLevel.RenameChar(fromName, toName);

    // Level ledger -> TRANSFER (move; discards any fresh new-char seed Reconcile may have just added for toName).
    AccountLevels::RenameChar(fromName, toName);

    // Account-data caches (inventory/equipment) -> move (otherwise they self-heal from the API).
    AccountData::RenameChar(fromName, toName);

    // Per-character profiles (4 families) + loadouts -> REPLACE (the renamed character's config wins). These live
    // in settings.json; mutate the in-memory stores, then SaveSettings persists them.
    for (int fam = 0; fam < Loadouts::FamCount; ++fam)
        if (Profiles::IProfileHost* h = Loadouts::FamilyHost(app, fam))
            h->RenameChar(fromName, toName);
    Loadouts::RenameChar(app, fromName, toName);

    // Inventory single-grid toggle (keyed "char:<name>" in Config).
    auto& inv = app.config.inventorySingleGridBySource;
    auto itF = inv.find("char:" + fromName);
    if (itF != inv.end()) { inv["char:" + toName] = itF->second; inv.erase(itF); }

    SaveSettings(app);   // persist the profile families + loadouts + config
}

void CharData::PurgeCharacter(App& app, const std::string& name, bool alsoLevels)
{
    if (name.empty()) return;
    if (name == app.state.currentChar) return;   // never wipe the active character (the UI also disables this)

    app.progress.PurgeChar(name);
    app.sessionHistory.PurgeChar(name);
    app.storyStore.PurgeChar(name);
    app.characterLevel.PurgeChar(name);
    AccountData::PurgeChar(name);
    if (alsoLevels) AccountLevels::PurgeChar(name);   // off by default: a deleted character's lifetime levels stay

    for (int fam = 0; fam < Loadouts::FamCount; ++fam)
        if (Profiles::IProfileHost* h = Loadouts::FamilyHost(app, fam))
            h->PurgeChar(name);
    Loadouts::PurgeChar(app, name);

    app.config.inventorySingleGridBySource.erase("char:" + name);

    SaveSettings(app);
}

std::vector<std::string> CharData::AllCharacterNames(App& app)
{
    std::set<std::string> s;
    app.progress.CollectCharNames(s);
    app.sessionHistory.CollectCharNames(s);
    app.storyStore.CollectCharNames(s);
    app.characterLevel.CollectCharNames(s);
    AccountLevels::CollectCharNames(s);
    AccountData::CollectCharNames(s);
    for (int fam = 0; fam < Loadouts::FamCount; ++fam)
        if (Profiles::IProfileHost* h = Loadouts::FamilyHost(app, fam))
            h->CollectCharNames(s);
    Loadouts::CollectCharNames(app, s);

    s.erase("default");
    s.erase("__template__");
    return std::vector<std::string>(s.begin(), s.end());
}
