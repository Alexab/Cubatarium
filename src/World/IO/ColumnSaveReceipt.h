#pragma once
#include "World/Chunks/ChunkManager.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/Math/GridMath.h"
#include <limits>
#include <unordered_set>

namespace cutum
{
// Captures data/metadata identities, not voxel copies. Unrelated columns do
// not invalidate this receipt; reuse, edits, fluid/light/weather changes do.
class TerrainColumnSaveStamp
{
public:
  TerrainColumnSaveStamp() = default;
  TerrainColumnSaveStamp(const UChunkManager &manager, glm::ivec3 ground,
                         int min_y, int max_y)
      : Owner(manager.GetInputRevisionIdentity()), Ground(ground),
        MinCy(FloorDiv(min_y, CHUNK_SIZE)), MaxCy(FloorDiv(max_y, CHUNK_SIZE))
  {
    if (max_y < min_y || int64_t(MaxCy) - MinCy + 1 > 65536)
      return;
    for (int cy = MinCy; cy <= MaxCy; ++cy)
      States.push_back(manager.CaptureTerrainSaveState({ground.x, cy, ground.z}));
  }
  bool Matches(const UChunkManager &manager) const
  {
    if (States.empty() || Owner.lock() != manager.GetInputRevisionIdentity())
      return false;
    for (int cy = MinCy; cy <= MaxCy; ++cy)
      if (States[cy - MinCy] != manager.CaptureTerrainSaveState({Ground.x, cy, Ground.z}))
        return false;
    return true;
  }
private:
  std::weak_ptr<const uint64_t> Owner;
  glm::ivec3 Ground{0};
  int MinCy{0}, MaxCy{-1};
  std::vector<UChunkManager::TerrainSaveState> States;
};

// An ACK belongs to one folder/column/batch and one expected operation. A
// stale or duplicate result cannot complete a newer column save.
class ColumnSaveCompletion
{
public:
  static constexpr int Light = std::numeric_limits<int>::min();
  static constexpr int Cleanup = Light + 1;
  ColumnSaveCompletion() = default;
  ColumnSaveCompletion(uint64_t id, std::string folder, glm::ivec3 ground,
                       int min_cy, int highest_cy, bool cleanup)
      : Id(id), Folder(std::move(folder)), Ground(ground)
  {
    Remaining.insert(Light);
    for (int cy = min_cy; cy <= highest_cy; ++cy) Remaining.insert(cy);
    if (cleanup) Remaining.insert(Cleanup);
  }
  bool Consume(const AsyncChunkSaveRequest &done)
  {
    if (!Id || done.saveBatchId != Id || done.worldFolder != Folder ||
        done.groundCoord != Ground || done.coord.x != Ground.x || done.coord.z != Ground.z)
      return false;
    int key = done.coord.y;
    if (!done.cleanupOnly && (key == Light || key == Cleanup)) return false;
    if (done.cleanupOnly)
    {
      if (done.cleanupOperation == "column_light_receipts") key = Light;
      else if (done.cleanupOperation == "slice_range") key = Cleanup;
      else return false;
    }
    if (!Remaining.erase(key)) return false;
    Failed |= !done.success;
    return true;
  }
  bool Complete() const { return Id && Remaining.empty(); }
  bool Succeeded() const { return Complete() && !Failed; }
  uint64_t BatchId() const { return Id; }
private:
  uint64_t Id{0};
  std::string Folder;
  glm::ivec3 Ground{0};
  std::unordered_set<int> Remaining;
  bool Failed{false};
};
} // namespace cutum
