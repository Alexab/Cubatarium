#pragma once

#include "Render/GlIncludes.h"
#include "Render/Mesh/PackedQuad.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

/// Fixed-size slot within the GPU quad SSBO. Each chunk gets one slot.
struct GpuMeshSlot
{
  uint32_t OffsetQuads{0};
  uint32_t QuadCount{0};
  glm::ivec3 ChunkCoord{0};
  bool Transparent{false};
};

/// DrawElementsIndirectCommand for glMultiDrawElementsIndirect.
struct DrawIndirectCmd
{
  uint32_t count;
  uint32_t instanceCount;
  uint32_t firstIndex;
  int32_t baseVertex;
  uint32_t baseInstance;
};

/// Manages a persistent SSBO of PackedQuads with a free-list slot allocator.
/// Each chunk occupies a fixed-size slot (kMaxQuadsPerSlot). Slots are
/// allocated/freed CPU-side; quad data is written GPU-side by the packed emit
/// compute shader. A separate indirect command buffer is rebuilt each frame
/// for the visible set (after frustum cull).
class UGpuMeshSlotAllocator
{
public:
  static constexpr uint32_t kMaxQuadsPerSlot = 2048;
  static constexpr uint32_t kBytesPerSlot =
      kMaxQuadsPerSlot * sizeof(PackedQuad);

  bool Init(uint32_t max_slots);
  void Shutdown();

  /// Allocate a slot for a chunk. Returns slot index or -1 on failure.
  int AllocateSlot(glm::ivec3 chunk_coord, bool transparent);

  /// Free a slot previously allocated for a chunk.
  void FreeSlot(glm::ivec3 chunk_coord);

  /// Returns true if the chunk has an allocated slot.
  bool HasSlot(glm::ivec3 chunk_coord) const;

  /// Get the slot for a chunk. Returns nullptr if not allocated.
  const GpuMeshSlot *GetSlot(glm::ivec3 chunk_coord) const;

  /// Update the quad count for a slot after GPU compute writes the data.
  void SetSlotQuadCount(int slot_index, uint32_t quad_count);

  /// Build indirect draw commands for all allocated opaque slots.
  /// Returns the number of commands written.
  int BuildOpaqueDrawCommands();

  /// Build indirect draw commands for all allocated transparent slots.
  int BuildTransparentDrawCommands();

  /// Issue the MDI draw call for opaque geometry.
  void DrawOpaque() const;

  /// Issue the MDI draw call for transparent geometry.
  void DrawTransparent() const;

  GLuint GetQuadSsbo() const { return QuadSsbo; }
  GLuint GetIndirectBuffer() const { return IndirectBuffer; }
  uint32_t GetMaxSlots() const { return MaxSlots; }

private:
  GLuint QuadSsbo{0};
  GLuint IndirectBuffer{0};
  uint32_t MaxSlots{0};
  std::vector<int> FreeList;
  std::vector<GpuMeshSlot> Slots;
  struct IVec3Hash
  {
    size_t operator()(const glm::ivec3 &v) const
    {
      return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 11) ^
             (std::hash<int>()(v.z) << 22);
    }
  };
  std::unordered_map<glm::ivec3, int, IVec3Hash> ChunkToSlot;
  std::vector<DrawIndirectCmd> OpaqueCommands;
  std::vector<DrawIndirectCmd> TransparentCommands;
};

} // namespace cutum
