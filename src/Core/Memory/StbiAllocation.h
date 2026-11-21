#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace cutum
{
// Allocator requests only: includes our prefix, excludes CRT headers, streams,
// decoder stack, shared ledger/control allocation and downstream vector/GL data.
class UStbiAllocationLedger
{
public:
  struct Snapshot
  {
    size_t live_payload_bytes{}, live_request_bytes{}, live_allocations{};
    size_t reserved_request_bytes{}, peak_admitted_request_bytes{}, peak_live_request_bytes{};
    size_t allocation_attempts{}, successful_allocations{}, refusals{}, system_failures{};
    size_t reallocations{}, failed_reallocations{};
  };
  explicit UStbiAllocationLedger(size_t byteLimit=std::numeric_limits<size_t>::max(),
                                 size_t failAfter=std::numeric_limits<size_t>::max())
      : Limit(byteLimit),FailAfter(failAfter) {}
  Snapshot GetSnapshot() const {std::lock_guard<std::mutex> lock(Mutex);return Counts;}
  size_t LimitBytes() const {return Limit;}

  // Internal allocator transitions. Reservation precedes malloc; the admitted
  // peak is a conservative envelope even with concurrent/pending/failed malloc.
  bool Reserve(size_t bytes) {
    std::lock_guard<std::mutex> lock(Mutex);
    const bool injected=Counts.allocation_attempts>=FailAfter;
    Increment(Counts.allocation_attempts);
    const size_t held=Counts.live_request_bytes+Counts.reserved_request_bytes;
    if(injected || held>Limit || bytes>Limit-held) {Increment(Counts.refusals);return false;}
    Counts.reserved_request_bytes+=bytes;
    Counts.peak_admitted_request_bytes=std::max(Counts.peak_admitted_request_bytes,held+bytes);
    return true;
  }
  void InvalidSize() {
    std::lock_guard<std::mutex> lock(Mutex);Increment(Counts.allocation_attempts);Increment(Counts.refusals);
  }
  void Cancel(size_t bytes,bool systemFailure) {
    std::lock_guard<std::mutex> lock(Mutex);Counts.reserved_request_bytes-=bytes;
    if(systemFailure) Increment(Counts.system_failures);
  }
  void Commit(size_t bytes,size_t payload) {
    std::lock_guard<std::mutex> lock(Mutex);Counts.reserved_request_bytes-=bytes;
    Counts.live_request_bytes+=bytes;Counts.live_payload_bytes+=payload;++Counts.live_allocations;
    Counts.peak_live_request_bytes=std::max(Counts.peak_live_request_bytes,Counts.live_request_bytes);
    Increment(Counts.successful_allocations);
  }
  void Release(size_t bytes,size_t payload) {
    std::lock_guard<std::mutex> lock(Mutex);Counts.live_request_bytes-=bytes;
    Counts.live_payload_bytes-=payload;--Counts.live_allocations;
  }
  void Replace(size_t oldBytes,size_t oldPayload,size_t newBytes,size_t newPayload) {
    std::lock_guard<std::mutex> lock(Mutex);Counts.reserved_request_bytes-=newBytes;
    Counts.live_request_bytes=Counts.live_request_bytes-oldBytes+newBytes;
    Counts.live_payload_bytes=Counts.live_payload_bytes-oldPayload+newPayload;
    Counts.peak_live_request_bytes=std::max(Counts.peak_live_request_bytes,Counts.live_request_bytes);
    Increment(Counts.successful_allocations);
  }
  void Reallocation(bool success) {
    std::lock_guard<std::mutex> lock(Mutex);Increment(Counts.reallocations);
    if(!success) Increment(Counts.failed_reallocations);
  }
private:
  static void Increment(size_t &count) {if(count<std::numeric_limits<size_t>::max()) ++count;}
  mutable std::mutex Mutex;
  Snapshot Counts;
  const size_t Limit,FailAfter;
};

namespace detail
{
inline UStbiAllocationLedger StbiProcessLedger;
inline thread_local std::shared_ptr<UStbiAllocationLedger> ActiveStbiLedger;
struct alignas(std::max_align_t) StbiAllocationHeader
{
  size_t PayloadBytes;
  std::shared_ptr<UStbiAllocationLedger> Owner;
};
static_assert(sizeof(StbiAllocationHeader)%alignof(std::max_align_t)==0);
// MSVC max_align_t can be 8 while the x64 CRT malloc guarantee is 16.
// Preserve that alignment as well, without increasing the header object's
// alignment beyond what malloc guarantees on other targets.
inline constexpr size_t StbiPrefixBytes=(sizeof(StbiAllocationHeader)+15)/16*16;
static_assert(StbiPrefixBytes%alignof(std::max_align_t)==0);
inline StbiAllocationHeader *StbiHeader(void *pointer)
{return reinterpret_cast<StbiAllocationHeader *>(static_cast<unsigned char *>(pointer)-StbiPrefixBytes);}
inline const StbiAllocationHeader *StbiHeader(const void *pointer)
{return reinterpret_cast<const StbiAllocationHeader *>(static_cast<const unsigned char *>(pointer)-StbiPrefixBytes);}

inline void *AllocateStbiForOwner(size_t bytes,const std::shared_ptr<UStbiAllocationLedger> &owner) noexcept
{
  constexpr size_t prefix=StbiPrefixBytes;
  if(bytes>std::numeric_limits<size_t>::max()-prefix) {
    StbiProcessLedger.InvalidSize();if(owner) owner->InvalidSize();return nullptr;
  }
  const size_t requested=prefix+bytes;
  if(!StbiProcessLedger.Reserve(requested)) return nullptr;
  if(owner && !owner->Reserve(requested)) {StbiProcessLedger.Cancel(requested,false);return nullptr;}
  void *memory=std::malloc(requested);
  if(!memory) {
    StbiProcessLedger.Cancel(requested,true);if(owner) owner->Cancel(requested,true);return nullptr;
  }
  ::new(memory) StbiAllocationHeader{bytes,owner};
  StbiProcessLedger.Commit(requested,bytes);if(owner) owner->Commit(requested,bytes);
  return static_cast<unsigned char *>(memory)+prefix;
}
} // namespace detail

// Optional synchronous diagnostic scope. Each allocation keeps this ledger
// through its original last-free, including escaped output or another thread.
// Default gameplay decoding has no new byte limit/failure injection.
class UStbiAllocationScope
{
public:
  explicit UStbiAllocationScope(std::shared_ptr<UStbiAllocationLedger> ledger)
      : Previous(std::move(detail::ActiveStbiLedger)) {detail::ActiveStbiLedger=std::move(ledger);}
  ~UStbiAllocationScope() {detail::ActiveStbiLedger=std::move(Previous);}
  UStbiAllocationScope(const UStbiAllocationScope &)=delete;
  UStbiAllocationScope &operator=(const UStbiAllocationScope &)=delete;
private:
  std::shared_ptr<UStbiAllocationLedger> Previous;
};

inline auto GetStbiProcessAllocationSnapshot() {return detail::StbiProcessLedger.GetSnapshot();}
inline constexpr size_t StbiAllocationHeaderBytes() {return detail::StbiPrefixBytes;}
struct StbiAllocationInfo
{
  size_t payload_bytes{},request_bytes{};
  std::shared_ptr<const UStbiAllocationLedger> owner;
};
// Only a live pointer returned by our STBI implementation may be inspected.
inline StbiAllocationInfo InspectStbiAllocation(const void *pointer)
{
  if(!pointer) return {};
  const auto *header=detail::StbiHeader(pointer);
  return {header->PayloadBytes,header->PayloadBytes+detail::StbiPrefixBytes,header->Owner};
}
inline void *StbiAllocate(size_t bytes) noexcept
{return detail::AllocateStbiForOwner(bytes,detail::ActiveStbiLedger);}
inline void StbiFree(void *pointer) noexcept
{
  if(!pointer) return;
  auto *header=detail::StbiHeader(pointer);
  const size_t payload=header->PayloadBytes,requested=payload+detail::StbiPrefixBytes;
  auto owner=std::move(header->Owner);
  header->~StbiAllocationHeader();std::free(header);
  detail::StbiProcessLedger.Release(requested,payload);if(owner) owner->Release(requested,payload);
}
inline void *StbiReallocate(void *pointer,size_t bytes) noexcept
{
  if(!pointer) return StbiAllocate(bytes);
  if(!bytes) {StbiFree(pointer);return nullptr;}
  auto *header=detail::StbiHeader(pointer);
  // Realloc belongs to the original allocation, not a later/nested caller.
  const auto owner=header->Owner;
  const size_t oldPayload=header->PayloadBytes,oldRequest=oldPayload+detail::StbiPrefixBytes;
  const auto failed=[&] {
    detail::StbiProcessLedger.Reallocation(false);if(owner) owner->Reallocation(false);
  };
  if(bytes>std::numeric_limits<size_t>::max()-detail::StbiPrefixBytes) {
    detail::StbiProcessLedger.InvalidSize();if(owner) owner->InvalidSize();failed();return nullptr;
  }
  const size_t requested=bytes+detail::StbiPrefixBytes;
  if(!detail::StbiProcessLedger.Reserve(requested)) {failed();return nullptr;}
  if(owner && !owner->Reserve(requested)) {detail::StbiProcessLedger.Cancel(requested,false);failed();return nullptr;}
  // Do not bit-relocate a live shared_ptr object. Its retained owner covers
  // both outcomes while CRT realloc preserves payload and may grow in place.
  header->~StbiAllocationHeader();
  void *memory=std::realloc(header,requested);
  if(!memory) {
    ::new(header) detail::StbiAllocationHeader{oldPayload,owner};
    detail::StbiProcessLedger.Cancel(requested,true);if(owner) owner->Cancel(requested,true);
    failed();return nullptr;
  }
  ::new(memory) detail::StbiAllocationHeader{bytes,owner};
  detail::StbiProcessLedger.Replace(oldRequest,oldPayload,requested,bytes);
  if(owner) owner->Replace(oldRequest,oldPayload,requested,bytes);
  detail::StbiProcessLedger.Reallocation(true);if(owner) owner->Reallocation(true);
  return static_cast<unsigned char *>(memory)+detail::StbiPrefixBytes;
}
} // namespace cutum
