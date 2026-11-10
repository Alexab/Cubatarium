# Script mode (cmake -P): regenerates Version.h from git on every build so new
# commits/tags show up without a reconfigure. The header is rewritten only when
# the version string changes, so an unchanged tree triggers no recompilation.
#
# Required: -DSOURCE_DIR=... -DTEMPLATE=... -DOUTPUT=... -DFALLBACK_VERSION=...

include("${CMAKE_CURRENT_LIST_DIR}/GetVersionFromGit.cmake")
cubatarium_resolve_version(CUBATARIUM_VERSION_STRING "${SOURCE_DIR}" "${FALLBACK_VERSION}")
configure_file("${TEMPLATE}" "${OUTPUT}.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
