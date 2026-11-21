#pragma once

#include "Core/Memory/BufferAllocationBudget.h"
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace cutum
{
// Algorithm containers can escape the active scope (e.g. returned biome
// blends). The allocator owns the original job context through final free.
template<class T> struct UGenerationAllocator
{
  using value_type=T;
  using propagate_on_container_copy_assignment=std::true_type;
  using propagate_on_container_move_assignment=std::true_type;
  using propagate_on_container_swap=std::true_type;
  using is_always_equal=std::false_type;
  BufferBudgetContext Owner;

  UGenerationAllocator() noexcept : Owner(ActiveBufferBudget) {}
  explicit UGenerationAllocator(BufferBudgetContext owner) noexcept : Owner(std::move(owner)) {}
  UGenerationAllocator(const UGenerationAllocator &)=default;
  UGenerationAllocator &operator=(const UGenerationAllocator &)=default;
  // A moved-from vector can be reused and must retain its allocator's owner.
  UGenerationAllocator(UGenerationAllocator &&other) noexcept : Owner(other.Owner) {}
  UGenerationAllocator &operator=(UGenerationAllocator &&other) noexcept
  { Owner=other.Owner; return *this; }
  template<class U> UGenerationAllocator(const UGenerationAllocator<U> &other) noexcept : Owner(other.Owner) {}

  T *allocate(size_t count) {
    if(count>std::numeric_limits<size_t>::max()/sizeof(T)) throw std::bad_array_new_length{};
    const size_t bytes=count*sizeof(T);
    if(Owner.budget) Owner.budget->Reserve(*Owner.job,bytes);
    try { return std::allocator<T>{}.allocate(count); }
    catch(...) {
      if(Owner.budget) Owner.budget->Release(*Owner.job,bytes);
      throw;
    }
  }
  void deallocate(T *ptr,size_t count) noexcept {
    std::allocator<T>{}.deallocate(ptr,count);
    if(Owner.budget) Owner.budget->Release(*Owner.job,count*sizeof(T));
  }
  template<class U> bool operator==(const UGenerationAllocator<U> &other) const noexcept
  {return Owner.budget==other.Owner.budget && Owner.job==other.Owner.job;}
  template<class U> bool operator!=(const UGenerationAllocator<U> &other) const noexcept
  {return !(*this==other);}
};

template<class T> using GenerationVector=std::vector<T,UGenerationAllocator<T>>;
} // namespace cutum
