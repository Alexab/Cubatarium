#ifndef FLUIDCOLUMNSURFACEQUERY_H
#define FLUIDCOLUMNSURFACEQUERY_H

#include "World/Core/FluidSurfaceScanTuning.h"
#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct FluidColumnSurface
{
  BlockId fluidId{BLOCK_AIR};
  float surfaceY{0.0f};
  int surfaceBlockY{0};
  int bottomBlockY{0};
  bool valid{false};
};

/// Only the contiguous liquid interval at the eye (or immediately below it
/// within the surface transition band). Distant water behind rock is excluded.
FluidColumnSurface FindFluidColumnSurfaceForEye(const UBlockWorld &world,
                                                const UBlockRegistry &registry,
                                                const glm::vec3 &eye);

bool ContainsFluidAtHeight(const FluidColumnSurface &column, float eyeY);

/// Topmost liquid block with Fluid render style in column (bx, bz).
FluidColumnSurface
FindFluidColumnSurfaceAt(const UBlockWorld &world,
                         const UBlockRegistry &registry, int bx, int bz,
                         int hintY, int scanUp = FluidSurfaceScanTuning::ScanUp,
                         int scanDown = FluidSurfaceScanTuning::ScanDown);

/// Coarse column samples around (bx, bz) for fog/render proximity gating.
bool HasFluidSurfaceNear(const UBlockWorld &world,
                         const UBlockRegistry &registry, int bx, int bz,
                         int hintY, int radiusBlocks = 48);

} // namespace cutum

#endif
