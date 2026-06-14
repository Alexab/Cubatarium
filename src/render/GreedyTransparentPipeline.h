#ifndef GREEDY_TRANSPARENT_PIPELINE_H
#define GREEDY_TRANSPARENT_PIPELINE_H

#include "render/GreedyTransparentSettings.h"
#include "render/IGreedyTransparentBackend.h"

namespace cutum {

class UGreedyTransparentPipeline {
public:
 static void Draw(IGreedyTransparentBackend& backend,
                  const GreedyTransparentDrawContext& ctx,
                  const GreedyTransparentSettings& settings = {});
};

} // namespace cutum

#endif
