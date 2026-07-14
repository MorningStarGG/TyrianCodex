#pragma once

class App;

// The Collections-tab "Homestead" scope: a browser over the bundled Homestead collection catalogs
// (App::decorations / DecorationCatalog) joined with the account's owned sets (AccountData DomHomestead). A
// sub-view switcher over four collections -- Decorations (the rich primary), Glyphs, Cats, and gathering Nodes --
// with owned/missing status + counts, category filter, search, a collection-progress header, and click-to-open
// in the in-overlay wiki reader. Decorations render on the shared Gw2Ui::GalleryBrowser shell; Glyphs/Cats/Nodes
// on Gw2Ui::ChecklistBrowser.
void DrawHomesteadContent(App &app);
