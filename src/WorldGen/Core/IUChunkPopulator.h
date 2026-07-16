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
class IUWorldGenPipeline;

struct ChunkPopulateRequest
{
  glm::ivec3 chunkCoord;
  ChunkGenerationToken token;
  ProceduralSettings settings;
  glm::ivec2 columnOrigin{0};
  bool hasColumnOrigin{false};
  UObjectLibrary *objects{nullptr};
  std::function<bool()> shouldCancel;
};

struct ChunkPopulateTiming
{
  double totalMs{0.0};
  double sampleMs{0.0};
  double terrainMs{0.0};
  double carveMs{0.0};
  double postMs{0.0};
  double sealMs{0.0};
};

struct ChunkPopulateDiagnostics
{
  static void Record(const ChunkPopulateTiming &timing);
  static ChunkPopulateTiming GetLast();
};

struct ChunkPopulateResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  UChunkBuffer buffer;
};

class IUChunkPopulator
{
public:
  virtual ~IUChunkPopulator() = default;
  virtual ChunkPopulateResult Populate(const ChunkPopulateRequest &request) = 0;
};

class UPipelineChunkPopulator : public IUChunkPopulator
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
