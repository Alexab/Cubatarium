#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>

namespace cutum
{
// Opt-in fault exercise, never an ownership or correctness certificate. One
// cancellation after real focus travel, a new AIR install and a staged result.
class DiskAirCancellationProbe
{
public:
  explicit DiskAirCancellationProbe(std::optional<glm::ivec2> target = {})
      : Target(target)
  {
  }

  static std::optional<glm::ivec2> Parse(const char *value)
  {
    if (!value)
      return {};
    const std::string_view text(value);
    const auto comma = text.find(',');
    if (comma == std::string_view::npos)
      return {};
    glm::ivec2 target;
    const auto x = std::from_chars(text.data(), text.data() + comma, target.x);
    const auto z = std::from_chars(text.data() + comma + 1,
                                   text.data() + text.size(), target.y);
    if (x.ec != std::errc{} || z.ec != std::errc{} ||
        x.ptr != text.data() + comma || z.ptr != text.data() + text.size())
      return {};
    return target;
  }
  bool Enabled() const { return Target.has_value(); }
  void ObserveFocus(glm::ivec3 focus)
  {
    if (Target)
    {
      const auto distance = [](int a, int b)
      {
        const int64_t difference = int64_t(a) - b;
        return difference < 0 ? -difference : difference;
      };
      WasFar |= std::max(distance(focus.x, Target->x),
                         distance(focus.z, Target->y)) >= 16;
    }
  }
  bool TryCancel(glm::ivec3 column, bool installed_air, bool geometry,
                 int remaining, bool staged_result)
  {
    if (!Target || Fired || !WasFar || !installed_air || geometry ||
        remaining <= 0 || !staged_result ||
        glm::ivec2(column.x, column.z) != *Target)
      return false;
    Fired = true;
    return true;
  }

private:
  std::optional<glm::ivec2> Target;
  bool WasFar{false}, Fired{false};
};
} // namespace cutum
