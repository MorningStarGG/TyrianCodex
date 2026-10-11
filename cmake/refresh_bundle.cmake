# Data generation is an explicit, validated builder operation. Compilation never
# refreshes sources or mutates the committed bundle behind its manifest.
if(NOT DEFINED REPO_ROOT)
    get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
message(STATUS "Bundled data: using committed data/. Run builder/build_all.py --patch to refresh; build_release.py validates the completion manifest.")
