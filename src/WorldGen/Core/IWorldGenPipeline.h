#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class IWorldGenPipeline
{
public:
  virtual ~IWorldGenPipeline() = default;

  virtual void GenerateColumn(int worldX, int worldZ) = 0;
  virtual int SurfaceYAt(int worldX, int worldZ) const = 0;

  virtual glm::vec3 DefaultSpawnPosition(int worldX, int worldZ,
                                         float eyeHeight = 1.62f) const;
  virtual void GenerateSpawnPatch(int centerX, int centerZ, int radiusBlocks);
  virtual void GenerateFullPatch(int centerX, int centerZ, int halfExtent);

protected:
  explicit IWorldGenPipeline(WorldGenContext ctx);
  WorldGenContext ctx_;
};

class UProceduralWorldGenFactory
{
public:
  static std::unique_ptr<IWorldGenPipeline> Create(WorldGenContext ctx);
};

} // namespace cutum
