#pragma once

#include "Core/Memory/AllocationCountingResource.h"
#include <atomic>
#include <exception>
#include <limits>
#include <memory>

namespace cutum
{
struct UBufferBudgetExceeded final : std::exception
{
  const char *what() const noexcept override { return "Buffer allocation quota temporarily unavailable"; }
};

struct BufferBudgetJob
{
  std::atomic<size_t> reserved{0};
};

class UBufferAllocationBudget
{
public:
  explicit UBufferAllocationBudget(size_t bytes) : Limit(bytes) {}
  size_t UsedBytes() const { return Used.load(std::memory_order_relaxed); }
  size_t PeakBytes() const { return Peak.load(std::memory_order_relaxed); }
  size_t LimitBytes() const { return Limit.load(std::memory_order_relaxed); }
  uint64_t RefusalCount() const { return Refusals.load(std::memory_order_relaxed); }
  void Reserve(BufferBudgetJob &job,size_t bytes)
  {
    size_t used=Used.load(std::memory_order_relaxed);
    for(;;) {
      if(bytes>std::numeric_limits<size_t>::max()-used) Deny();
      size_t limit=Limit.load(std::memory_order_relaxed);
      if(used>limit || bytes>limit-used) {
        // One valid oversized job can progress once competing owners retire.
        // Concurrent jobs still share one limit, never one limit per worker.
        if(used==job.reserved.load(std::memory_order_relaxed)) {
          const size_t needed=used+bytes;
          while(limit<needed && !Limit.compare_exchange_weak(limit,needed,std::memory_order_relaxed)) {}
          continue;
        }
        Deny();
      }
      if(Used.compare_exchange_weak(used,used+bytes,std::memory_order_relaxed)) break;
    }
    job.reserved.fetch_add(bytes,std::memory_order_relaxed);
    size_t peak=Peak.load(std::memory_order_relaxed);
    while(peak<used+bytes && !Peak.compare_exchange_weak(peak,used+bytes,std::memory_order_relaxed)) {}
  }
  void Release(BufferBudgetJob &job,size_t bytes)
  {
    job.reserved.fetch_sub(bytes,std::memory_order_relaxed);
    Used.fetch_sub(bytes,std::memory_order_relaxed);
  }
private:
  [[noreturn]] void Deny() { Refusals.fetch_add(1,std::memory_order_relaxed); throw UBufferBudgetExceeded{}; }
  std::atomic<size_t> Used{0},Peak{0},Limit;
  std::atomic<uint64_t> Refusals{0};
};

struct BufferBudgetContext
{
  std::shared_ptr<UBufferAllocationBudget> budget;
  std::shared_ptr<BufferBudgetJob> job;
};
class UConcurrentAllocationCountingResource;
enum class BufferJobSourceProfile:uint8_t {Unknown,Default,Counted};
// Borrowed owner-thread source slots. The enclosing scope/callable outlives
// this descriptor;it never adds an allocator or settings owner.
struct BufferBudgetJobSource {
  const std::shared_ptr<UConcurrentAllocationCountingResource> *origin{};
  const void *scheduler_settings_slot{};
  BufferJobSourceProfile profile{BufferJobSourceProfile::Unknown};
  bool requests_covered{};
};
inline thread_local BufferBudgetJobSource ActiveBufferBudgetJobSource;
// Named forms of the population resource callbacks keep their concrete
// storage and retained budget/job ownership available to the source model.
struct BufferBudgetReserveCallback {
  BufferBudgetContext context;
  void operator()(size_t bytes)const {context.budget->Reserve(*context.job,bytes);}
};
struct BufferBudgetReleaseCallback {
  BufferBudgetContext context;
  void operator()(size_t bytes)const {context.budget->Release(*context.job,bytes);}
};
inline thread_local BufferBudgetContext ActiveBufferBudget;
class UBufferBudgetScope
{
public:
  explicit UBufferBudgetScope(BufferBudgetContext context,BufferBudgetJobSource source={})
      : Previous(std::move(ActiveBufferBudget)),PreviousSource(ActiveBufferBudgetJobSource)
  { ActiveBufferBudget=std::move(context);ActiveBufferBudgetJobSource=source; }
  ~UBufferBudgetScope() { ActiveBufferBudget=std::move(Previous);ActiveBufferBudgetJobSource=PreviousSource; }
  UBufferBudgetScope(const UBufferBudgetScope &)=delete;
  UBufferBudgetScope &operator=(const UBufferBudgetScope &)=delete;
private:
  BufferBudgetContext Previous;
  BufferBudgetJobSource PreviousSource;
};

// Each buffer reserves node/bucket credit in bounded chunks to avoid a shared
// atomic operation for every voxel. Credit survives erasure until Clear/drop.
class UBufferAllocationResource final : public std::pmr::memory_resource
{
public:
  explicit UBufferAllocationResource(const BufferBudgetContext &owner) : Owner(owner) {}
  ~UBufferAllocationResource()
  { if(Owner.budget && Credit) Owner.budget->Release(*Owner.job,Credit); }
  UAllocationCountingResource::Snapshot GetSnapshot() const { return Counts; }
private:
  void *do_allocate(size_t bytes,size_t alignment) override
  {
    if(bytes>std::numeric_limits<size_t>::max()-Counts.live_bytes) throw UBufferBudgetExceeded{};
    const size_t needed=Counts.live_bytes+bytes;
    if(Owner.budget && needed>Credit) {
      constexpr size_t quantum=64*1024;
      const size_t delta=needed-Credit;
      size_t reserve=delta<=std::numeric_limits<size_t>::max()-(quantum-1)
          ? ((delta+quantum-1)/quantum)*quantum : delta;
      if(reserve>std::numeric_limits<size_t>::max()-Credit) reserve=delta;
      Owner.budget->Reserve(*Owner.job,reserve); Credit+=reserve;
    }
    void *result=std::pmr::new_delete_resource()->allocate(bytes,alignment);
    Counts.live_bytes+=bytes; ++Counts.live_allocations;
    Counts.peak_bytes=std::max(Counts.peak_bytes,Counts.live_bytes);
    return result;
  }
  void do_deallocate(void *pointer,size_t bytes,size_t alignment) override
  {
    std::pmr::new_delete_resource()->deallocate(pointer,bytes,alignment);
    Counts.live_bytes-=bytes; --Counts.live_allocations;
  }
  bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this==&other; }
  const BufferBudgetContext &Owner;
  size_t Credit{0};
  UAllocationCountingResource::Snapshot Counts;
};
} // namespace cutum
