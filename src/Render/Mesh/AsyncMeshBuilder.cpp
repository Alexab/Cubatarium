#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Mesh/CrossInstanceCollector.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cutum
{

namespace
{
std::size_t ResolveMeshWorkerCount(std::size_t thread_count)
{
  return ComputeWorkerThreadCount(JobPoolKind::MeshBuild, thread_count);
}

std::size_t EstimateMeshResultBytes(const MeshBuildResult &result)
{
  std::size_t bytes = sizeof(MeshBuildResult);
  for (const GreedyMeshBatch &batch : result.batches)
  {
    bytes += batch.vertices.capacity() * sizeof(GreedyMeshVertex);
    bytes += batch.indices.capacity() * sizeof(uint32_t);
  }
  if (result.PendingSnapshot)
  {
    bytes += sizeof(ChunkMeshSnapshot);
  }
  for (const auto &entry : result.crossCenters)
  {
    bytes += entry.second.capacity() * sizeof(CrossInstanceGpu);
  }
  return bytes + result.batches.capacity() * sizeof(GreedyMeshBatch);
}
} // namespace

UAsyncMeshBuilder::UAsyncMeshBuilder(std::size_t thread_count)
    : WorkerCount(static_cast<int>(ResolveMeshWorkerCount(thread_count))),
      Pool(ResolveMeshWorkerCount(thread_count), "MeshBuild")
{
  const int slots = URuntimeTuning::Get().MeshCompletedSlots;
  const std::size_t cap =
      slots > 0 ? static_cast<std::size_t>(slots)
                : static_cast<std::size_t>(WorkerCount * kPipelineSlotsPerWorker);
  Completed.SetCapacity(cap);
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

bool UAsyncMeshBuilder::Enqueue(ChunkMeshSnapshot snapshot,
                                UBlockRegistry &registry)
{
  const glm::ivec3 coord = snapshot.coord;
  if (!UPipelineAdmission::Get().TryAcquireSnapshotBytes(
          sizeof(ChunkMeshSnapshot)))
  {
    return false;
  }
  // std::function is copyable; the last queued/running closure owns the lease.
  auto snapshot_credit = std::make_shared<UPipelineCreditGuard>(
      PipelineCreditKind::Snapshot, sizeof(ChunkMeshSnapshot), true);
  const uint64_t submitEpoch = Epoch.load(std::memory_order_acquire);
  const uint64_t jobId =
      NextJobId.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[coord] = jobId;
  }

  // Keep the definitions catalog alive for the whole job even if Reload swaps
  // Active mid-flight. Registry maps are separately mutex-protected.
  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();

  if (!Pool.TryEnqueue(
          [this, snapshot = std::move(snapshot), registryPtr = &registry,
           catalogKeep = std::move(catalogKeep), jobId, submitEpoch,
           snapshot_credit = std::move(snapshot_credit)]() mutable
          {
        (void)catalogKeep;
        MeshBuildResult result;
        result.coord = snapshot.coord;
        result.sourceRevision = snapshot.sourceRevision;
        result.jobId = jobId;
        result.submitEpoch = submitEpoch;
        result.InputStamps = snapshot.inputStamps;
        result.InputStampsValid = snapshot.inputStampsValid;
        result.InputCatalog = catalogKeep;

        auto *gpu_mesher = Mesher;
        const bool defer_gpu =
            gpu_mesher &&
            gpu_mesher->CanDeferGpuExtract(snapshot, *registryPtr);
        if (defer_gpu)
        {
          // GPF1: defer opaque-solid chunks to main-thread GPU emit.
          result.GpuExtractPending = true;
          result.PendingSnapshot =
              std::make_unique<ChunkMeshSnapshot>(std::move(snapshot));
          CollectCrossInstancesFromSnapshot(*result.PendingSnapshot, *registryPtr,
                                            result.crossCenters);
        }
        else
        {
          std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
          const auto quads =
              Mesher ? Mesher->BuildChunkMesh(snapshot, *registryPtr)
                     : UGreedyMesher::BuildChunkMesh(snapshot, *registryPtr);
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
          CollectCrossInstancesFromSnapshot(snapshot, *registryPtr,
                                            result.crossCenters);
          result.batches.reserve(byBlockId.size());
          for (auto &entry : byBlockId)
          {
            entry.second.blockId = entry.first;
            result.batches.push_back(std::move(entry.second));
          }
        }
        const std::size_t result_bytes = EstimateMeshResultBytes(result);
        auto &pipe_adm = UPipelineAdmission::Get();
        if (!pipe_adm.TryAcquireResultBytes(result_bytes))
        {
          {
            std::lock_guard<std::mutex> lock(InFlightMutex);
            const auto it = InFlight.find(result.coord);
            if (it != InFlight.end() && it->second == jobId)
              InFlight.erase(it);
          }
          std::lock_guard<std::mutex> lock(OverflowMutex);
          OverflowCoords.push_back(result.coord); // Unconditional demand retry.
          return;
        }
        result.ResultCredit = std::make_unique<UPipelineCreditGuard>(
            PipelineCreditKind::Result, result_bytes, true);

        MeshBuildResult dropped;
        if (Completed.PushDropOldest(std::move(result), &dropped))
        {
          {
            std::lock_guard<std::mutex> lock(InFlightMutex);
            const auto it = InFlight.find(dropped.coord);
            if (it != InFlight.end() && it->second == dropped.jobId)
            {
              InFlight.erase(it);
            }
          }
          std::lock_guard<std::mutex> olock(OverflowMutex);
          OverflowCoords.push_back(dropped.coord);
        }
          }))
  {
    {
      std::lock_guard<std::mutex> lock(InFlightMutex);
      const auto it = InFlight.find(coord);
      if (it != InFlight.end() && it->second == jobId)
      {
        InFlight.erase(it);
      }
    }
    return false;
  }
  return true;
}

void UAsyncMeshBuilder::SetCompletedCapacity(std::size_t cap)
{
  for (auto &dropped : Completed.SetCapacity(cap))
  {
    {
      std::lock_guard<std::mutex> lock(InFlightMutex);
      const auto it = InFlight.find(dropped.coord);
      if (it != InFlight.end() && it->second == dropped.jobId)
        InFlight.erase(it);
    }
    std::lock_guard<std::mutex> lock(OverflowMutex);
    OverflowCoords.push_back(dropped.coord);
  }
}

std::vector<MeshBuildResult> UAsyncMeshBuilder::DrainCompleted(int maxPerFrame)
{
  const std::size_t take =
      maxPerFrame <= 0 ? 0 : static_cast<std::size_t>(maxPerFrame);
  std::vector<MeshBuildResult> drained = Completed.DrainUpTo(take);
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  std::vector<MeshBuildResult> accepted;
  accepted.reserve(drained.size());

  std::vector<glm::ivec3> discarded_now;
  discarded_now.reserve(drained.size());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (MeshBuildResult &result : drained)
    {
      if (result.submitEpoch != current_epoch)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        DiscardedLateEpoch.fetch_add(1, std::memory_order_relaxed);
        const auto it = InFlight.find(result.coord);
        if (it != InFlight.end() && it->second == result.jobId)
        {
          InFlight.erase(it);
        }
        discarded_now.push_back(result.coord);
        continue;
      }
      const auto it = InFlight.find(result.coord);
      if (it == InFlight.end() || it->second != result.jobId)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        DiscardedLateJobMismatch.fetch_add(1, std::memory_order_relaxed);
        discarded_now.push_back(result.coord);
        continue;
      }
      InFlight.erase(it);
      accepted.push_back(std::move(result));
    }
  }
  if (!discarded_now.empty())
  {
    std::lock_guard<std::mutex> dlock(DiscardedMutex);
    DiscardedCoords.insert(DiscardedCoords.end(), discarded_now.begin(),
                           discarded_now.end());
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
  int tracked = 0;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    tracked = static_cast<int>(InFlight.size());
  }
  // ForgetInflight erases tracking while the worker is still Active/Pending —
  // schedule must count real pool depth or Completed fills with vertex RAM.
  const int pool = static_cast<int>(Pool.GetActiveJobCount() +
                                    Pool.GetPendingJobCount());
  return std::max(tracked, pool);
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

