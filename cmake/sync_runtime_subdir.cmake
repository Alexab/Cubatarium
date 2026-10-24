# Incrementally sync a runtime subdirectory from the source tree.
# Required: SYNC_SRC (directory), SYNC_DST (directory under runtime root).
# Optional: SYNC_EXCLUDE_PREFIX — skip top-level entries whose names start with this.
# Optional: SYNC_MIRROR — remove runtime-only files from this owned subtree.
#
# Incremental sync is the default so repeated Release builds stay fast. Exact
# mirroring is opt-in for a single owned subtree that must prune removed files.

if(NOT DEFINED SYNC_SRC OR NOT DEFINED SYNC_DST)
  message(FATAL_ERROR "sync_runtime_subdir.cmake requires SYNC_SRC and SYNC_DST")
endif()

if(NOT EXISTS "${SYNC_SRC}")
  message(STATUS "sync_runtime_subdir: source missing, skipping: ${SYNC_SRC}")
  return()
endif()

file(MAKE_DIRECTORY "${SYNC_DST}")

if(DEFINED SYNC_MIRROR AND SYNC_MIRROR)
  # Mirror only explicitly owned subtrees (for example one resource pack), not
  # the whole runtime resource_packs directory where users may keep local packs.
  if(WIN32)
    execute_process(
      COMMAND robocopy "${SYNC_SRC}" "${SYNC_DST}" /MIR /NFL /NDL /NJH /NJS /NP
      RESULT_VARIABLE _sync_rc
    )
    if(_sync_rc GREATER 7)
      message(FATAL_ERROR "sync_runtime_subdir: robocopy mirror failed (${_sync_rc})")
    endif()
  else()
    file(REMOVE_RECURSE "${SYNC_DST}")
    file(GLOB _sync_entries RELATIVE "${SYNC_SRC}" "${SYNC_SRC}/*")
    foreach(_sync_entry IN LISTS _sync_entries)
      set(_sync_item "${SYNC_SRC}/${_sync_entry}")
      file(COPY "${_sync_item}" DESTINATION "${SYNC_DST}")
    endforeach()
  endif()
elseif(WIN32 AND NOT DEFINED SYNC_EXCLUDE_PREFIX)
  # Fast path: only copy newer files (robocopy exit 0–7 = success).
  execute_process(
    COMMAND robocopy "${SYNC_SRC}" "${SYNC_DST}" /E /XO /NFL /NDL /NJH /NJS /NP
    RESULT_VARIABLE _sync_rc
  )
  if(_sync_rc GREATER 7)
    message(FATAL_ERROR "sync_runtime_subdir: robocopy failed (${_sync_rc})")
  endif()
else()
  file(GLOB _sync_entries RELATIVE "${SYNC_SRC}" "${SYNC_SRC}/*")
  foreach(_sync_entry IN LISTS _sync_entries)
    if(DEFINED SYNC_EXCLUDE_PREFIX AND _sync_entry MATCHES "^${SYNC_EXCLUDE_PREFIX}")
      message(STATUS "sync_runtime_subdir: skipping ${_sync_entry}")
      continue()
    endif()
    set(_sync_item "${SYNC_SRC}/${_sync_entry}")
    file(COPY "${_sync_item}" DESTINATION "${SYNC_DST}")
  endforeach()
endif()

get_filename_component(_sync_name "${SYNC_SRC}" NAME)
message(STATUS "Synced runtime tree: ${_sync_name} -> ${SYNC_DST}")
