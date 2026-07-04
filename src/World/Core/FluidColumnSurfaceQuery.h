#ifndef FLUIDCOLUMNSURFACEQUERY_H
#define FLUIDCOLUMNSURFACEQUERY_H

#include "World/Math/BlockTypes.h"

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct FluidColumnSurface
{
  BlockId fluidId{BLOCK_AIR};
  float surfaceY{0.0f};
  int surfaceBlockY{0};
  bool valid{false};
};

/// Topmost liquid block with Fluid render style in column (bx, bz).
FluidColumnSurface FindFluidColumnSurfaceAt(const UBlockWorld &world,
                                            const UBlockRegistry &registry,
                                            int bx, int bz, int hintY,
                                            int scanUp = 32, int scanDown = 64);

} // namespace cutum

#endif
