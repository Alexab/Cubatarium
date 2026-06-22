# Replace a runtime subdirectory with a fresh copy from the source tree.
# Required: SYNC_SRC (directory), SYNC_DST (directory under runtime root).

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

get_filename_component(_sync_parent "${SYNC_DST}" DIRECTORY)
get_filename_component(_sync_name "${SYNC_SRC}" NAME)
file(COPY "${SYNC_SRC}" DESTINATION "${_sync_parent}")
message(STATUS "Synced runtime tree: ${_sync_name} -> ${SYNC_DST}")
