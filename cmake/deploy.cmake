# Best-effort deploy into the Nexus addons folder. Uses execute_process WITHOUT checking the result, so a DLL
# locked by a running Nexus (addon still loaded) never fails the build.
#
# The loose texture dirs (items/tiles/maps/portraits/decorations/cats/skins) are build INPUTS: they get PACKED
# into data/textures/*.pack (which the runtime mmaps) and are gitignored. They must NEVER ship -- that's tens of
# thousands of loose PNGs (hundreds of MB). We do NOT copy data/ wholesale and then delete them (a ton of wasted
# I/O); we copy everything EXCEPT those source dirs, so the loose images are never written in the first place.

# 1) the DLL
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC}" "${DST_DLL}")

# 2) all of data/ EXCEPT data/textures (json catalogs, zones, instances, fonts, ...)
file(GLOB _data_entries LIST_DIRECTORIES true "${DATA_SRC}/*")
foreach(_e ${_data_entries})
  get_filename_component(_n "${_e}" NAME)
  if(NOT _n STREQUAL "textures")
    if(IS_DIRECTORY "${_e}")
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${_e}" "${DATA_DST}/${_n}")
    else()
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_e}" "${DATA_DST}/${_n}")
    endif()
  endif()
endforeach()

# 3) data/textures/: the .pack files + loose FILES + any REAL loose-texture dir (loading/markers/ui), but NEVER
#    the packed build-input source dirs -- those are the loose images we must not ship.
set(_packed_inputs items tiles maps portraits decorations cats skins cosmetic_icons cosmetic_renders)
file(GLOB _tex_entries LIST_DIRECTORIES true "${DATA_SRC}/textures/*")
foreach(_e ${_tex_entries})
  get_filename_component(_n "${_e}" NAME)
  if(IS_DIRECTORY "${_e}")
    list(FIND _packed_inputs "${_n}" _idx)
    if(_idx EQUAL -1)   # a real loose-texture dir (no pack) -> ship it
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${_e}" "${DATA_DST}/textures/${_n}")
    endif()
  else()              # a .pack (or a loose top-level texture file) -> ship it
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_e}" "${DATA_DST}/textures/${_n}")
  endif()
endforeach()

# 4) clean up any stale loose source dir left in the deployed addon by older deploys (remove-only; never copied).
foreach(_p ${_packed_inputs})
  execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${DATA_DST}/textures/${_p}")
endforeach()
