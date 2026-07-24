#pragma once

#include "Blocks/BlockRegistry.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

enum class GreedyDrawCategory
{
  Opaque,
  Cutout,
  CrossCutout,
  TransparentFluid,
};

GreedyDrawCategory GreedyDrawCategoryForBlock(const UBlockRegistry &registry,
                                              BlockId id);

} // namespace cutum
