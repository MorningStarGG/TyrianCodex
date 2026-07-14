# Run as a script (cmake -P) by the tc_refresh_bundle target whenever src/Version.h changes -- i.e. on a
# version bump. Best-effort bundled-data refresh:
#   * On a maintainer machine (the private builder/ is present + Python is found):
#       - re-pull any CHANGED map tiles into data/textures/tiles via an ETag-conditional check (304 = skip);
#       - re-check data/sectors.json (Zone Display offline names) against the live GW2 build id -- skips the
#         continents/floors walk when the game build is unchanged, regenerates only when it advanced.
#   * On a public checkout / CI runner (no builder/, or no Python): do NOTHING -- the build just uses the
#     committed data/. The DLL never depends on builder/ to compile.
# Always non-fatal: a network failure or a non-zero exit warns but never breaks the build.
if(NOT DEFINED REPO_ROOT)
    get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_assets "${REPO_ROOT}/builder/build_assets.py")
if(NOT EXISTS "${_assets}")
    message(STATUS "refresh_bundle: no builder/build_assets.py (public checkout / CI) -- using committed data/.")
    return()
endif()

find_program(_python NAMES python python3)
if(NOT _python)
    message(STATUS "refresh_bundle: Python not found -- skipping bundled-tile refresh.")
    return()
endif()

message(STATUS "refresh_bundle: version changed -- checking bundled map tiles for updates (ETag-conditional)...")
execute_process(
    COMMAND "${_python}" "${_assets}" tiles --check
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(WARNING "refresh_bundle: tile check exited ${_rc} (non-fatal; build continues with current data/).")
endif()

# `tiles --check` only refreshes the LOOSE working tiles (data/textures/tiles); fold them into the shipped
# tiles.pack and remove the loose dir, so ONLY the pack is ever committed/deployed (the addon mmaps the pack;
# loose tiles are a transient build input, like the items pack). Without this a version bump leaves ~hundreds of
# loose tiles sitting next to the stale pack.
set(_texpacks "${REPO_ROOT}/builder/build_texpacks.py")
if(_rc EQUAL 0 AND EXISTS "${_texpacks}")   # only on a CLEAN tile check -- a partial/failed pull must not repack
    message(STATUS "refresh_bundle: re-packing tiles into data/textures/tiles.pack...")
    execute_process(
        COMMAND "${_python}" "${_texpacks}" tiles
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _rc_pack)
    if(NOT _rc_pack EQUAL 0)
        message(WARNING "refresh_bundle: tiles re-pack exited ${_rc_pack} (non-fatal; loose tiles left for manual packing).")
    else()
        file(REMOVE_RECURSE "${REPO_ROOT}/data/textures/tiles")   # packed -> drop the loose working dir (only the pack ships)
    endif()
endif()

# Zone Display offline names: regenerate data/sectors.json only if the GW2 game build advanced (build-id gate).
set(_sectors "${REPO_ROOT}/builder/build_sectors.py")
if(EXISTS "${_sectors}")
    message(STATUS "refresh_bundle: checking bundled sectors (data/sectors.json) against the live GW2 build id...")
    execute_process(
        COMMAND "${_python}" "${_sectors}" --check
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _rc_sec)
    if(NOT _rc_sec EQUAL 0)
        message(WARNING "refresh_bundle: sector check exited ${_rc_sec} (non-fatal; build continues with current data/).")
    endif()
endif()

# Total-AP table: regenerate data/achievement_points.json only if the GW2 game build advanced (build-id gate).
set(_achpts "${REPO_ROOT}/builder/build_achievement_points.py")
if(EXISTS "${_achpts}")
    message(STATUS "refresh_bundle: checking bundled achievement points (data/achievement_points.json) against the live GW2 build id...")
    execute_process(
        COMMAND "${_python}" "${_achpts}" --check
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _rc_ach)
    if(NOT _rc_ach EQUAL 0)
        message(WARNING "refresh_bundle: achievement-points check exited ${_rc_ach} (non-fatal; build continues with current data/).")
    endif()
endif()

# Story completion-time estimates: re-pull the community times from gw2storytimes.com into data/story_times.json
# (cheap -- a few REST calls; always fresh, no build-id gate, since the community averages drift over time).
set(_stimes "${REPO_ROOT}/builder/build_story_times.py")
if(EXISTS "${_stimes}")
    message(STATUS "refresh_bundle: refreshing bundled story-time estimates (data/story_times.json) from gw2storytimes.com...")
    execute_process(
        COMMAND "${_python}" "${_stimes}"
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _rc_st)
    if(NOT _rc_st EQUAL 0)
        message(WARNING "refresh_bundle: story-times refresh exited ${_rc_st} (non-fatal; build continues with current data/).")
    endif()
endif()

# Content rotations: re-sync the deterministic fractal/strike rotation lists into data/rotations.json (cheap -- the
# lists are embedded + a best-effort wiki scale-name fetch). Always re-run so a version bump keeps the rotations in
# step with the wiki if ArenaNet ever shifts a cycle (the runtime then computes "today" from the refreshed bundle).
set(_rot "${REPO_ROOT}/builder/build_rotations.py")
if(EXISTS "${_rot}")
    message(STATUS "refresh_bundle: refreshing bundled content rotations (data/rotations.json)...")
    execute_process(
        COMMAND "${_python}" "${_rot}"
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE _rc_rot)
    if(NOT _rc_rot EQUAL 0)
        message(WARNING "refresh_bundle: rotations refresh exited ${_rc_rot} (non-fatal; build continues with current data/).")
    endif()
endif()
