#pragma once

#include "Render/Mesh/GreedyMeshBatch.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UChunkMeshCache;
struct Frustum;

/// Visible-set / flat greedy refs producer. Bound once at init.
class IUChunkCull
{
public:
  virtual ~IUChunkCull() = default;

  virtual const char *BackendName() const = 0;

  /// Rebuild opaque/cutout + transparent refs for the current camera.
  virtual void RebuildVisible(UChunkMeshCache &cache, const Frustum *frustum,
                              const glm::vec3 *camera_pos,
                              float max_cull_distance) = 0;

  virtual bool SupportsGpuCull() const { return false; }
};

} // namespace cutum
