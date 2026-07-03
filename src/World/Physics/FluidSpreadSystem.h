#ifndef FLUIDSPREADSYSTEM_H
#define FLUIDSPREADSYSTEM_H

#include "World/Physics/FluidSpreadTypes.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace cutum
{

class UWorld;
class UBlockWorld;
class UBlockRegistry;
class UBlockDefinitionStorage;

struct FluidFloodOptions
{
  BlockId fluid_id{BLOCK_AIR};
  BlockId water_id{BLOCK_AIR};
  bool source_for_air{false};
  int max_passes{8};
};

class UFluidSpreadSystem
{
public:
  bool ShadowMode{true};

  static bool CellTouchesWet(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 pos);
  static BlockId ResolveFloodFluidId(
      const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
      glm::ivec3 pos, const FluidFloodOptions &options);
  static int FloodWetPocketsInBox(UBlockWorld &blockWorld,
                                  const UBlockDefinitionStorage &definitions,
                                  glm::ivec3 box_min, glm::ivec3 box_max,
                                  const FluidFloodOptions &options,
                                  std::vector<glm::ivec3> *out_changed = nullptr);
  static int FloodWetPocketsLocal(UBlockWorld &blockWorld,
                                  const UBlockDefinitionStorage &definitions,
                                  glm::ivec3 center, int radius,
                                  const FluidFloodOptions &options,
                                  std::vector<glm::ivec3> *out_changed = nullptr);

  static bool HasSpreadTarget(const UBlockWorld &world,
                              const UBlockDefinitionStorage &definitions,
                              glm::ivec3 block_pos);

  static bool CanReceiveFluid(const UBlockWorld &blockWorld,
                              const UBlockRegistry &registry,
                              glm::ivec3 pos);
  static bool ShouldReplaceBlockWithFluid(const UBlockWorld &blockWorld,
                                          const UBlockRegistry &registry,
                                          glm::ivec3 pos);
  static bool CanReceiveFluid(const UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              glm::ivec3 pos);
  static bool ShouldReplaceBlockWithFluid(const UBlockWorld &blockWorld,
                                          const UBlockDefinitionStorage &definitions,
                                          glm::ivec3 pos);

  FluidSpreadStats Tick(UWorld &world, glm::ivec3 block_pos);
  FluidSpreadStats TickBlock(UBlockWorld &blockWorld,
                             const UBlockRegistry &registry,
                             uint64_t physics_tick, glm::ivec3 block_pos);
  FluidSpreadStats TickBlock(UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             uint64_t physics_tick, glm::ivec3 block_pos);

  static bool ShouldProcessFluidTick(uint64_t physics_tick,
                                     glm::ivec3 block_pos, int spread_period);

private:
};

} // namespace cutum

#endif
