#ifndef GREEDY_TRANSPARENT_PIPELINE_H
#define GREEDY_TRANSPARENT_PIPELINE_H

#include "Render/Pipeline/GreedyTransparentSettings.h"
#include "Render/Pipeline/IUGreedyTransparentBackend.h"

namespace cutum
{

class UGreedyTransparentPipeline
{
public:
  static void Draw(IUGreedyTransparentBackend &backend,
                   const GreedyTransparentDrawContext &ctx,
                   const GreedyTransparentSettings &settings = {});
};

} // namespace cutum

#endif
