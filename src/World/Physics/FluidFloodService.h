#ifndef FLUIDFLOODSERVICE_H
#define FLUIDFLOODSERVICE_H

#include "World/Physics/FluidTuning.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UBlockDefinitionStorage;

struct FluidFloodOptions
{
  BlockId fluid_id{BLOCK_AIR};
  BlockId water_id{BLOCK_AIR};
  bool source_for_air{false};
  int max_passes{FluidTuning::FloodMaxPassesDefault};
  int sea_level{-1};
};

class UFluidFloodService
{
public:
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
  static int FloodBreakSiteFromWetNeighbors(
      UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
      glm::ivec3 break_pos, const FluidFloodOptions &options,
      std::vector<glm::ivec3> *out_changed = nullptr);
};

} // namespace cutum

#endif
