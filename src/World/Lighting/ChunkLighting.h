#ifndef CHUNKLIGHTING_H
#define CHUNKLIGHTING_H

#include <glm/glm.hpp>
#include <vector>
#include "World/Lighting/GpuLightTransport.h"

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

// Synchronous caller-thread selection only. Async snapshot jobs never consult
// this scope. The backend itself requires a current compute context or falls
// back to CPU without publishing partial output.
inline thread_local GpuLightTransportExecutor SyncRelightTransport = nullptr;
class ScopedSyncRelightTransport
{
public:
  explicit ScopedSyncRelightTransport(GpuLightTransportExecutor executor)
      : Previous(SyncRelightTransport) { SyncRelightTransport = executor; }
  ~ScopedSyncRelightTransport() { SyncRelightTransport = Previous; }
  ScopedSyncRelightTransport(const ScopedSyncRelightTransport &) = delete;
  ScopedSyncRelightTransport &operator=(const ScopedSyncRelightTransport &) = delete;
private:
  GpuLightTransportExecutor Previous;
};

constexpr int kRelightFrontierIterationsFull = 4;
constexpr int kRelightFrontierIterationsEdit = 2;
constexpr int kRelightFrontierIterationsBulk = 1;
/// Terrain column async job: light the center column only (Starlight edge).
constexpr int kRelightFrontierIterationsColumn = 0;

struct RelightFrontierOutcome
{
  std::vector<glm::ivec3> relit_chunks;
  bool frontier_unfinished{false};
};

void RelightChunk(UBlockWorld &world, UBlockRegistry &registry,
                  glm::ivec3 chunk_coord, bool include_block_light = true,
                  bool include_skylight = true);
void RelightChunkBlockLight(UBlockWorld &world, UBlockRegistry &registry,
                            glm::ivec3 chunk_coord);
void RelightChunksAround(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 block_pos, int max_world_y);
void RelightBlocksAroundLocal(UBlockWorld &world, UBlockRegistry &registry,
                              const std::vector<glm::ivec3> &block_positions,
                              bool clear_first = true);
/// Dig/place: remove+flood sky/block around edits (radius ≤15). No chunk Clear.
/// Call after SetBlock, before Immediate mesh so faces sample correct light.
void RelightBlocksAroundEdit(UBlockWorld &world, UBlockRegistry &registry,
                             const std::vector<glm::ivec3> &block_positions);
std::vector<glm::ivec3>
RelightBlocksAroundAll(UBlockWorld &world, UBlockRegistry &registry,
                       const std::vector<glm::ivec3> &block_positions,
                       int max_world_y);
RelightFrontierOutcome RelightBlocksAroundAllEx(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::vector<glm::ivec3> &block_positions, int min_world_y,
    int max_world_y, bool include_block_light, int frontier_iterations);
void RelightColumn(UBlockWorld &world, UBlockRegistry &registry, int world_x,
                   int world_z, int min_y, int max_y,
                   bool include_block_light = true,
                   bool include_skylight = true);
void RelightColumnWithFrontier(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               bool include_block_light, bool include_skylight,
                               std::vector<glm::ivec3> *out_relit_chunks);
/// Recompute sky from known geometry at the entering boundary, preserving block
/// light. Missing geometry is not evidence of an open-sky face.
void ApplyEnterOpenSkyBoundary(UBlockWorld &world, UBlockRegistry &registry,
                               int world_x, int world_z, int min_y, int max_y,
                               std::vector<glm::ivec3> *out_changed_chunks =
                                   nullptr,
                               const char *audit_source = nullptr);
void RelightAllLoadedChunks(UBlockWorld &world, UBlockRegistry &registry);

/// Geometry-derived blocklight for one writable section and a read-only halo.
void PropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                         glm::ivec3 chunk_coord);

} // namespace cutum

#endif // CHUNKLIGHTING_H
