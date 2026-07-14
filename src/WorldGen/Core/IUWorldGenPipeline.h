#pragma once

#include "WorldGen/Core/WorldGenContext.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

using WorldGenColumnProgressFn = std::function<void(int done, int total)>;

class IUWorldGenPipeline
{
public:
  virtual ~IUWorldGenPipeline() = default;

  virtual void GenerateColumn(int worldX, int worldZ) = 0;
  virtual int SurfaceYAt(int worldX, int worldZ) const = 0;

  virtual glm::vec3 DefaultSpawnPosition(int worldX, int worldZ,
                                         float eyeHeight = 1.62f) const;
  glm::vec3 ResolvePlayerSpawnPosition(const UBlockWorld &world,
                                       UBlockRegistry &registry,
                                       int centerX = 0, int centerZ = 0,
                                       float eyeHeight = 1.62f) const;

  const ProceduralSettings &GetProceduralSettings() const { return Ctx.Settings; }
  virtual void GenerateSpawnPatch(int centerX, int centerZ, int radiusBlocks,
                                  WorldGenColumnProgressFn onProgress = nullptr);
  virtual void GenerateFullPatch(int centerX, int centerZ, int halfExtent,
                                 WorldGenColumnProgressFn onProgress = nullptr);

protected:
  explicit IUWorldGenPipeline(WorldGenContext ctx);
  WorldGenContext Ctx;
};

class UProceduralWorldGenFactory
{
public:
  static std::unique_ptr<IUWorldGenPipeline> Create(WorldGenContext ctx);
};

} // namespace cutum
