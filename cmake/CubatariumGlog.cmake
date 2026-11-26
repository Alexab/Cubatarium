# Windows desktop glog 0.7.1 with exception-safe ownership of LogMessageData.
# Same upstream API/version as the static vcpkg dependency. Keep the installed
# SDK immutable;build the pinned patched source as a normal target dependency.
include(FetchContent)
set(CUBATARIUM_GLOG_ARCHIVE "" CACHE FILEPATH "Optional local copy of pinned glog0.7.1 source archive")
set(_cuba_glog_url "https://codeload.github.com/google/glog/tar.gz/refs/tags/v0.7.1")
if(CUBATARIUM_GLOG_ARCHIVE)
  if(NOT EXISTS "${CUBATARIUM_GLOG_ARCHIVE}")
    message(FATAL_ERROR "CUBATARIUM_GLOG_ARCHIVE does not exist: ${CUBATARIUM_GLOG_ARCHIVE}")
  endif()
  set(_cuba_glog_url "${CUBATARIUM_GLOG_ARCHIVE}")
endif()
set(_cuba_glog_archive_hash "00e4a87e87b7e7612f519a41e491f16623b12423620006f59f5688bfd8d13b08")
set(_cuba_glog_patch_hash "58cc9e03225f0699d98fda492ce0b872d369af15002051215e66bf565379bf8b")
set(_cuba_glog_patch_script "${CMAKE_CURRENT_LIST_DIR}/PatchGlogLifetime.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cuba_glog_patch_script}")
function(cubatarium_add_lifetime_glog)
  set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  # Normal variables scope these dependency options to this function/child.
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_TESTING OFF)
  set(WITH_GFLAGS OFF)
  set(WITH_GTEST OFF)
  set(WITH_GMOCK OFF)
  FetchContent_Declare(cubatarium_glog URL "${_cuba_glog_url}"
      URL_HASH "SHA256=${_cuba_glog_archive_hash}")
  FetchContent_GetProperties(cubatarium_glog)
  if(NOT cubatarium_glog_POPULATED)
    FetchContent_Populate(cubatarium_glog)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${cubatarium_glog_SOURCE_DIR}"
      "-DEXPECTED_PATCH_HASH=${_cuba_glog_patch_hash}" -P "${_cuba_glog_patch_script}"
      RESULT_VARIABLE _patched)
  if(NOT _patched EQUAL 0)
    message(FATAL_ERROR "Pinned glog lifetime source preparation failed")
  endif()
  add_subdirectory("${cubatarium_glog_SOURCE_DIR}" "${cubatarium_glog_BINARY_DIR}" EXCLUDE_FROM_ALL)
  # Match the installed vcpkg target's public MSVC language-mode option.
  target_compile_options(glog PUBLIC "$<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:/Zc:__cplusplus>")
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/glog-lifetime-provenance-$<CONFIG>.json"
      CONTENT "{\n  \"schema\": \"glog-lifetime-source.v1\",\n  \"upstream_version\": \"0.7.1\",\n  \"archive_sha256\": \"${_cuba_glog_archive_hash}\",\n  \"patched_logging_sha256\": \"${_cuba_glog_patch_hash}\",\n  \"source\": \"${cubatarium_glog_SOURCE_DIR}/src/logging.cc\",\n  \"library\": \"$<TARGET_FILE:glog>\"\n}\n")
endfunction()
cubatarium_add_lifetime_glog()
