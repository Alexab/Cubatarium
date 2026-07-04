#ifndef FLUIDSPREADSYSTEM_H
#define FLUIDSPREADSYSTEM_H

#include "World/Physics/FluidTuning.h"
#include "World/Physics/FluidSpreadTypes.h"
#include "World/Math/FluidCellState.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace cutum
{

class UWorld;
class UBlockWorld;
class UBlockRegistry;
class UBlockDefinitionStorage;
class IUChunkMeshReader;

struct FluidFloodOptions
{
  BlockId fluid_id{BLOCK_AIR};
  BlockId water_id{BLOCK_AIR};
  bool source_for_air{false};
  int max_passes{FluidTuning::FloodMaxPassesDefault};
  int sea_level{-1};
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
  static BlockId ResolveWaterBlockId(
      const UBlockDefinitionStorage &definitions);
  static FluidKind FluidKindFromBlockId(
      const UBlockDefinitionStorage &definitions, BlockId id);
  static BlockId BlockIdFromFluidKind(
      const UBlockDefinitionStorage &definitions, FluidKind kind);
  static BlockId ResolveFluidBlockId(
      const UBlockWorld &blockWorld,
      const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos);
  static BlockId ResolveFluidBlockIdForMesh(
      const IUChunkMeshReader &reader,
      const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos);
  static void ApplyFluidFill(UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 pos, BlockId fluid_id,
                             FluidCellState state);
  static BlockId ResolveFluidKind(
      const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
      glm::ivec3 block_pos, BlockId block_id);
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
  static int FloodBreakSiteFromWetNeighbors(
      UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
      glm::ivec3 break_pos, const FluidFloodOptions &options,
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
                             uint64_t physics_tick, glm::ivec3 block_pos,
                             int sea_level = -1);

  static bool ShouldProcessFluidTick(uint64_t physics_tick,
                                     glm::ivec3 block_pos, int spread_period);

private:
};

} // namespace cutum

#endif
