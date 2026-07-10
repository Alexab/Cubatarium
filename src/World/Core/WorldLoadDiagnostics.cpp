#include "World/Core/WorldLoadDiagnostics.h"

#include "App/Platform/Log.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"

#include <iostream>
#include <sstream>

namespace cutum
{

namespace
{

size_t CountLoadedChunks(const UBlockWorld &block_world)
{
  size_t count = 0;
  block_world.GetChunkManager().ForEachChunk(
      [&](const UChunk &) { ++count; });
  return count;
}

std::string FormatWorldLoadDiagLine(const std::string &phase, const UWorld &world,
                                    const std::optional<glm::vec3> &camera_pos)
{
  const UWorldMeshService &mesh = world.GetMeshService();
  std::ostringstream out;
  out << "[WorldLoad] phase=" << phase << " chunks=" << CountLoadedChunks(world.GetBlockWorld())
      << " blocks_non_air=" << world.GetBlockWorld().CountNonAir()
      << " mesh_dirty=" << mesh.GetDirtyCount()
      << " mesh_in_flight=" << mesh.GetAsyncInFlightCount()
      << " greedy_cache=" << mesh.GetGreedyCacheSize()
      << " greedy_batches=" << mesh.GetCache().GetGreedyBatches().size()
      << " greedy_vertices=" << mesh.GetGreedyVertexCount()
      << " flat_rebuild_ms=" << mesh.GetLastFlatRebuildMs();
  if (camera_pos)
  {
    out << " camera=(" << camera_pos->x << "," << camera_pos->y << ","
        << camera_pos->z << ")";
  }
  return out.str();
}

} // namespace

void LogWorldLoadDiag(const std::string &phase, const UWorld &world,
                      const std::optional<glm::vec3> &camera_pos)
{
  const std::string line = FormatWorldLoadDiagLine(phase, world, camera_pos);
  CubatariumLogInfo("WorldLoad", line);
  std::cerr << line << std::endl;
}

void WarnIfTerrainMeshesMissing(const UWorld &world, const std::string &context)
{
  const size_t blocks = world.GetBlockWorld().CountNonAir();
  const UWorldMeshService &mesh = world.GetMeshService();
  const size_t cache_size = mesh.GetGreedyCacheSize();
  const size_t batch_count = mesh.GetCache().GetGreedyBatches().size();
  if (blocks == 0)
  {
    return;
  }
  if (cache_size > 0 || batch_count > 0)
  {
    return;
  }
  std::ostringstream msg;
  msg << context << ": terrain blocks present (" << blocks
      << ") but greedy meshes are empty (cache=" << cache_size
      << " batches=" << batch_count << " dirty=" << mesh.GetDirtyCount()
      << " in_flight=" << mesh.GetAsyncInFlightCount() << ")";
  CubatariumLogError("WorldLoad", msg.str());
  std::cerr << "[WorldLoad] WARNING: " << msg.str() << std::endl;
}

} // namespace cutum
