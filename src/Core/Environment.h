#pragma once

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace cutum
{

/// Treat common textual false values as disabled for environment feature flags.
/// In particular, setting a flag to "0" must not enable diagnostic work.
inline bool IsEnvironmentFlagEnabled(const char *name) noexcept
{
  if (!name)
  {
    return false;
  }
  const char *value = std::getenv(name);
  if (!value)
  {
    return false;
  }
  std::string_view text(value);
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())))
  {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())))
  {
    text.remove_suffix(1);
  }
  if (text.empty())
  {
    return false;
  }

  constexpr std::string_view false_values[] = {"0", "false", "no", "off"};
  for (const std::string_view false_value : false_values)
  {
    if (text.size() != false_value.size())
    {
      continue;
    }
    bool equal = true;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
      const char lower = static_cast<char>(
          std::tolower(static_cast<unsigned char>(text[i])));
      if (lower != false_value[i])
      {
        equal = false;
        break;
      }
    }
    if (equal)
    {
      return false;
    }
  }
  return true;
}

} // namespace cutum
