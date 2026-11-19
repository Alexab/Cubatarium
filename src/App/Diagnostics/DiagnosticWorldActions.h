#pragma once

#include <glm/glm.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace cutum
{
struct DiagnosticWorldAction
{
  glm::ivec3 cell{0};
  std::string expected;
  std::optional<std::string> replacement;
};

// Separate from the movement controller. Admission is bounded and requires an
// explicitly named isolated world; mutations additionally require a return
// route.
inline std::vector<DiagnosticWorldAction>
ParseDiagnosticWorldActions(const nlohmann::json &route,
                            const std::string &world)
{
  std::vector<DiagnosticWorldAction> result;
  if (!route.contains("world_actions"))
    return result;
  const auto &plan = route.at("world_actions");
  if (plan.at("schema") != "engine-native-world-actions.v1" ||
      route.at("isolated_world") != world ||
      world.rfind("EngineSmoke_", 0) != 0 ||
      world.find_first_of("/\\:") != std::string::npos ||
      !plan.at("entries").is_array() || plan.at("entries").empty() ||
      plan.at("entries").size() > 8)
    throw std::runtime_error("invalid isolated world action plan");
  std::set<std::tuple<int, int, int>> cells;
  const auto material = [](const nlohmann::json &value)
  {
    if (!value.is_string())
      throw std::runtime_error("material must be a string");
    auto name = value.get<std::string>();
    if (name.empty() || name.size() > 128 ||
        name.find('\0') != std::string::npos)
      throw std::runtime_error("invalid material name");
    return name;
  };
  for (const auto &entry : plan.at("entries"))
  {
    DiagnosticWorldAction action;
    const auto &cell = entry.at("cell");
    if (!cell.is_array() || cell.size() != 3)
      throw std::runtime_error("action cell must have three integers");
    for (int axis = 0; axis < 3; ++axis)
    {
      const auto &value = cell.at(axis);
      if (!value.is_number_integer() ||
          (value.is_number_unsigned() &&
           value.get<uint64_t>() > uint64_t(std::numeric_limits<int>::max())) ||
          value.get<int64_t>() < std::numeric_limits<int>::min() ||
          value.get<int64_t>() > std::numeric_limits<int>::max())
        throw std::runtime_error("action cell outside int32");
      action.cell[axis] = value.get<int>();
    }
    if (!cells.emplace(action.cell.x, action.cell.y, action.cell.z).second)
      throw std::runtime_error("duplicate action cell");
    action.expected = material(entry.at("expected"));
    if (entry.contains("replacement"))
    {
      action.replacement = material(entry.at("replacement"));
      if (*action.replacement == action.expected ||
          route.value("purpose", "") != "round_trip")
        throw std::runtime_error(
            "mutation requires distinct material and return route");
    }
    result.push_back(std::move(action));
  }
  return result;
}
} // namespace cutum
