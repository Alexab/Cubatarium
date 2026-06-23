# Resolves CUBATARIUM_VERSION_STRING for configure_file(Version.h.in).
# - Exact semver tag on HEAD -> tag name (optional leading v)
# - Else -> {nearest_tag}-{branch}-{short_hash}
# - No git / no tags -> PROJECT_VERSION

function(_cubatarium_git_run GIT_EXE WORK_DIR OUT_RC OUT_STDOUT)
    set(_args ${ARGN})
    execute_process(
        COMMAND ${GIT_EXE} ${_args}
        WORKING_DIRECTORY "${WORK_DIR}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc
    )
    set(${OUT_RC} "${_rc}" PARENT_SCOPE)
    set(${OUT_STDOUT} "${_stdout}" PARENT_SCOPE)
endfunction()

function(_cubatarium_normalize_semver_tag TAG OUT_SEMVER)
    set(_tag "${TAG}")
    if(_tag MATCHES "^v(.+)$")
        set(_tag "${CMAKE_MATCH_1}")
    endif()
    # Cubatarium tags: 0.0.1.1, 0.0.2.1b (optional letter suffix on fourth segment)
    if(_tag MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+[a-zA-Z]?$")
        set(${OUT_SEMVER} "${_tag}" PARENT_SCOPE)
    elseif(_tag MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        set(${OUT_SEMVER} "${_tag}" PARENT_SCOPE)
    else()
        set(${OUT_SEMVER} "" PARENT_SCOPE)
    endif()
endfunction()

function(_cubatarium_nearest_tag_name DESCRIBE_OUT OUT_TAG)
    set(_describe "${DESCRIBE_OUT}")
    _cubatarium_normalize_semver_tag("${_describe}" _semver)
    if(_semver)
        set(${OUT_TAG} "${_semver}" PARENT_SCOPE)
        return()
    endif()
    if(_describe MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+[a-zA-Z]?)-[0-9]+-g[0-9a-fA-F]+$")
        set(${OUT_TAG} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_describe MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)-[0-9]+-g[0-9a-fA-F]+$")
        set(${OUT_TAG} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_describe MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)-[0-9]+-g[0-9a-fA-F]+$")
        set(${OUT_TAG} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    string(FIND "${_describe}" "-" _dash)
    if(_dash GREATER 0)
        string(SUBSTRING "${_describe}" 0 ${_dash} _prefix)
        _cubatarium_normalize_semver_tag("${_prefix}" _prefix_semver)
        if(_prefix_semver)
            set(${OUT_TAG} "${_prefix_semver}" PARENT_SCOPE)
            return()
        endif()
    endif()
    set(${OUT_TAG} "" PARENT_SCOPE)
endfunction()

function(_cubatarium_sanitize_version_string VERSION OUT_SAFE)
    set(_safe "${VERSION}")
    string(REGEX REPLACE "\\\\" "_" _safe "${_safe}")
    string(REGEX REPLACE "\"" "_" _safe "${_safe}")
    string(REGEX REPLACE "[\r\n]" "" _safe "${_safe}")
    set(${OUT_SAFE} "${_safe}" PARENT_SCOPE)
endfunction()

function(cubatarium_resolve_version OUT_VAR)
    set(_fallback "${PROJECT_VERSION}")
    set(_resolved "${_fallback}")

    find_package(Git QUIET)
    if(NOT Git_FOUND)
        _cubatarium_sanitize_version_string("${_resolved}" _safe)
        set(${OUT_VAR} "${_safe}" PARENT_SCOPE)
        return()
    endif()

    _cubatarium_git_run("${GIT_EXECUTABLE}" "${CMAKE_SOURCE_DIR}" _rc _out rev-parse --is-inside-work-tree)
    if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "true")
        _cubatarium_sanitize_version_string("${_resolved}" _safe)
        set(${OUT_VAR} "${_safe}" PARENT_SCOPE)
        return()
    endif()

    _cubatarium_git_run("${GIT_EXECUTABLE}" "${CMAKE_SOURCE_DIR}" _rc _exact
        describe --tags --exact-match HEAD)
    if(_rc EQUAL 0)
        _cubatarium_normalize_semver_tag("${_exact}" _exact_semver)
        if(_exact_semver)
            set(_resolved "${_exact_semver}")
            _cubatarium_sanitize_version_string("${_resolved}" _safe)
            set(${OUT_VAR} "${_safe}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(_nearest_tag "")
    _cubatarium_git_run("${GIT_EXECUTABLE}" "${CMAKE_SOURCE_DIR}" _rc _describe
        describe --tags --abbrev=0 HEAD)
    if(_rc EQUAL 0)
        _cubatarium_nearest_tag_name("${_describe}" _nearest_tag)
    endif()

    _cubatarium_git_run("${GIT_EXECUTABLE}" "${CMAKE_SOURCE_DIR}" _rc _branch
        rev-parse --abbrev-ref HEAD)
    if(NOT _rc EQUAL 0)
        set(_branch "unknown")
    else()
        string(REPLACE "/" "-" _branch "${_branch}")
    endif()

    _cubatarium_git_run("${GIT_EXECUTABLE}" "${CMAKE_SOURCE_DIR}" _rc _hash rev-parse --short=7 HEAD)
    if(NOT _rc EQUAL 0)
        set(_hash "0000000")
    endif()

    if(_nearest_tag)
        set(_resolved "${_nearest_tag}-${_branch}-${_hash}")
    else()
        set(_resolved "${_fallback}")
    endif()

    _cubatarium_sanitize_version_string("${_resolved}" _safe)
    set(${OUT_VAR} "${_safe}" PARENT_SCOPE)
endfunction()
