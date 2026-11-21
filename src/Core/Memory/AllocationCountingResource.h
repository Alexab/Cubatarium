#pragma once

#include <algorithm>
#include <cstddef>
#include <memory_resource>

namespace cutum
{

// Owner-thread accounting of allocator requests, including container nodes.
// Does not estimate upstream heap headers, fragmentation or retained OS pages.
class UAllocationCountingResource final : public std::pmr::memory_resource
{
public:
  struct Snapshot
  {
    size_t live_bytes{0};
    size_t peak_bytes{0};
    size_t live_allocations{0};
  };

  Snapshot GetSnapshot() const { return Counts; }

private:
  void *do_allocate(size_t bytes, size_t alignment) override
  {
    void *result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    Counts.live_bytes += bytes;
    Counts.peak_bytes = std::max(Counts.peak_bytes, Counts.live_bytes);
    ++Counts.live_allocations;
    return result;
  }

  void do_deallocate(void *ptr, size_t bytes, size_t alignment) override
  {
    std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
    Counts.live_bytes -= bytes;
    --Counts.live_allocations;
  }

  bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
  {
    return this == &other;
  }

  Snapshot Counts;
};

} // namespace cutum
