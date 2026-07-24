# Replace a runtime subdirectory with a fresh copy from the source tree.
# Required: SYNC_SRC (directory), SYNC_DST (directory under runtime root).
# Optional: SYNC_EXCLUDE_PREFIX — skip top-level entries whose names start with this.

if(NOT DEFINED SYNC_SRC OR NOT DEFINED SYNC_DST)
  message(FATAL_ERROR "sync_runtime_subdir.cmake requires SYNC_SRC and SYNC_DST")
endif()

if(NOT EXISTS "${SYNC_SRC}")
  message(STATUS "sync_runtime_subdir: source missing, skipping: ${SYNC_SRC}")
  return()
endif()

if(EXISTS "${SYNC_DST}")
  file(REMOVE_RECURSE "${SYNC_DST}")
endif()

file(MAKE_DIRECTORY "${SYNC_DST}")

file(GLOB _sync_entries RELATIVE "${SYNC_SRC}" "${SYNC_SRC}/*")
foreach(_sync_entry IN LISTS _sync_entries)
  if(DEFINED SYNC_EXCLUDE_PREFIX AND _sync_entry MATCHES "^${SYNC_EXCLUDE_PREFIX}")
    message(STATUS "sync_runtime_subdir: skipping ${_sync_entry}")
    continue()
  endif()
  set(_sync_item "${SYNC_SRC}/${_sync_entry}")
  file(COPY "${_sync_item}" DESTINATION "${SYNC_DST}")
endforeach()

get_filename_component(_sync_name "${SYNC_SRC}" NAME)
message(STATUS "Synced runtime tree: ${_sync_name} -> ${SYNC_DST}")
