#include "Navigation/NavigationPathBudget.h"
#include <algorithm>
#include <atomic>

namespace cutum
{
namespace
{

std::atomic<int> &ExpandsPerTick()
{
  static std::atomic<int> limit{UNavigationPathBudget::kDefaultExpandsPerTick};
  return limit;
}

std::atomic<int> &ExpandsUsed()
{
  static std::atomic<int> used{0};
  return used;
}

} // namespace

void UNavigationPathBudget::BeginActivityTick()
{
  ExpandsUsed().store(0, std::memory_order_relaxed);
}

void UNavigationPathBudget::SetExpandsPerTick(int expands)
{
  ExpandsPerTick().store(std::max(1, expands), std::memory_order_relaxed);
}

int UNavigationPathBudget::GetExpandsPerTick()
{
  return ExpandsPerTick().load(std::memory_order_relaxed);
}

int UNavigationPathBudget::GetExpandsUsed()
{
  return ExpandsUsed().load(std::memory_order_relaxed);
}

int UNavigationPathBudget::GetExpandsRemaining()
{
  return std::max(0, GetExpandsPerTick() - GetExpandsUsed());
}

bool UNavigationPathBudget::HasRemainingBudget()
{
  return GetExpandsRemaining() > 0;
}

bool UNavigationPathBudget::TryConsumeExpand()
{
  const int limit = GetExpandsPerTick();
  int used = ExpandsUsed().load(std::memory_order_relaxed);
  while (used < limit)
  {
    if (ExpandsUsed().compare_exchange_weak(used, used + 1,
                                            std::memory_order_relaxed))
    {
      return true;
    }
  }
  return false;
}

} // namespace cutum
