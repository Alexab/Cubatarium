#pragma once

#include "IWorldGenPipeline.h"

namespace cutum {

class UFlatPipeline : public IWorldGenPipeline {
public:
 explicit UFlatPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;
};

class ULegacyHashPipeline : public IWorldGenPipeline {
public:
 explicit ULegacyHashPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;
};

} // namespace cutum
