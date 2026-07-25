#ifndef NAVIGATIONPATHBUDGET_H
#define NAVIGATIONPATHBUDGET_H

namespace cutum
{

/// Global A* expand budget shared across all creatures for one activity tick.
class UNavigationPathBudget
{
public:
  static constexpr int kDefaultExpandsPerTick = 800;

  static void BeginActivityTick();
  static void SetExpandsPerTick(int expands);
  static int GetExpandsPerTick();
  static int GetExpandsUsed();
  static int GetExpandsRemaining();
  /// Consume one node expand; returns false when the tick budget is exhausted.
  static bool TryConsumeExpand();
  static bool HasRemainingBudget();
};

} // namespace cutum

#endif
