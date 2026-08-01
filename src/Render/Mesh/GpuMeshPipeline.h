#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GpuGreedyOpaqueEmit.h"
#include "Render/Mesh/GpuMeshSlotAllocator.h"
#include "Render/Mesh/GpuPackedMeshTypes.h"
#include "Render/Mesh/PackedQuad.h"
#include "Render/GlIncludes.h"
#include "Blocks/BlockRegistry.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <queue>
#include <vector>

namespace cutum
{

/// Full GPU mesh pipeline: snapshot upload → mask → greedy → packed emit → SSBO slot.
/// Replaces the CPU vertex readback path in GPF1 for eligible chunks.
/// Non-eligible chunks and cross instances continue through the CPU path.
class UGpuMeshPipeline
{
public:
  static constexpr uint32_t kDefaultMaxSlots = 2048;
  static constexpr int kReadbackRing = 4;

  bool Init(uint32_t max_slots = kDefaultMaxSlots);
  void Shutdown();
  bool IsReady() const { return Ready; }

  /// Queue a snapshot for GPU meshing on the next frame.
  void EnqueueSnapshot(ChunkMeshSnapshot snapshot, uint64_t source_revision);

  /// Process one snapshot synchronously (main thread, GL context required).
  bool ProcessSnapshot(const ChunkMeshSnapshot &snapshot,
                       UBlockRegistry &registry,
                       GpuMeshProcessResult &out_result);

  /// Kick upload+compute into ring PBO; counters polled async (Dispatched).
  /// Rects live in per-slot RectsHoldSsbo so up to kReadbackRing Dispatched
  /// can coexist without serializing Kick on shared EmitState.RectsSsbo.
  struct GpuApplyTicket
  {
    int slotIndex{-1};
    glm::ivec3 coord{0};
    uint32_t quadCount{0};
    uint32_t slotOffsetQuads{0};
    GLsync fence{nullptr};
    int pboIndex{-1};
    bool valid{false};
    /// True after greedy dispatch until counters mapped + packed emit done.
    bool awaitingCounters{false};
  };
  bool KickComputePasses(const ChunkMeshSnapshot &snapshot,
                         UBlockRegistry &registry, glm::ivec3 coord,
                         int slot_idx, GpuApplyTicket &out_ticket);
  enum class GpuFinishStatus : uint8_t
  {
    Ready = 0,
    NotReady = 1,
    Failed = 2,
  };
  /// Poll counter fence (timeout_ns=0), map, packed emit, CopyQuads fence.
  GpuFinishStatus TryCompleteCountersAndEmit(GpuApplyTicket &ticket,
                                            UBlockRegistry &registry,
                                            uint64_t timeout_ns);
  /// Poll fence (timeout_ns=0 non-blocking), map PBO, build RLE ranges.
  GpuFinishStatus TryFinishComputePasses(
      GpuApplyTicket &ticket, UBlockRegistry &registry, uint32_t &out_quad_count,
      std::vector<GpuBlockDrawRange> *out_ranges, bool *out_has_dark_face,
      uint64_t timeout_ns);
  /// Blocking Finish (up to 100ms) for sync ProcessSnapshot path.
  bool FinishComputePasses(GpuApplyTicket &ticket, UBlockRegistry &registry,
                           uint32_t &out_quad_count,
                           std::vector<GpuBlockDrawRange> *out_ranges,
                           bool *out_has_dark_face);

  /// Free ring PBO held by ticket (CancelOutside / fail paths).
  void ReleaseReadbackSlot(GpuApplyTicket &ticket);
  bool HasFreeReadbackSlot() const;

  /// Process queued snapshots: upload → compute → write to SSBO slots.
  int ProcessQueue(UBlockRegistry &registry, int budget);

  void FreeChunk(glm::ivec3 chunk_coord);

  bool HasGpuMesh(glm::ivec3 chunk_coord) const;

  UGpuMeshSlotAllocator &GetAllocator() { return Allocator; }
  const UGpuMeshSlotAllocator &GetAllocator() const { return Allocator; }

  int GetPendingCount() const { return static_cast<int>(PendingQueue.size()); }

private:
  struct PendingChunk
  {
    ChunkMeshSnapshot Snapshot;
    uint64_t SourceRevision;
  };

  bool RunComputePasses(const ChunkMeshSnapshot &snapshot,
                        UBlockRegistry &registry, glm::ivec3 coord,
                        int slot_idx, uint32_t &out_quad_count,
                        std::vector<GpuBlockDrawRange> *out_ranges = nullptr,
                        bool *out_has_dark_face = nullptr);

  /// GPU counting-sort in-slot; downloads histogram only (not full quads).
  bool GpuSortSlotQuads(uint32_t slot_offset, uint32_t num_quads,
                        UBlockRegistry &registry,
                        std::vector<GpuBlockDrawRange> *out_ranges,
                        bool *out_has_dark_face);

  void ShutdownGpuSort();
  void EnsureReadbackPbo();
  void DestroyReadbackPbo();
  int AcquireReadbackSlot();
  bool ReadCountersViaPbo(int pbo_index, std::array<uint32_t, 4> &out_counters);
  bool CopyQuadsToPbo(int pbo_index, uint32_t slot_offset, uint32_t quad_count,
                      GLsync *out_fence);
  /// timeout_ns=0 → poll; fence kept on NotReady (TIMEOUT_EXPIRED).
  GpuFinishStatus MapQuadsFromPbo(int pbo_index, uint32_t quad_count,
                                  GLsync *inout_fence, uint64_t timeout_ns);

  bool Ready{false};
  GpuGreedyEmitState EmitState;
  UGpuMeshSlotAllocator Allocator;
  std::queue<PendingChunk> PendingQueue;

#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  GLuint SortHistProgram{0};
  GLuint SortScatterProgram{0};
  GLuint SortCountsSsbo{0};  // 1025 uint: hist[1024] + dark flag
  GLuint SortOffsetsSsbo{0}; // 1024 uint exclusive prefix (mutated by scatter)
  GLuint SortScratchSsbo{0}; // kMaxQuadsPerSlot PackedQuads
  /// Ring of pack=counters(16)+PackedQuad[kMaxQuads] — multi in-flight apply.
  GLuint ReadbackPbos[kReadbackRing]{};
  bool ReadbackInUse[kReadbackRing]{};
  /// Per-slot greedy rect hold — frees EmitState.RectsSsbo for next Kick.
  GLuint RectsHoldSsbo[kReadbackRing]{};
  static constexpr GLintptr kReadbackCountersBytes = 16;
  static constexpr GLintptr kReadbackQuadsOffset = 16;
#endif

  /// CPU fallback when GPU sort programs unavailable.
  std::vector<PackedQuad> ScratchQuads;
  std::vector<PackedQuad> ScratchQuadsSorted;
};

} // namespace cutum
