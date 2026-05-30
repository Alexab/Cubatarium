#pragma once

#include "IWorldGenPipeline.h"

namespace cutum {

class FlatPipeline : public IWorldGenPipeline {
public:
 explicit FlatPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;
};

class LegacyHashPipeline : public IWorldGenPipeline {
public:
 explicit LegacyHashPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;
};

} // namespace cutum
