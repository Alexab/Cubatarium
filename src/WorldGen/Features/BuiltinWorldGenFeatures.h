#pragma once

#include "WorldGen/Core/WorldGenStageId.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/ColumnSample.h"

namespace cutum
{

class IBuiltinWorldGenFeature
{
public:
  virtual ~IBuiltinWorldGenFeature() = default;
  virtual WorldGenStageId StageId() const = 0;
  virtual bool TryPlace(WorldGenContext &ctx, const ColumnSampleContext &sample,
                        int world_x, int world_z) const = 0;
};

const IBuiltinWorldGenFeature *BuiltinWorldGenFeatureFor(WorldGenStageId id);

} // namespace cutum
