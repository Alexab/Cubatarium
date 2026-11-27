# Keep the installed nlohmann package unchanged. Terrain parsing returns a
# validity result and does not use the lexer's raw text for error messages.
get_target_property(_json_includes nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
foreach(_json_include IN LISTS _json_includes)
    if(EXISTS "${_json_include}/nlohmann/detail/input/lexer.hpp")
        set(_json_lexer "${_json_include}/nlohmann/detail/input/lexer.hpp")
        break()
    endif()
endforeach()
if(NOT _json_lexer OR NOT nlohmann_json_VERSION VERSION_EQUAL "3.12.0")
    return()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_json_lexer}")
file(READ "${_json_lexer}" _lexer_source)
string(REPLACE "\r\n" "\n" _lexer_source "${_lexer_source}")

function(cubatarium_replace_lexer_text before after)
    string(FIND "${_lexer_source}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Unrecognized nlohmann 3.12 lexer layout at ${_json_lexer}")
    endif()
    string(REPLACE "${before}" "${after}" _lexer_source "${_lexer_source}")
    set(_lexer_source "${_lexer_source}" PARENT_SCOPE)
endfunction()

cubatarium_replace_lexer_text(
    "    std::vector<char_type> token_string {};"
    [=[    std::vector<char_type> token_string {};

    template<class Input, class = void>
    struct collect_raw_text : std::true_type {};

    template<class Input>
    struct collect_raw_text<Input, std::void_t<decltype(Input::cutum_collect_raw_lexer_diagnostics)>>
        : std::bool_constant<Input::cutum_collect_raw_lexer_diagnostics> {};

    static constexpr bool collects_raw_text = collect_raw_text<InputAdapterType>::value;]=])
cubatarium_replace_lexer_text(
    "        token_string.push_back(char_traits<char_type>::to_char_type(current));"
    "        if constexpr (collects_raw_text) token_string.push_back(char_traits<char_type>::to_char_type(current));")
cubatarium_replace_lexer_text(
    "            JSON_ASSERT(!token_string.empty());\n            token_string.pop_back();"
    [=[            if constexpr (collects_raw_text)
            {
                JSON_ASSERT(!token_string.empty());
                token_string.pop_back();
            }]=])

set(_lexer_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/json-headers")
set(_lexer_out "${_lexer_dir}/nlohmann/detail/input/lexer.hpp")
file(MAKE_DIRECTORY "${_lexer_dir}/nlohmann/detail/input")
set(_existing_lexer "")
if(EXISTS "${_lexer_out}")
    file(READ "${_lexer_out}" _existing_lexer)
endif()
if(NOT _existing_lexer STREQUAL _lexer_source)
    file(WRITE "${_lexer_out}" "${_lexer_source}")
endif()
# A normal include directory must precede vcpkg's external include path in
# MSBuild. All project targets see the same template definition.
include_directories(BEFORE "${_lexer_dir}")
add_compile_definitions(CUBATARIUM_JSON_LEXER_OVERRIDES)
