#pragma once

#include <cstdint>
#include <memory>

namespace cutum
{
class UGpuMeshSlotAllocator;

/// Identity of one unpublished allocation, independent of its numeric index.
/// Keeping the allocator token alive also prevents allocator-object ABA.
class GpuStagingSlotKey
{
public:
  int Index() const { return Index_; }
  uint64_t Generation() const { return Generation_; }
  bool IsValid() const
  {
    return Index_ >= 0 && Generation_ != 0 && bool(AllocatorIdentity_);
  }

private:
  friend class UGpuMeshSlotAllocator;
  int Index_{-1};
  uint64_t Generation_{0};
  std::shared_ptr<const uint8_t> AllocatorIdentity_;
};
} // namespace cutum
