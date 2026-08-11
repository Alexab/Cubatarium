#include "Render/Mesh/MeshCaptureStore.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

void UMeshCaptureStore::Invalidate(glm::ivec3 coord)
{
  Store_.erase(coord);
}

void UMeshCaptureStore::InvalidateAll()
{
  Store_.clear();
}

void UMeshCaptureStore::BumpWorldEpoch()
{
  ++WorldEpoch_;
  if (WorldEpoch_ == 0)
  {
    WorldEpoch_ = 1;
  }
  Store_.clear();
}

std::optional<ChunkMeshSnapshot>
UMeshCaptureStore::TryGet(glm::ivec3 coord, uint64_t source_revision) const
{
  const auto it = Store_.find(coord);
  if (it == Store_.end())
  {
    return std::nullopt;
  }
  if (it->second.worldEpoch != WorldEpoch_)
  {
    return std::nullopt;
  }
  if (it->second.sourceRevision != source_revision)
  {
    return std::nullopt;
  }
  return it->second.data;
}

ChunkMeshSnapshot UMeshCaptureStore::CaptureAndStore(const UBlockWorld &world,
                                                     glm::ivec3 coord,
                                                     uint64_t source_revision)
{
  ChunkMeshSnapshot snap = ChunkMeshSnapshot::Capture(
      world, coord, source_revision, NeighborDrawableFn_, NeighborDrawableCtx_);
  Entry entry;
  entry.worldEpoch = WorldEpoch_;
  entry.sourceRevision = source_revision;
  entry.data = snap;
  Store_[coord] = std::move(entry);
  return snap;
}

ChunkMeshSnapshot UMeshCaptureStore::TakeOrRefresh(const UBlockWorld &world,
                                                   glm::ivec3 coord,
                                                   uint64_t source_revision,
                                                   int &refresh_budget)
{
  if (auto hit = TryGet(coord, source_revision))
  {
    return std::move(*hit);
  }
  if (refresh_budget <= 0)
  {
    // Still capture — schedule cannot proceed without a snapshot; budget is a
    // soft prefer for reuse, not a hard deny.
    return CaptureAndStore(world, coord, source_revision);
  }
  --refresh_budget;
  return CaptureAndStore(world, coord, source_revision);
}

} // namespace cutum