bool UAsyncMeshBuilder::HasInflightInHorizontalRadius(
    glm::ivec3 center_ground_chunk, int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  // Era53 / Enter SoT: only near Completed blocks spawn ring — far pool depth
  // must not sticky-block IsSpawnMeshRingReady / mesh warmup forever.
  if (Completed.Any(
          [&](const MeshBuildResult &result)
          {
            const int horiz =
                std::max(std::abs(result.coord.x - center_ground_chunk.x),
                         std::abs(result.coord.z - center_ground_chunk.z));
            return horiz <= radius_chunks;
          }))
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  for (const auto &kv : InFlight)
  {
    const glm::ivec3 &coord = kv.first;
    const int horiz = std::max(std::abs(coord.x - center_ground_chunk.x),
                               std::abs(coord.z - center_ground_chunk.z));
    if (horiz <= radius_chunks)
    {
      return true;
    }
  }
  return false;
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
      DiscardedLateEpoch.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void UAsyncMeshBuilder::ForgetInflight(const glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  InFlight.erase(coord);
}

std::vector<glm::ivec3> UAsyncMeshBuilder::TakeOverflowCoords()
{
  std::lock_guard<std::mutex> olock(OverflowMutex);
  std::vector<glm::ivec3> out;
  out.swap(OverflowCoords);
  return out;
}

std::vector<glm::ivec3> UAsyncMeshBuilder::TakeDiscardedCoords()
{
  std::lock_guard<std::mutex> dlock(DiscardedMutex);
  std::vector<glm::ivec3> out;
  out.swap(DiscardedCoords);
  return out;
}

} // namespace cutum
