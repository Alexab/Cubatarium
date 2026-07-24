#ifndef FLUIDCOLUMNSURFACEQUERY_H
#define FLUIDCOLUMNSURFACEQUERY_H

#include "World/Math/BlockTypes.h"
#include "World/Core/FluidSurfaceScanTuning.h"

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

/// Topmost liquid block with Fluid render style in column (bx, bz).
FluidColumnSurface FindFluidColumnSurfaceAt(const UBlockWorld &world,
                                            const UBlockRegistry &registry,
                                            int bx, int bz, int hintY,
                                            int scanUp = FluidSurfaceScanTuning::ScanUp,
                                            int scanDown = FluidSurfaceScanTuning::ScanDown);

/// Coarse column samples around (bx, bz) for fog/render proximity gating.
bool HasFluidSurfaceNear(const UBlockWorld &world, const UBlockRegistry &registry,
                         int bx, int bz, int hintY, int radiusBlocks = 48);

} // namespace cutum

#endif
