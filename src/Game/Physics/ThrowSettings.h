#pragma once
#include <array>
#include <nlohmann/json.hpp>
#include <string>
namespace cutum
{
enum class ThrowCategory
{
  Block,
  Material,
  Throwable,
  Tool,
  Weapon,
  Food,
  Equipment,
  Utility,
  General,
  Count
};
enum class ThrowPreset
{
  Free,
  Restricted,
  Custom
};
inline constexpr std::array<const char *, 9> ThrowCategoryNames = {
    "block", "material",  "throwable", "tool",   "weapon",
    "food",  "equipment", "utility",   "general"};
inline ThrowCategory ThrowCategoryFromString(const std::string &s)
{
  for (size_t i = 0; i < ThrowCategoryNames.size(); ++i)
    if (s == ThrowCategoryNames[i])
      return static_cast<ThrowCategory>(i);
  return ThrowCategory::General;
}
struct ThrowSettings
{
  ThrowPreset preset{ThrowPreset::Free};
  unsigned allowed{511};
  bool Allows(ThrowCategory category) const
  {
    const auto bit = 1u << static_cast<unsigned>(category);
    return preset == ThrowPreset::Free ||
           (preset == ThrowPreset::Restricted ? (bit & 7u) != 0
                                              : (allowed & bit) != 0);
  }
};
inline void to_json(nlohmann::json &j, const ThrowSettings &v)
{
  j = {{"preset", v.preset == ThrowPreset::Restricted ? "restricted"
                  : v.preset == ThrowPreset::Custom   ? "custom"
                                                      : "free"},
       {"allowed", v.allowed & 511u}};
}
inline void from_json(const nlohmann::json &j, ThrowSettings &v)
{
  const auto p = j.value("preset", std::string("free"));
  v.preset = p == "restricted" ? ThrowPreset::Restricted
             : p == "custom"   ? ThrowPreset::Custom
                               : ThrowPreset::Free;
  v.allowed = j.value("allowed", 511u) & 511u;
}
} // namespace cutum
