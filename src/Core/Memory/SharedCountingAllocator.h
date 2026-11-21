#pragma once

#include "Core/Memory/AllocationCountingResource.h"
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <functional>

namespace cutum
{
// Control blocks may be freed by workers or by the last weak owner. Keep the
// resource alive in the allocator itself, and serialize allocation snapshots.
class UConcurrentAllocationCountingResource final : public std::pmr::memory_resource
{
public:
  UAllocationCountingResource::Snapshot GetSnapshot() const {
    std::lock_guard<std::mutex> lock(Mutex); return Memory.GetSnapshot();
  }
  void SetBeforeAllocationFn(std::function<void(size_t)> fn) {
    std::lock_guard<std::mutex> lock(Mutex); BeforeAllocation=std::move(fn);
  }
private:
  void *do_allocate(size_t bytes,size_t alignment) override {
    std::lock_guard<std::mutex> lock(Mutex);
    if(BeforeAllocation) BeforeAllocation(bytes);
    return Memory.allocate(bytes,alignment);
  }
  void do_deallocate(void *ptr,size_t bytes,size_t alignment) override {
    std::lock_guard<std::mutex> lock(Mutex); Memory.deallocate(ptr,bytes,alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {return this==&other;}
  mutable std::mutex Mutex;
  UAllocationCountingResource Memory;
  std::function<void(size_t)> BeforeAllocation;
};

template<class T> struct USharedCountingAllocator
{
  using value_type=T;
  using propagate_on_container_move_assignment=std::true_type;
  using propagate_on_container_swap=std::true_type;
  std::shared_ptr<UConcurrentAllocationCountingResource> resource;
  explicit USharedCountingAllocator(std::shared_ptr<UConcurrentAllocationCountingResource> owner)
      : resource(std::move(owner)) {}
  USharedCountingAllocator(const USharedCountingAllocator &)=default;
  USharedCountingAllocator &operator=(const USharedCountingAllocator &)=default;
  // A moved-from container must still have a usable allocator.
  USharedCountingAllocator(USharedCountingAllocator &&other) noexcept : resource(other.resource) {}
  USharedCountingAllocator &operator=(USharedCountingAllocator &&other) noexcept
  {resource=other.resource; return *this;}
  template<class U> USharedCountingAllocator(const USharedCountingAllocator<U> &other) noexcept
      : resource(other.resource) {}
  T *allocate(size_t count) {
    if(count>std::numeric_limits<size_t>::max()/sizeof(T)) throw std::bad_array_new_length{};
    return static_cast<T *>(resource->allocate(count*sizeof(T),alignof(T)));
  }
  void deallocate(T *ptr,size_t count) noexcept {resource->deallocate(ptr,count*sizeof(T),alignof(T));}
  template<class U> bool operator==(const USharedCountingAllocator<U> &other) const noexcept
  {return resource==other.resource;}
  template<class U> bool operator!=(const USharedCountingAllocator<U> &other) const noexcept
  {return !(*this==other);}
};
} // namespace cutum
