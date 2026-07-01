#ifndef IU_GREEDY_TRANSPARENT_BACKEND_H
#define IU_GREEDY_TRANSPARENT_BACKEND_H

#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Pipeline/GreedyShaderMode.h"
#include "Render/Textures/TextureCube.h"

#include <cstdint>
#include <map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace cutum
{

class UBlockRegistry;

struct GreedyTransparentDrawContext
{
  const std::vector<GreedyMeshBatch> &allBatches;
  glm::mat4 viewProjection;
  uint64_t meshRevision;
  uint64_t cullRevision;
  glm::vec3 cameraPos;
  const UBlockRegistry &blockRegistry;
  const std::map<size_t, UTextureCube> &textures;
};

class IUGreedyTransparentBackend
{
public:
  virtual ~IUGreedyTransparentBackend() = default;
  virtual void PrepareTransparent(const GreedyTransparentDrawContext &ctx) = 0;
  virtual void DrawPreparedTransparent(GreedyShaderMode mode,
                                       float shellAlpha) = 0;
};

} // namespace cutum

#endif
