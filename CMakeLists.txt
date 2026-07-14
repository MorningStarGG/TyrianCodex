cmake_minimum_required(VERSION 3.20)
project(TyrianCodex VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# -- Nexus API headers (the aggregator <Nexus.h>) -----------------------------
FetchContent_Declare(nexus_api
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nexus_api)

# -- Dear ImGui -- MUST be v1.80 to match Nexus's bundled context -------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.80
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# -- nlohmann/json -- loads our data/zones/*.json datasets --------------------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# -- litehtml -- native HTML/CSS cascade + layout for the in-addon wiki reader.
# litehtml bundles gumbo (Apache-2.0) for HTML5 parsing; Tyrian Codex provides the ImGui draw container.
FetchContent_Declare(litehtml
    GIT_REPOSITORY https://github.com/litehtml/litehtml.git
    GIT_TAG        d96c0209f15624c3d3c82aa81c36032e87602cd9)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EXTERNAL_GUMBO OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(litehtml)

set(SOURCES
    src/entry.cpp
    src/resources/TyrianCodex.rc
    src/model/Dataset.cpp
    src/model/EventAreas.cpp
    src/model/EventTimers.cpp
    src/model/LoadingScreens.cpp
    src/util/Textures.cpp
    src/util/ImageCache.cpp
    src/util/OwnedTex.cpp
    src/util/TexPack.cpp
    src/app/DataBootstrap.cpp
    src/guide/TrailFollower.cpp
    src/guide/Progress.cpp
    src/guide/Travel.cpp
    src/guide/Story.cpp
    src/util/Dyes.cpp
    src/render/glyphs/Glyphs.cpp
    src/render/objectives/ObjectiveIcons.cpp
    src/render/route/RouteArt.cpp
    src/render/tasks/TaskIcons.cpp
    src/ui/gw2ui/Gw2UiInternal.cpp
    src/ui/gw2ui/Gw2UiWindow.cpp
    src/ui/gw2ui/Gw2UiCards.cpp
    src/ui/gw2ui/Gw2UiControls.cpp
    src/ui/gw2ui/Gw2UiText.cpp
    src/ui/gw2ui/Gw2UiWidgets.cpp
    src/ui/gw2ui/Gw2UiChart.cpp
    src/ui/gw2ui/Gw2UiGalleryRail.cpp
    src/ui/gw2ui/Gw2UiGallery.cpp
    src/ui/Effect.cpp
    src/ui/ApiReminder.cpp
    src/ui/welcome/WelcomeWindow.cpp
    src/ui/search/SearchModel.cpp
    src/ui/search/SearchRow.cpp
    src/ui/SettingsWindow.cpp
    src/ui/profiles/ProfileBar.cpp
    src/ui/profiles/ConfigProfiles.cpp
    src/ui/profiles/Loadouts.cpp
    src/ui/InfoHudShared.cpp
    src/ui/hud/Hud.cpp
    src/ui/hud/HudButtons.cpp
    src/ui/infopanel/InfoData.cpp
    src/ui/infopanel/InfoPanel.cpp
    src/ui/tabs/SettingsModel.cpp
    src/ui/tabs/SettingsCommon.cpp
    src/ui/tabs/MapThumbnail.cpp
    src/ui/tabs/OptionsTab.cpp
    src/ui/tabs/DiagnosticsSection.cpp
    src/ui/tabs/JournalTab.cpp
    src/ui/tabs/ChecklistTab.cpp
    src/ui/tabs/DungeonsTab.cpp
    src/ui/tabs/AtlasTab.cpp
    src/ui/tabs/InventorySection.cpp
    src/ui/tabs/ItemsTab.cpp
    src/ui/tabs/CollectionsTab.cpp
    src/ui/tabs/CosmeticsSection.cpp
    src/ui/tabs/RecipesSection.cpp
    src/ui/tabs/LegendaryArmorySection.cpp
    src/ui/tabs/WardrobeSection.cpp
    src/ui/tabs/HomesteadSection.cpp
    src/ui/items/ItemRender.cpp
    src/ui/items/ItemTpDetail.cpp
    src/ui/tabs/AboutTab.cpp
    src/ui/tabs/HubTab.cpp
    src/ui/tabs/TimersTab.cpp
    src/ui/tabs/TradingPostTab.cpp
    src/ui/tabs/SessionTab.cpp
    src/ui/tabs/FishingSection.cpp
    src/ui/GuideViewer.cpp
    src/ui/viewer/ViewerComponents.cpp
    src/ui/viewer/ViewerTaskChips.cpp
    src/ui/viewer/ViewerActions.cpp
    src/ui/viewer/ViewerHeader.cpp
    src/ui/viewer/ViewerObjectiveList.cpp
    src/ui/viewer/ViewerPanels.cpp
    src/ui/viewer/ViewerModeBodies.cpp
    src/ui/viewer/ViewerLayout.cpp
    src/ui/viewer/ViewerModel.cpp
    src/ui/viewer/ViewerStepCompact.cpp
    src/ui/viewer/ViewerStepRich.cpp
    src/ui/viewer/ViewerFarming.cpp
    src/ui/viewer/ViewerEventPanels.cpp
    src/ui/dashboard/Notify.cpp
    src/ui/dashboard/Toast.cpp
    src/ui/dashboard/Dashboard.cpp
    src/ui/dashboard/widgets/WidgetRegistry.cpp
    src/ui/dashboard/widgets/WvwWidgets.cpp
    src/ui/dashboard/widgets/NotificationsWidget.cpp
    src/ui/dashboard/widgets/ZoneHeaderWidget.cpp
    src/ui/dashboard/widgets/CurrentObjectiveWidget.cpp
    src/ui/dashboard/widgets/ModeSwitchWidget.cpp
    src/ui/dashboard/widgets/FarmWidgetUtil.cpp
    src/ui/dashboard/widgets/ObjectiveListWidget.cpp
    src/ui/dashboard/widgets/ZoneProgressWidget.cpp
    src/ui/dashboard/widgets/EventsWidget.cpp
    src/ui/dashboard/widgets/ResetTimersWidget.cpp
    src/ui/dashboard/widgets/CharacterWidget.cpp
    src/ui/dashboard/widgets/QuickControlsWidget.cpp
    src/ui/dashboard/widgets/TravelTargetWidget.cpp
    src/ui/dashboard/widgets/RecommendedZonesWidget.cpp
    src/ui/dashboard/widgets/PersonalStoryWidget.cpp
    src/ui/dashboard/widgets/FavoriteWaypointsWidget.cpp
    src/ui/dashboard/widgets/SessionStatsWidget.cpp
    src/ui/dashboard/widgets/SessionEarningsWidget.cpp
    src/ui/dashboard/widgets/RouteArrowWidget.cpp
    src/ui/dashboard/widgets/NearestWaypointsWidget.cpp
    src/ui/dashboard/widgets/SearchWidget.cpp
    src/ui/dashboard/widgets/ItemsWidget.cpp
    src/ui/dashboard/widgets/QuickNotesWidget.cpp
    src/ui/dashboard/widgets/WorldCompletionWidget.cpp
    src/ui/dashboard/widgets/CalculatorWidget.cpp
    src/ui/dashboard/widgets/LoadoutsWidget.cpp
    src/ui/dashboard/widgets/CraftingCartWidget.cpp
    src/app/AccountData.cpp
    src/app/StaticData.cpp
    src/app/SectorData.cpp
    src/app/ItemCatalog.cpp
    src/app/SkinCatalog.cpp
    src/app/CosmeticCatalog.cpp
    src/app/CraftingEngine.cpp
    src/app/CraftKnown.cpp
    src/app/CraftCart.cpp
    src/app/CraftCartSummary.cpp
    src/app/MarketData.cpp
    src/app/PriceHistory.cpp
    src/app/TpEligibility.cpp
    src/app/TpPrices.cpp
    src/app/AchievementPoints.cpp
    src/app/ItemAttributes.cpp
    src/app/SessionHistory.cpp
    src/app/SessionTracker.cpp
    src/app/AccountLevels.cpp
    src/app/CharRegistry.cpp
    src/app/CharData.cpp
    src/app/FishData.cpp
    src/app/ContentData.cpp
    src/app/DecorationCatalog.cpp
    src/app/wiki/WikiService.cpp
    src/ui/dashboard/widgets/WalletWidget.cpp
    src/ui/dashboard/widgets/TodaysDailiesWidget.cpp
    src/ui/dashboard/widgets/TrackedAchievementsWidget.cpp
    src/ui/dashboard/widgets/MasteryWidget.cpp
    src/ui/dashboard/widgets/TradingPostWidget.cpp
    src/ui/dashboard/widgets/LoginRewardsWidget.cpp
    src/ui/dashboard/widgets/CharactersOverviewWidget.cpp
    src/ui/dashboard/widgets/InventoryWidget.cpp
    src/ui/dashboard/widgets/AltEquipmentWidget.cpp
    src/ui/dashboard/widgets/AccountBankWidget.cpp
    src/ui/StoryRecommend.cpp
    src/ui/TrayMenu.cpp
    src/ui/QuickMenu.cpp
    src/ui/WaypointFavorites.cpp
    src/ui/wiki/WikiHtmlSanitizer.cpp
    src/ui/wiki/WikiStyleAdapter.cpp
    src/ui/wiki/WikiContainer.cpp
    src/ui/wiki/WikiQuoteFont.cpp
    src/ui/wiki/WikiNativeRail.cpp
    src/ui/wiki/WikiReader.cpp
    src/ui/ZoneRow.cpp
    src/ui/zonedisplay/ZoneDisplay.cpp
    src/util/Draw.cpp
    src/util/Trading.cpp
    src/util/Json.cpp
    src/world/TrailRenderer.cpp
    src/world/Markers.cpp
    src/world/CategoryMarkers.cpp
    src/world/FishMarkers.cpp
    src/world/PersonalMarker.cpp
    src/world/GpsArrow.cpp
    src/util/DpiWatcher.cpp
    src/world/MapRenderer.cpp
    src/guide/CharacterLevel.cpp
    src/guide/StoryCompletion.cpp
    src/guide/Guidance.cpp
    src/guide/VistaWatch.cpp
    src/guide/InstanceGuidance.cpp
    src/guide/FarmingGuidance.cpp
    src/guide/FishingGuidance.cpp
    src/guide/FishHoles.cpp
    src/guide/ZoneManager.cpp

    # GW2 Web API client framework (src/api/): a self-contained Gw2Sharp-style client.
    src/api/core/Http.cpp
    src/api/core/Cache.cpp
    src/api/core/RateLimiter.cpp
    src/api/core/Middleware.cpp
    src/api/core/Connection.cpp
    src/api/Client.cpp
    src/api/v2/TokenInfo.cpp
    src/api/v2/Characters.cpp
    src/api/v2/Account.cpp
    src/api/v2/Maps.cpp
    src/api/v2/Stories.cpp
    src/api/v2/Achievements.cpp
    src/api/v2/Items.cpp
    # World/profession/wardrobe/item reference families are now one header-only module per root
    # (Worlds.h, Colors.h, ... Mounts.h, Continents.h, ...) - no .cpp; inline parse.
    src/api/v2/Commerce.cpp
    src/api/v2/Guild.cpp
    src/api/v2/Pvp.cpp
    src/api/v2/Wvw.cpp

    # ImGui compiled INTO this DLL; context shared with Nexus at runtime.
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

add_library(TyrianCodex SHARED ${SOURCES})

target_include_directories(TyrianCodex PRIVATE
    src
    third_party/stb          # vendored stb_image.h (addon-owned texture decode, src/util/OwnedTex.cpp)
    ${nexus_api_SOURCE_DIR}
    ${imgui_SOURCE_DIR})

target_link_libraries(TyrianCodex PRIVATE
    nlohmann_json::nlohmann_json
    litehtml
    winhttp       # GW2 Web API client transport (src/api/core/Http.cpp)
    shlwapi       # path helpers
    d3d11         # ScrollingHighlight effect: bind a custom PS on Nexus's device
    d3dcompiler)  # compile menuitem.fx at runtime

if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(TyrianCodex PRIVATE
        /W3 /WX- /MP /EHsc /permissive-
        /wd4100   # unreferenced formal parameter (imgui / callbacks)
        /wd4189)  # local var initialised but not referenced
    target_compile_definitions(TyrianCodex PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_options(TyrianCodex PRIVATE /SUBSYSTEM:WINDOWS)
endif()

set_target_properties(TyrianCodex PROPERTIES
    PREFIX ""           # output is TyrianCodex.dll (= target name; no lib prefix, explicit .dll suffix)
    SUFFIX ".dll")

# -- Bundled-asset refresh on a version bump ----------------------------------
# Keyed to src/Version.h: when you bump the addon version, this re-runs cmake/refresh_bundle.cmake, which
# ETag-checks the bundled map tiles and re-pulls only the ones that changed -- but ONLY on a machine that
# has the private builder/. A public checkout / CI runner has no builder/, so it is a clean no-op (the build
# uses the committed data/). A normal build (Version.h unchanged) never runs it: the stamp stays up to date.
set(_bundle_stamp "${CMAKE_BINARY_DIR}/bundle_refresh.stamp")
add_custom_command(
    OUTPUT  "${_bundle_stamp}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h"
    COMMAND ${CMAKE_COMMAND} -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/refresh_bundle.cmake
    COMMAND ${CMAKE_COMMAND} -E touch "${_bundle_stamp}"
    COMMENT "Bundled-asset check (src/Version.h changed)"
    VERBATIM)
add_custom_target(tc_refresh_bundle DEPENDS "${_bundle_stamp}")
add_dependencies(TyrianCodex tc_refresh_bundle)

# -- Auto-deploy into the Nexus addons folder for the hot-reload dev loop ------
# (Disable/unload the addon in Nexus before rebuilding, or the copy hits a locked DLL.)
set(GW2_ADDONS_DIR "D:/Games/Guild Wars 2/addons" CACHE PATH "Nexus addons dir to deploy the built DLL into")
if(EXISTS "${GW2_ADDONS_DIR}")
    add_custom_command(TARGET TyrianCodex POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DSRC=$<TARGET_FILE:TyrianCodex>
                -DDST_DLL=${GW2_ADDONS_DIR}/TyrianCodex.dll
                -DDATA_SRC=${CMAKE_CURRENT_SOURCE_DIR}/data
                -DDATA_DST=${GW2_ADDONS_DIR}/TyrianCodex/data
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy.cmake
        COMMENT "Best-effort deploy TyrianCodex.dll + data -> ${GW2_ADDONS_DIR} (skips if locked)")
endif()
