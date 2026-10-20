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

/// Alpha discard is a material property for opaque draws, even when they share
/// one GPU pass. Transparent multipass rendering retains its pass-wide policy.
inline bool ShouldUseGreedyAlphaCutout(bool transparentPass,
                                       bool transparentPassAlphaCutout,
                                       bool materialAlphaCutout) noexcept
{
  return transparentPass ? transparentPassAlphaCutout : materialAlphaCutout;
}

GreedyDrawCategory GreedyDrawCategoryForBlock(const UBlockRegistry &registry,
                                              BlockId id);

} // namespace cutum
