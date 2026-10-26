#pragma once

#include <string>

namespace cutum
{

enum class WorldOperationKind
{
  Load,
  Create,
  Save,
  EnterGame,
  Shutdown
};

struct ProgressSnapshot
{
  WorldOperationKind kind{WorldOperationKind::Load};
  std::string phaseId;
  std::string message;
  /// 0..1 for determinate progress; negative = indeterminate.
  float fraction{0.f};
  /// Structured substage counters ("123/456"); negative = not reported.
  /// Screens render these instead of parsing message strings.
  int subDone{-1};
  int subTotal{-1};
  bool active{false};
  bool succeeded{true};
  std::string errorMessage;
};

} // namespace cutum
