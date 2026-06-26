#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <mutex>

namespace cutum
{

namespace
{
constexpr int kCrossScanBelow = 2;
constexpr int kCrossScanAbove = 4;

int MaxSolidLocalYSnapshot(const ChunkMeshSnapshot &snapshot,
                           const UBlockRegistry &registry)
{
  int max_y = 0;
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const BlockId id = snapshot.GetBlockLocal(glm::ivec3(lx, ly, lz));
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) == BlockRenderStyle::Cross)
        {
          continue;
        }
        max_y = std::max(max_y, ly);
      }
    }
  }
  return max_y;
}
} // namespace

void UAsyncMeshBuilder::Enqueue(ChunkMeshSnapshot snapshot,
                                UBlockRegistry &registry)
{
  const glm::ivec3 coord = snapshot.coord;
  const uint64_t jobId = NextJobId++;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[coord] = jobId;
  }

  Pool.Enqueue(
      [this, snapshot = std::move(snapshot), registryPtr = &registry,
       jobId]() mutable
      {
        MeshBuildResult result;
        result.coord = snapshot.coord;
        result.sourceRevision = snapshot.sourceRevision;
        result.jobId = jobId;

        std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
        const auto quads =
            UGreedyMesher::BuildChunkMesh(snapshot, *registryPtr);
        for (const GreedyQuad &q : quads)
        {
          GreedyMeshBatch &batch = byBlockId[q.Id];
          batch.blockId = q.Id;
          batch.Transparent = registryPtr->IsTransparent(q.Id);
          batch.AlphaCutout =
              registryPtr->GetRenderStyle(q.Id) == BlockRenderStyle::Cutout;
          AppendGreedyQuad(q, snapshot.coord, batch.vertices, batch.indices);
        }
        const int max_local_y =
            MaxSolidLocalYSnapshot(snapshot, *registryPtr);
        const int y_min = std::max(0, max_local_y - kCrossScanBelow);
        const int y_max =
            std::min(CHUNK_SIZE - 1, max_local_y + kCrossScanAbove);
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
          for (int ly = y_min; ly <= y_max; ++ly)
          {
            for (int lz = 0; lz < CHUNK_SIZE; ++lz)
            {
              const glm::ivec3 local(lx, ly, lz);
              const BlockId id = snapshot.GetBlockLocal(local);
              if (id == BLOCK_AIR ||
                  registryPtr->GetRenderStyle(id) != BlockRenderStyle::Cross)
              {
                continue;
              }
              const glm::ivec3 worldPos(snapshot.coord.x * CHUNK_SIZE + lx,
                                        snapshot.coord.y * CHUNK_SIZE + ly,
                                        snapshot.coord.z * CHUNK_SIZE + lz);
              GreedyMeshBatch &batch = byBlockId[id];
              batch.blockId = id;
              batch.Transparent = false;
              batch.AlphaCutout = true;
              AppendCrossSprite(BlockCenter(worldPos), batch.vertices,
                                batch.indices);
            }
          }
        }
        result.batches.reserve(byBlockId.size());
        for (auto &entry : byBlockId)
        {
          entry.second.blockId = entry.first;
          result.batches.push_back(std::move(entry.second));
        }
        Completed.Push(std::move(result));
      });
}

std::vector<MeshBuildResult> UAsyncMeshBuilder::DrainCompleted(int maxPerFrame)
{
  std::vector<MeshBuildResult> all = Completed.DrainAll();
  if (maxPerFrame > 0 &&
      static_cast<int>(all.size()) > maxPerFrame)
  {
    std::vector<MeshBuildResult> kept;
    kept.reserve(static_cast<std::size_t>(maxPerFrame));
    std::vector<MeshBuildResult> rest;
    rest.reserve(all.size() - static_cast<std::size_t>(maxPerFrame));
    for (std::size_t i = 0; i < all.size(); ++i)
    {
      if (static_cast<int>(i) < maxPerFrame)
      {
        kept.push_back(std::move(all[i]));
      }
      else
      {
        rest.push_back(std::move(all[i]));
      }
    }
    for (MeshBuildResult &pending : rest)
    {
      Completed.Push(std::move(pending));
    }
    all = std::move(kept);
  }

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (const MeshBuildResult &result : all)
    {
      const auto it = InFlight.find(result.coord);
      if (it != InFlight.end() && it->second == result.jobId)
      {
        InFlight.erase(it);
      }
    }
  }
  return all;
}

bool UAsyncMeshBuilder::IsInFlight(glm::ivec3 coord) const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return InFlight.find(coord) != InFlight.end();
}

int UAsyncMeshBuilder::GetInFlightCount() const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return static_cast<int>(InFlight.size());
}

bool UAsyncMeshBuilder::HasPendingWork() const
{
  if (!Completed.Empty())
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return !InFlight.empty();
}

void UAsyncMeshBuilder::WaitIdle() { Pool.WaitIdle(); }

} // namespace cutum
