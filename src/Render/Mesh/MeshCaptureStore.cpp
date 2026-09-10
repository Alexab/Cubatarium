#include "Render/Mesh/MeshCaptureStore.h"
#include "Render/Mesh/MeshNeighborPolicy.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"

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

bool UMeshCaptureStore::TryCommit(glm::ivec3 coord, uint64_t source_revision,
                                  uint64_t world_epoch,
                                  ChunkMeshSnapshot snapshot,
                                  const DependencyStamp *captured_deps)
{
  if (world_epoch == 0 || world_epoch != WorldEpoch_)
  {
    return false;
  }
  Entry entry;
  entry.worldEpoch = world_epoch;
  entry.sourceRevision = source_revision;
  if (captured_deps != nullptr)
  {
    entry.deps = *captured_deps;
  }
  entry.data = std::move(snapshot);
  Store_[coord] = std::move(entry);
  return true;
}

void UMeshCaptureStore::Commit(glm::ivec3 coord, uint64_t source_revision,
                               uint64_t world_epoch, ChunkMeshSnapshot snapshot)
{
  (void)TryCommit(coord, source_revision, world_epoch, std::move(snapshot),
                  nullptr);
}

ChunkMeshSnapshot UMeshCaptureStore::CaptureAndStore(const UBlockWorld &world,
                                                     glm::ivec3 coord,
                                                     uint64_t source_revision)
{
  ChunkMeshSnapshot snap = ChunkMeshSnapshot::Capture(
      world, coord, source_revision, NeighborDrawableFn_, NeighborDrawableCtx_);
  Commit(coord, source_revision, WorldEpoch_, std::move(snap));
  return Store_[coord].data;
}

std::optional<ChunkMeshSnapshot> UMeshCaptureStore::TakeOrRefresh(
    const UBlockWorld &world, glm::ivec3 coord, uint64_t source_revision,
    int &refresh_budget)
{
  if (auto hit = TryGet(coord, source_revision))
  {
    ++LastStoreHitN_;
    return hit;
  }
  ++LastStoreMissN_;
  if (refresh_budget <= 0)
  {
    return std::nullopt;
  }
  --refresh_budget;
  return CaptureAndStore(world, coord, source_revision);
}

std::optional<ChunkMeshSnapshot> UMeshCaptureStore::RefreshIncrementalShell(
    const UBlockWorld &world, glm::ivec3 coord, uint64_t source_revision,
    uint8_t face_mask)
{
  auto it = Store_.find(coord);
  if (it == Store_.end() || it->second.sourceRevision != source_revision ||
      it->second.worldEpoch != WorldEpoch_ || face_mask == 0)
  {
    int budget = 1;
    return TakeOrRefresh(world, coord, source_revision, budget);
  }
  ChunkMeshSnapshot snap = it->second.data;
  const glm::ivec3 origin = snap.ChunkOrigin();
  for (int face = 0; face < 6; ++face)
  {
    if ((face_mask & (1u << face)) == 0)
    {
      continue;
    }
    const int axis = face / 2;
    const int sign = (face % 2 == 0) ? -1 : 1;
    glm::ivec3 neighbor_coord = coord;
    neighbor_coord[axis] += sign;
    const UChunk *neighbor_chunk =
        world.GetChunkManager().GetChunk(neighbor_coord);
    const bool neighbor_loaded = neighbor_chunk != nullptr;
    bool neighbor_visually_drawable = neighbor_loaded;
    if (neighbor_loaded && NeighborDrawableFn_)
    {
      neighbor_visually_drawable =
          NeighborDrawableFn_(NeighborDrawableCtx_, neighbor_coord);
    }
    for (int u = 0; u < CHUNK_SIZE; ++u)
    {
      for (int v = 0; v < CHUNK_SIZE; ++v)
      {
        glm::ivec3 local(0);
        const int u_axis = (axis + 1) % 3;
        const int v_axis = (axis + 2) % 3;
        local[axis] = sign < 0 ? -1 : CHUNK_SIZE;
        local[u_axis] = u;
        local[v_axis] = v;
        const glm::ivec3 worldPos = origin + local;
        const int cell = u + v * CHUNK_SIZE;
        const int flat = face * ChunkMeshSnapshot::kShellFaceCells + cell;
        BlockId raw = BLOCK_AIR;
        if (neighbor_chunk)
        {
          raw = neighbor_chunk->GetBlockLocal(
              UChunkManager::WorldToLocal(worldPos));
        }
        else
        {
          raw = world.GetBlock(worldPos);
        }
        snap.shellBlocks[static_cast<size_t>(flat)] =
            ShellBlockForNeighborOcclusion(raw, neighbor_visually_drawable);
        snap.shellNeighborState[static_cast<size_t>(flat)] =
            static_cast<uint8_t>(ClassifyShellCell(
                neighbor_loaded, snap.shellBlocks[static_cast<size_t>(flat)],
                neighbor_visually_drawable));
        if (neighbor_chunk)
        {
          snap.shellLight[static_cast<size_t>(flat)] =
              neighbor_chunk->GetLightPackedLocal(
                  UChunkManager::WorldToLocal(worldPos));
        }
        snap.shellFluid[static_cast<size_t>(flat)] =
            PackFluidCellState(world.GetFluidState(worldPos));
      }
    }
  }
  Commit(coord, source_revision, WorldEpoch_, snap);
  ++LastStoreHitN_;
  return snap;
}

} // namespace cutum
