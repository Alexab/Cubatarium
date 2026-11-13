#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace cutum
{
class UWorld;
class UCreature;
struct CropState
{
  glm::ivec3 ground{};
  float growth{0}, moisture{0};
};
struct ShearedState
{
  uint64_t creature{0};
  float remaining{300};
};
class USurvivalSystems
{
public:
  static USurvivalSystems &Instance();
  // True means the action was handled, including a refusal with an explanation.
  bool Use(UWorld &, UCreature &, glm::vec3 eye, glm::vec3 direction,
           std::string &reason);
  void Tick(UWorld &, float dt);
  void Serialize(nlohmann::json &) const;
  void Deserialize(const nlohmann::json &);
  void Clear();
  bool NearStation(UWorld &, glm::vec3 position,
                   const std::string &station) const;
  void BlockDrops(UWorld &, glm::ivec3 position, const std::string &id);

private:
  std::vector<CropState> Crops;
  std::vector<ShearedState> Sheared;
  float TickDebt{0};
};
} // namespace cutum
