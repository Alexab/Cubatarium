#ifndef COLOR_UTIL_H
#define COLOR_UTIL_H

#include <glm/glm.hpp>
#include <string>

namespace cutum
{

inline glm::vec3 ParseHexColor(const std::string &hex, glm::vec3 fallback)
{
  if (hex.size() != 7 || hex[0] != '#')
  {
    return fallback;
  }
  auto nib = [&](size_t i) -> int
  {
    const char c = hex[i];
    if (c >= '0' && c <= '9')
    {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
      return 10 + (c - 'A');
    }
    return 0;
  };
  return glm::vec3((nib(1) * 16 + nib(2)) / 255.0f,
                   (nib(3) * 16 + nib(4)) / 255.0f,
                   (nib(5) * 16 + nib(6)) / 255.0f);
}

} // namespace cutum

#endif
