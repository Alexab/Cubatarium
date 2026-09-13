#pragma once

#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
struct BlockDefinitionCatalog;
struct ChunkMeshSnapshot;

/// Pluggable chunk mesher. Bound once at init (CPU or GPU implementation).
class IUChunkMesher
{
public:
  virtual ~IUChunkMesher() = default;

  virtual const char *BackendName() const = 0;

  virtual std::vector<GreedyQuad>
  BuildChunkMesh(const UBlockWorld &world, glm::ivec3 chunk_coord,
                 UBlockRegistry &registry,
                 const BlockDefinitionCatalog *catalog = nullptr) = 0;

  virtual std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
                 const BlockDefinitionCatalog *catalog = nullptr) = 0;

  /// P5: worker may defer opaque extract to the main GL thread.
  /// When catalog is non-null (Q4 WorkerCompute), eligibility uses the pinned
  /// catalog only; registry remains fallback for main-thread callers.
  virtual bool CanDeferGpuExtract(
      const ChunkMeshSnapshot & /*snapshot*/, UBlockRegistry & /*registry*/,
      const BlockDefinitionCatalog * /*catalog*/ = nullptr) const
  {
    return false;
  }

  /// P5: main-thread GPU extract → batches. Default: not supported.
  /// When `deferred_no_gpu_readback`, CPU reference extract (no mask readback).
  virtual bool TryExtractOpaqueToBatches(
      const ChunkMeshSnapshot & /*snapshot*/, UBlockRegistry & /*registry*/,
      glm::ivec3 /*coord*/, std::vector<GreedyMeshBatch> & /*out_batches*/,
      bool /*deferred_no_gpu_readback*/ = false,
      bool /*greedy_merge_rects*/ = false)
  {
    return false;
  }
};

} // namespace cutum
