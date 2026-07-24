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
  Shutdown,
  ApplySettings
};

struct ProgressSnapshot
{
  WorldOperationKind kind{WorldOperationKind::Load};
  std::string phaseId;
  std::string message;
  /// 0..1 for determinate progress; negative = indeterminate.
  float fraction{0.f};
  bool active{false};
  bool succeeded{true};
  std::string errorMessage;
};

} // namespace cutum
