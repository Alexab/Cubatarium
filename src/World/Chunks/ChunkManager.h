#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include "World/Chunks/BlockQuery.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/LoadedAirSlice.h"
#include "World/Math/BlockTypes.h"
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct IVec3Hash
{
  size_t operator()(const glm::ivec3 &v) const noexcept
  {
    const size_t h1 = std::hash<int>()(v.x);
    const size_t h2 = std::hash<int>()(v.y);
    const size_t h3 = std::hash<int>()(v.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

struct ChunkGenerationToken;

class UChunkManager
{
public:
  BlockId GetBlock(glm::ivec3 worldPos) const;
  /// Phase 4: Unloaded ≠ AIR for raycast/place contracts.
  BlockQueryResult QueryBlock(glm::ivec3 worldPos) const;
  FluidCellState GetFluidState(glm::ivec3 worldPos) const;
  void SetBlock(glm::ivec3 worldPos, BlockId Id);
  void SetFluidState(glm::ivec3 worldPos, FluidCellState state);
  void ClearFluidState(glm::ivec3 worldPos);
  void Clear();
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunk *GetChunk(glm::ivec3 chunk_coord);
  const UChunk *GetChunk(glm::ivec3 chunk_coord) const;
  bool HasChunk(glm::ivec3 chunk_coord) const;
  void EnsureChunk(glm::ivec3 chunk_coord);
  /// Caller supplies the current owning column sequence, never a cached one.
  bool InstallLoadedAirSlice(const LoadedAirSlice &slice,
                             const ChunkGenerationToken &token,
                             uint64_t current_sequence, bool cancelled);
  bool HasLoadedAirSlice(glm::ivec3 coord) const;
  /// Legacy serialization carrier only; never inserts geometry into the world.
  std::unique_ptr<UChunk> MakeTerrainSaveSnapshot(glm::ivec3 coord) const;
  /// Light ownership may exist without allocating a geometry chunk. Allocation
  /// alone is not evidence of known AIR or a completed physical calculation.
  std::shared_ptr<const SectionLightField>
  GetSectionLightField(glm::ivec3 coord) const;
  std::shared_ptr<const SectionLightField>
  EnsureSectionLightField(glm::ivec3 coord);
  bool ReplaceSectionLightData(glm::ivec3 coord,
                               const std::array<uint8_t, CHUNK_VOLUME> &data);
  /// Explicit retirement for absent geometry; resident adapters retain
  /// ownership.
  bool ReplaceComputedSectionLightVolume(glm::ivec3 coord,
                                         const SectionLightVolume &volume,
                                         bool sky, bool block);
  bool ReleaseAbsentSectionLightField(glm::ivec3 coord);
  /// Fair bounded retirement scan. Resident adapters are always retained;
  /// external shared snapshots remain immutable after ownership is released.
  size_t
  RetireAbsentSectionLightFields(size_t visit_budget,
                                 const std::function<bool(glm::ivec3)> &keep);
  size_t GetSectionLightFieldCount() const { return LightFields.size(); }
  /// Fired when a chunk transitions to non-air (its first populated block
  /// arrives via SetBlock). Subscriber handles cross-thread sync; set once
  /// before async IO starts.
  std::function<void(glm::ivec3)> OnChunkBecameNonAir;
  void RemoveChunk(glm::ivec3 chunk_coord);
  void ForEachChunk(const std::function<void(const UChunk &)> &fn) const;
  /// Iterate each resident horizontal column once (coordinate Y is always 0).
  void ForEachColumn(const std::function<void(const glm::ivec3 &)> &fn) const;
  /// O(1) resident chunk count (loaded map size, not free-list).
  size_t GetResidentChunkCount() const { return Chunks.size(); }
  /// Changes for every resident data/light mutation and residency transition.
  /// Keeps a revision counter's identity distinct across manager replacement.
  std::shared_ptr<const uint64_t> GetInputRevisionIdentity() const
  {
    return InputRevision;
  }
  uint64_t GetInputRevision() const
  {
    return InputRevision ? *InputRevision : 0;
  }
  /// Physical calculations read voxel/fluid geometry, not their light output.
  /// Residency/replacement changes both clocks; light-only writes change only
  /// GetInputRevision(). Identity protects manager replacement as well as ABA.
  std::shared_ptr<const uint64_t> GetGeometryInputRevisionIdentity() const
  {
    return GeometryInputRevision;
  }
  uint64_t GetGeometryInputRevision() const
  {
    return GeometryInputRevision ? *GeometryInputRevision : 0;
  }
  /// O(1) count of unique horizontal columns with one or more resident slices.
  size_t GetResidentColumnCount() const
  {
    return ResidentColumnChunkCounts.size();
  }
  /// Cap recycled chunks retained after unload (0 = destroy immediately).
  void SetMaxFreeListChunks(size_t cap) { MaxFreeListChunks = cap; }
  size_t GetFreeListSize() const { return FreeList.size(); }

  static glm::ivec3 WorldToChunk(glm::ivec3 world_pos);
  static glm::ivec3 WorldToLocal(glm::ivec3 world_pos);

private:
  friend class UBlockWorld;
  UChunk &GetOrCreateChunk(glm::ivec3 chunk_coord);

  void NoteLightInputMutation()
  {
    if (!InputRevision)
      InputRevision = std::make_shared<uint64_t>(0);
    ++*InputRevision;
  }
  void NoteInputMutation()
  {
    NoteLightInputMutation();
    if (!GeometryInputRevision)
      GeometryInputRevision = std::make_shared<uint64_t>(0);
    ++*GeometryInputRevision;
  }
  std::shared_ptr<uint64_t> InputRevision{std::make_shared<uint64_t>(0)};
  std::shared_ptr<uint64_t> GeometryInputRevision{
      std::make_shared<uint64_t>(0)};
  std::unordered_map<glm::ivec3, std::unique_ptr<UChunk>, IVec3Hash> Chunks;
  std::unordered_map<glm::ivec3, std::shared_ptr<SectionLightField>, IVec3Hash>
      LightFields;
  std::unordered_map<glm::ivec3, LoadedAirSlice, IVec3Hash> LoadedAirSlices;
  std::deque<std::pair<glm::ivec3, uint64_t>> AbsentLightFieldScan;
  std::unordered_map<glm::ivec3, size_t, IVec3Hash> ResidentColumnChunkCounts;
  std::vector<std::unique_ptr<UChunk>> FreeList;
  size_t MaxFreeListChunks{256};
};

} // namespace cutum

#endif
