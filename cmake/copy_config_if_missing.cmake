# Copies CONFIG_SRC to CONFIG_DST only when destination does not exist.
# Avoids post-build failures when bin/config.json is locked by a running game.

if(NOT DEFINED CONFIG_SRC OR NOT DEFINED CONFIG_DST)
 message(FATAL_ERROR "copy_config_if_missing.cmake requires CONFIG_SRC and CONFIG_DST")
endif()

if(NOT EXISTS "${CONFIG_SRC}")
 message(STATUS "Config template not found, skipping: ${CONFIG_SRC}")
 return()
endif()

if(EXISTS "${CONFIG_DST}")
 return()
endif()

get_filename_component(CONFIG_DST_DIR "${CONFIG_DST}" DIRECTORY)
file(MAKE_DIRECTORY "${CONFIG_DST_DIR}")
configure_file("${CONFIG_SRC}" "${CONFIG_DST}" COPYONLY)
message(STATUS "Seeded config: ${CONFIG_DST}")
