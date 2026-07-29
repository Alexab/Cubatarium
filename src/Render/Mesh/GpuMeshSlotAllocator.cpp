#include "Render/Mesh/GpuMeshSlotAllocator.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"

namespace cutum
{

bool UGpuMeshSlotAllocator::Init(uint32_t max_slots)
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)max_slots;
  return false;
#else
  MaxSlots = max_slots;
  Slots.resize(max_slots);
  FreeList.reserve(max_slots);
  for (int i = static_cast<int>(max_slots) - 1; i >= 0; --i)
  {
    FreeList.push_back(i);
  }

  glGenBuffers(1, &QuadSsbo);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, QuadSsbo);
  const GLsizeiptr total_bytes =
      static_cast<GLsizeiptr>(max_slots) * kBytesPerSlot;
  glBufferData(GL_SHADER_STORAGE_BUFFER, total_bytes, nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  glGenBuffers(1, &IndirectBuffer);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
  glBufferData(GL_DRAW_INDIRECT_BUFFER,
               static_cast<GLsizeiptr>(max_slots * sizeof(DrawIndirectCmd)),
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

  return true;
#endif
}

void UGpuMeshSlotAllocator::Shutdown()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (QuadSsbo)
  {
    glDeleteBuffers(1, &QuadSsbo);
    QuadSsbo = 0;
  }
  if (IndirectBuffer)
  {
    glDeleteBuffers(1, &IndirectBuffer);
    IndirectBuffer = 0;
  }
#endif
  Slots.clear();
  FreeList.clear();
  ChunkToSlot.clear();
}

int UGpuMeshSlotAllocator::AllocateSlot(glm::ivec3 chunk_coord,
                                        bool transparent)
{
  auto it = ChunkToSlot.find(chunk_coord);
  if (it != ChunkToSlot.end())
  {
    Slots[static_cast<size_t>(it->second)].Transparent = transparent;
    return it->second;
  }
  if (FreeList.empty())
  {
    return -1;
  }
  const int slot_idx = FreeList.back();
  FreeList.pop_back();
  GpuMeshSlot &slot = Slots[static_cast<size_t>(slot_idx)];
  slot.OffsetQuads = static_cast<uint32_t>(slot_idx) * kMaxQuadsPerSlot;
  slot.QuadCount = 0;
  slot.ChunkCoord = chunk_coord;
  slot.Transparent = transparent;
  ChunkToSlot[chunk_coord] = slot_idx;
  return slot_idx;
}

void UGpuMeshSlotAllocator::FreeSlot(glm::ivec3 chunk_coord)
{
  auto it = ChunkToSlot.find(chunk_coord);
  if (it == ChunkToSlot.end())
  {
    return;
  }
  const int slot_idx = it->second;
  Slots[static_cast<size_t>(slot_idx)].QuadCount = 0;
  FreeList.push_back(slot_idx);
  ChunkToSlot.erase(it);
}

bool UGpuMeshSlotAllocator::HasSlot(glm::ivec3 chunk_coord) const
{
  return ChunkToSlot.find(chunk_coord) != ChunkToSlot.end();
}

const GpuMeshSlot *
UGpuMeshSlotAllocator::GetSlot(glm::ivec3 chunk_coord) const
{
  auto it = ChunkToSlot.find(chunk_coord);
  if (it == ChunkToSlot.end())
  {
    return nullptr;
  }
  return &Slots[static_cast<size_t>(it->second)];
}

void UGpuMeshSlotAllocator::SetSlotQuadCount(int slot_index,
                                             uint32_t quad_count)
{
  if (slot_index < 0 || slot_index >= static_cast<int>(MaxSlots))
  {
    return;
  }
  Slots[static_cast<size_t>(slot_index)].QuadCount =
      std::min(quad_count, kMaxQuadsPerSlot);
}

int UGpuMeshSlotAllocator::BuildOpaqueDrawCommands()
{
  OpaqueCommands.clear();
  for (const auto &entry : ChunkToSlot)
  {
    const GpuMeshSlot &slot = Slots[static_cast<size_t>(entry.second)];
    if (slot.QuadCount == 0 || slot.Transparent)
    {
      continue;
    }
    DrawIndirectCmd cmd;
    cmd.count = slot.QuadCount * 6;
    cmd.instanceCount = 1;
    cmd.firstIndex = slot.OffsetQuads * 6;
    cmd.baseVertex = 0;
    cmd.baseInstance = static_cast<uint32_t>(entry.second);
    OpaqueCommands.push_back(cmd);
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!OpaqueCommands.empty())
  {
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                    static_cast<GLsizeiptr>(OpaqueCommands.size() *
                                            sizeof(DrawIndirectCmd)),
                    OpaqueCommands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  }
#endif
  return static_cast<int>(OpaqueCommands.size());
}

int UGpuMeshSlotAllocator::BuildTransparentDrawCommands()
{
  TransparentCommands.clear();
  for (const auto &entry : ChunkToSlot)
  {
    const GpuMeshSlot &slot = Slots[static_cast<size_t>(entry.second)];
    if (slot.QuadCount == 0 || !slot.Transparent)
    {
      continue;
    }
    DrawIndirectCmd cmd;
    cmd.count = slot.QuadCount * 6;
    cmd.instanceCount = 1;
    cmd.firstIndex = slot.OffsetQuads * 6;
    cmd.baseVertex = 0;
    cmd.baseInstance = static_cast<uint32_t>(entry.second);
    TransparentCommands.push_back(cmd);
  }
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!TransparentCommands.empty())
  {
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
    const GLintptr offset =
        static_cast<GLintptr>(OpaqueCommands.size() * sizeof(DrawIndirectCmd));
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, offset,
                    static_cast<GLsizeiptr>(TransparentCommands.size() *
                                            sizeof(DrawIndirectCmd)),
                    TransparentCommands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
  }
#endif
  return static_cast<int>(TransparentCommands.size());
}

void UGpuMeshSlotAllocator::DrawOpaque() const
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (OpaqueCommands.empty())
  {
    return;
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
  glMultiDrawElementsIndirect(
      GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
      static_cast<GLsizei>(OpaqueCommands.size()),
      sizeof(DrawIndirectCmd));
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#endif
}

void UGpuMeshSlotAllocator::DrawTransparent() const
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (TransparentCommands.empty())
  {
    return;
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, IndirectBuffer);
  const void *offset = reinterpret_cast<const void *>(
      OpaqueCommands.size() * sizeof(DrawIndirectCmd));
  glMultiDrawElementsIndirect(
      GL_TRIANGLES, GL_UNSIGNED_INT, offset,
      static_cast<GLsizei>(TransparentCommands.size()),
      sizeof(DrawIndirectCmd));
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#endif
}

} // namespace cutum
