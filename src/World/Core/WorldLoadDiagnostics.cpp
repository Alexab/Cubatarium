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

int SkyLightAtWorld(const UBlockWorld &block_world, glm::ivec3 world_pos)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(world_pos);
  const UChunk *chunk = block_world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return 0;
  }
  return chunk->GetSkyLightLocal(UChunkManager::WorldToLocal(world_pos));
}

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
  // Skip full-world scans on save/shutdown — CountNonAir (and even chunk
  // iteration under worker contention) can stall and look like a hang.
  const bool skip_world_scan =
      phase == "drain_async_io" || phase == "scan_save_chunks" ||
      phase == "save_chunks" || phase == "save_metadata" || phase == "done" ||
      phase == "init" || phase == "prepare_enter" || phase == "prepare_view" ||
      phase == "finalize_world" || phase == "mesh_warmup" ||
      phase == "post_load_analysis";
  std::ostringstream out;
  out << "[WorldLoad] phase=" << phase
      << " chunks="
      << (skip_world_scan ? 0 : CountLoadedChunks(world.GetBlockWorld()))
      << " blocks_non_air="
      << (skip_world_scan ? 0 : world.GetBlockWorld().CountNonAir())
      << " mesh_dirty=" << mesh.GetDirtyCount()
      << " mesh_in_flight=" << mesh.GetAsyncInFlightCount()
      << " greedy_cache=" << mesh.GetGreedyCacheSize()
      << " greedy_batches="
      << (skip_world_scan
              ? 0
              : (mesh.GetCache().GetGreedyOpaqueCutoutRefs().size() +
                 mesh.GetCache().GetGreedyTransparentRefs().size()))
      << " greedy_vertices="
      << (skip_world_scan ? 0 : mesh.GetGreedyVertexCount())
      << " flat_rebuild_ms=" << mesh.GetLastFlatRebuildMs();
  if (skip_world_scan)
  {
    out << " world_scan_skipped=1";
  }
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
  const UWorldMeshService &mesh = world.GetMeshService();
  const size_t cache_size = mesh.GetGreedyCacheSize();
  const size_t batch_count =
      mesh.GetCache().GetGreedyOpaqueCutoutRefs().size() +
      mesh.GetCache().GetGreedyTransparentRefs().size();
  // Check meshes first: CountNonAir under streamer/mesh worker contention can
  // stall for minutes and looks like a hang on PrepareView.
  if (cache_size > 0 || batch_count > 0)
  {
    return;
  }
  const size_t blocks = world.GetBlockWorld().CountNonAir();
  if (blocks == 0)
  {
    return;
  }
  std::ostringstream msg;
  msg << context << ": terrain blocks present (" << blocks
      << ") but greedy meshes are empty (cache=" << cache_size
      << " batches=" << batch_count << " dirty=" << mesh.GetDirtyCount()
      << " in_flight=" << mesh.GetAsyncInFlightCount() << ")";
  CubatariumLogInfo("WorldLoad", msg.str());
  std::cerr << "[WorldLoad] WARNING: " << msg.str() << std::endl;
}

void WarnIfSpawnSkylightMissing(const UWorld &world, const std::string &context)
{
  const size_t blocks = world.GetBlockWorld().CountNonAir();
  if (blocks == 0)
  {
    return;
  }
  const glm::ivec3 feet = world.GetPreferredLoadFocusBlock();
  const std::optional<int> top_y = world.FindHighestSolidY(feet.x, feet.z);
  if (!top_y)
  {
    return;
  }
  // Skylight is stored in transparent voxels; probe open air above the surface.
  const glm::ivec3 probe(feet.x, *top_y + 1, feet.z);
  if (SkyLightAtWorld(world.GetBlockWorld(), probe) > 0)
  {
    return;
  }
  // Fallback: any skylight in the spawn column above the surface.
  for (int y = *top_y + 1; y <= *top_y + 8; ++y)
  {
    if (SkyLightAtWorld(world.GetBlockWorld(), glm::ivec3(feet.x, y, feet.z)) > 0)
    {
      return;
    }
  }
  std::ostringstream msg;
  msg << context << ": terrain blocks present (" << blocks
      << ") but spawn skylight is zero above surface at (" << probe.x << ", "
      << probe.y << ", " << probe.z << ")";
  CubatariumLogInfo("WorldLoad", msg.str());
  std::cerr << "[WorldLoad] WARNING: " << msg.str() << std::endl;
}

} // namespace cutum
