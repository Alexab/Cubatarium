#pragma once
#include "Core/Memory/SharedCountingAllocator.h"
#include <utility>

namespace cutum
{
inline thread_local std::shared_ptr<UConcurrentAllocationCountingResource> ActiveScopedSharedResource;
class UScopedSharedAllocationResource
{
public:
  explicit UScopedSharedAllocationResource(std::shared_ptr<UConcurrentAllocationCountingResource> resource) noexcept
      : Previous(std::move(ActiveScopedSharedResource)) {ActiveScopedSharedResource=std::move(resource);}
  ~UScopedSharedAllocationResource() {ActiveScopedSharedResource=std::move(Previous);}
  UScopedSharedAllocationResource(const UScopedSharedAllocationResource &)=delete;
  UScopedSharedAllocationResource &operator=(const UScopedSharedAllocationResource &)=delete;
private:
  std::shared_ptr<UConcurrentAllocationCountingResource> Previous;
};
template<class T,class... Args> std::shared_ptr<T> MakeScopedShared(Args &&...args)
{
  if(ActiveScopedSharedResource)
    return std::allocate_shared<T>(USharedCountingAllocator<T>{ActiveScopedSharedResource},std::forward<Args>(args)...);
  return std::make_shared<T>(std::forward<Args>(args)...);
}
} // namespace cutum
