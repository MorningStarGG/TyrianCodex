# Best-effort deploy into the Nexus addons folder. Uses execute_process WITHOUT checking the result, so a DLL
# locked by a running Nexus (addon still loaded) never fails the build.
#
# The loose texture dirs (items/tiles/maps/portraits/decorations/cats/skins) are build INPUTS: they get PACKED
# into data/textures/*.pack (which the runtime mmaps) and are gitignored. They must NEVER ship -- that's tens of
# thousands of loose PNGs (hundreds of MB). We do NOT copy data/ wholesale and then delete them (a ton of wasted
# I/O); we copy everything EXCEPT those source dirs, so the loose images are never written in the first place.

# Shared with packaging, hydration and cleanup; includes loading -> loadingscreens.pack.
file(READ "${CMAKE_CURRENT_LIST_DIR}/texture_packs.json" _pack_definitions)
string(JSON _pack_count LENGTH "${_pack_definitions}")
math(EXPR _pack_last "${_pack_count} - 1")
set(_packed_inputs)
set(_pack_names)
foreach(_i RANGE 0 ${_pack_last})
  string(JSON _pack MEMBER "${_pack_definitions}" ${_i})
  string(JSON _source_dir GET "${_pack_definitions}" "${_pack}" sourceDir)
  if(NOT _pack MATCHES "^[a-z_]+$" OR NOT _source_dir MATCHES "^[a-z_]+$")
    message(FATAL_ERROR "Invalid texture pack directory: ${_pack}/${_source_dir}")
  endif()
  list(APPEND _pack_names "${_pack}")
  list(APPEND _packed_inputs "${_source_dir}")
endforeach()

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

# 3) data/textures/: the .pack files + loose FILES + runtime art (markers/ui), but NEVER
#    the packed build-input source dirs -- those are the loose images we must not ship.
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

# 4) Remove old loose sources only when their replacement pack was deployed intact.
file(REAL_PATH "${DATA_DST}" _data_root)
file(REAL_PATH "${DATA_DST}/textures" _texture_root)
cmake_path(IS_PREFIX _data_root "${_texture_root}" NORMALIZE _inside_data)
if(NOT _inside_data)
  message(FATAL_ERROR "Texture cleanup path escapes addon data: ${_texture_root}")
endif()
foreach(_pack IN LISTS _pack_names)
  string(JSON _source_dir GET "${_pack_definitions}" "${_pack}" sourceDir)
  set(_loose "${_texture_root}/${_source_dir}")
  if(NOT IS_DIRECTORY "${_loose}")
    continue()
  endif()
  file(REAL_PATH "${_loose}" _loose_real)
  if(IS_SYMLINK "${_loose}" OR NOT _loose_real STREQUAL _loose)
    message(WARNING "Skipping redirected texture source directory: ${_loose}")
    continue()
  endif()
  set(_src_pack "${DATA_SRC}/textures/${_pack}.pack")
  set(_dst_pack "${_texture_root}/${_pack}.pack")
  if(NOT EXISTS "${_src_pack}" OR NOT EXISTS "${_dst_pack}" OR IS_DIRECTORY "${_dst_pack}")
    message(WARNING "Keeping ${_loose}: replacement pack was not deployed")
    continue()
  endif()
  file(SHA256 "${_src_pack}" _src_hash)
  file(SHA256 "${_dst_pack}" _dst_hash)
  if(NOT _src_hash STREQUAL _dst_hash)
    message(WARNING "Keeping ${_loose}: deployed pack differs from source")
    continue()
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${_loose}" RESULT_VARIABLE _cleanup_result)
  if(NOT _cleanup_result EQUAL 0)
    message(WARNING "Failed to remove packed texture sources: ${_loose}")
  endif()
endforeach()
