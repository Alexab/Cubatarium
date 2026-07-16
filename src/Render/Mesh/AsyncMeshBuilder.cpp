#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Render/Mesh/CrossInstanceCollector.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <mutex>
#include <thread>

namespace cutum
{

namespace
{
std::size_t ResolveMeshWorkerCount(std::size_t thread_count)
{
  return ComputeWorkerThreadCount(JobPoolKind::MeshBuild, thread_count);
}
} // namespace

UAsyncMeshBuilder::UAsyncMeshBuilder(std::size_t thread_count)
    : Pool(ResolveMeshWorkerCount(thread_count), "MeshBuild"),
      WorkerCount(static_cast<int>(ResolveMeshWorkerCount(thread_count)))
{
}

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
  const uint64_t submitEpoch = Epoch.load(std::memory_order_acquire);
  const uint64_t jobId =
      NextJobId.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[coord] = jobId;
  }

  Pool.Enqueue(
      [this, snapshot = std::move(snapshot), registryPtr = &registry, jobId,
       submitEpoch]() mutable
      {
        MeshBuildResult result;
        result.coord = snapshot.coord;
        result.sourceRevision = snapshot.sourceRevision;
        result.jobId = jobId;
        result.submitEpoch = submitEpoch;

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
          const size_t base_vertex = batch.vertices.size();
          AppendGreedyQuad(q, snapshot.coord, batch.vertices, batch.indices);
          for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
          {
            ApplyVertexLight(batch.vertices[i], q.LightPacked);
          }
        }
        const int max_local_y = MaxSolidLocalYSnapshot(snapshot, *registryPtr);
        (void)max_local_y;
        CollectCrossInstancesFromSnapshot(snapshot, *registryPtr,
                                          result.crossCenters);
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
  const std::size_t limit =
      maxPerFrame > 0 ? static_cast<std::size_t>(maxPerFrame) : 0;
  std::vector<MeshBuildResult> drained =
      limit > 0 ? Completed.DrainUpTo(limit) : std::vector<MeshBuildResult>{};
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  std::vector<MeshBuildResult> accepted;
  accepted.reserve(drained.size());

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (MeshBuildResult &result : drained)
    {
      if (result.submitEpoch != current_epoch)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        const auto it = InFlight.find(result.coord);
        if (it != InFlight.end() && it->second == result.jobId)
        {
          InFlight.erase(it);
        }
        continue;
      }
      const auto it = InFlight.find(result.coord);
      if (it == InFlight.end() || it->second != result.jobId)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      InFlight.erase(it);
      accepted.push_back(std::move(result));
    }
  }
  return accepted;
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

bool UAsyncMeshBuilder::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncMeshBuilder::CancelPending()
{
  Epoch.fetch_add(1, std::memory_order_acq_rel);
  Pool.CancelPendingJobs();
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.clear();
  }
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  for (MeshBuildResult &result : Completed.DrainAll())
  {
    if (result.submitEpoch != current_epoch)
    {
      DiscardedLate.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void UAsyncMeshBuilder::ForgetInflight(const glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  InFlight.erase(coord);
}

} // namespace cutum
