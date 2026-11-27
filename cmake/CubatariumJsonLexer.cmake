# Generate a lexer for the terrain adapter's validity and bounded token policy.
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

    static constexpr bool collects_raw_text = collect_raw_text<InputAdapterType>::value;

    template<class Input, class = void>
    struct limit_tokens : std::false_type {};

    template<class Input>
    struct limit_tokens<Input, std::void_t<decltype(Input::cutum_numeric_token_limit)>> : std::true_type {};

    static constexpr bool limits_tokens = limit_tokens<InputAdapterType>::value;
    std::size_t terrain_token_limit = 0;
    bool terrain_token_truncated = false;
    bool terrain_after_colon = false;

    void limit_terrain_token(std::size_t limit) noexcept
    {
        terrain_token_limit = limit;
        terrain_token_truncated = false;
    }]=])
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

cubatarium_replace_lexer_text(
    "    token_type scan_string()\n    {"
    [=[    token_type scan_string(bool is_object_value)
    {
        if constexpr (limits_tokens) limit_terrain_token(ia.cutum_string_token_limit(is_object_value));]=])
cubatarium_replace_lexer_text(
    "        // reset token_buffer to store the number's bytes"
    [=[        if constexpr (limits_tokens) limit_terrain_token(InputAdapterType::cutum_numeric_token_limit);
        // reset token_buffer to store the number's bytes]=])
cubatarium_replace_lexer_text(
    "        // try to parse integers first and fall back to floats"
    [=[        if constexpr (limits_tokens)
        {
            if (terrain_token_truncated)
            {
                error_message = "terrain numeric token exceeds its byte limit";
                return token_type::parse_error;
            }
        }

        // try to parse integers first and fall back to floats]=])
cubatarium_replace_lexer_text(
    "        token_buffer.push_back(static_cast<typename string_t::value_type>(c));"
    [=[        if constexpr (limits_tokens)
        {
            if (token_buffer.size() == terrain_token_limit)
            {
                terrain_token_truncated = true;
                return;
            }
        }
        token_buffer.push_back(static_cast<typename string_t::value_type>(c));]=])
cubatarium_replace_lexer_text(
    "        switch (current)\n        {\n            // structural characters"
    [=[        bool is_object_value = false;
        if constexpr (limits_tokens)
        {
            is_object_value = terrain_after_colon;
            terrain_after_colon = current == ':';
        }
        switch (current)
        {
            // structural characters]=])
cubatarium_replace_lexer_text("return scan_string();" "return scan_string(is_object_value);")
cubatarium_replace_lexer_text(
    [=[            case '\0':
            case char_traits<char_type>::eof():]=]
    [=[            case '\0':
                if constexpr (limits_tokens)
                {
                    error_message = "invalid null byte in terrain JSON";
                    return token_type::parse_error;
                }
                return token_type::end_of_input;
            case char_traits<char_type>::eof():]=])

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
