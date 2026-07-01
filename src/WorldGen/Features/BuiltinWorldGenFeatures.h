#pragma once

#include "WorldGen/Core/WorldGenStageId.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/ColumnSample.h"

namespace cutum
{

class IUBuiltinWorldGenFeature
{
public:
  virtual ~IUBuiltinWorldGenFeature() = default;
  virtual WorldGenStageId StageId() const = 0;
  virtual bool TryPlace(WorldGenContext &ctx, const ColumnSampleContext &sample,
                        int world_x, int world_z) const = 0;
};

const IUBuiltinWorldGenFeature *BuiltinWorldGenFeatureFor(WorldGenStageId id);

} // namespace cutum
