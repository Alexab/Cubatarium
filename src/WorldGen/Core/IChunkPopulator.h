#pragma once

#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class UBlockRegistry;
class UObjectLibrary;
class IWorldGenPipeline;

struct ChunkPopulateRequest
{
  glm::ivec3 chunkCoord;
  ChunkGenerationToken token;
  ProceduralSettings settings;
  glm::ivec2 columnOrigin{0};
  bool hasColumnOrigin{false};
  std::function<bool()> shouldCancel;
};

struct ChunkPopulateResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  UChunkBuffer buffer;
};

class IChunkPopulator
{
public:
  virtual ~IChunkPopulator() = default;
  virtual ChunkPopulateResult Populate(const ChunkPopulateRequest &request) = 0;
};

class UPipelineChunkPopulator : public IChunkPopulator
{
public:
  UPipelineChunkPopulator(UBlockRegistry &registry, UObjectLibrary *prefabs,
                          std::string worldgenOwnerPackId);

  ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override;

private:
  UBlockRegistry &Registry;
  UObjectLibrary *Objects;
  std::string WorldgenOwnerPackId;
};

} // namespace cutum
