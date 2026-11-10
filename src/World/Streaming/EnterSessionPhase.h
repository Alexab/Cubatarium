#pragma once

#include <cstdint>

namespace cutum
{

/// Enter-load lifecycle (menu → InGame). Distinct from EnterLitGateActive (lit
/// drain only during GpuWarmup).
enum class EnterSessionPhase : uint8_t
{
  None = 0,
  CooperativeLoad,
  GpuWarmup,
  Quiesce,
  Done,
};

inline bool IsEnterSessionPhaseActive(EnterSessionPhase phase)
{
  return phase != EnterSessionPhase::None &&
         phase != EnterSessionPhase::Done;
}

/// @deprecated Use IsEnterSessionPhaseActive — kept for policy tests.
inline bool IsEnterSessionActive(EnterSessionPhase phase)
{
  return IsEnterSessionPhaseActive(phase);
}

} // namespace cutum
