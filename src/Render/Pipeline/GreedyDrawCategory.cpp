#include "Render/Pipeline/GreedyDrawCategory.h"

namespace cutum
{

GreedyDrawCategory GreedyDrawCategoryForBlock(const UBlockRegistry &registry,
                                              BlockId id)
{
  const BlockRenderStyle style = registry.GetRenderStyle(id);
  if (style == BlockRenderStyle::Cross)
  {
    return GreedyDrawCategory::CrossCutout;
  }
  if (style == BlockRenderStyle::Fluid)
  {
    return GreedyDrawCategory::TransparentFluid;
  }
  if (style == BlockRenderStyle::Cutout)
  {
    return GreedyDrawCategory::Cutout;
  }
  return GreedyDrawCategory::Opaque;
}

} // namespace cutum
