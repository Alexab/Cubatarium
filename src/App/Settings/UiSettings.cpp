#include "App/Settings/UiSettings.h"

#include <algorithm>
#include <cctype>

namespace cutum
{

namespace
{

std::string ToLowerAscii(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                 { return static_cast<char>(std::tolower(c)); });
  return s;
}

} // namespace

ControlScheme ControlSchemeFromString(const std::string &value)
{
  const std::string key = ToLowerAscii(value);
  if (key == "cubatarium")
  {
    return ControlScheme::Cubatarium;
  }
  if (key == "classic" || key == "minecraft") // "minecraft" deprecated alias
  {
    return ControlScheme::Classic;
  }
  return ControlScheme::Classic;
}

const char *ControlSchemeToString(ControlScheme scheme)
{
  switch (scheme)
  {
  case ControlScheme::Cubatarium:
    return "cubatarium";
  case ControlScheme::Classic:
  default:
    return "classic";
  }
}

} // namespace cutum
