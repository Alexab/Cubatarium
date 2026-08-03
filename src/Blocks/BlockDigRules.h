#ifndef BLOCKDIGRULES_H
#define BLOCKDIGRULES_H

#include "Game/WorldGameMode.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

/// Bare-hand dig timing from block hardness and game mode.
struct BlockDigRules
{
  static constexpr float DigSecondsPerHardness = 1.5f;
  static constexpr float DefaultHardness = 1.0f;

  /// Dig duration in seconds.
  /// 0 = instant (Creative). Negative = unbreakable (no progress in Survival).
  static float DigDurationSeconds(float hardness, WorldGameMode mode)
  {
    if (mode == WorldGameMode::Creative)
    {
      return 0.0f;
    }
    if (!(hardness > 0.0f))
    {
      return -1.0f;
    }
    return hardness * DigSecondsPerHardness;
  }

  static bool IsUnbreakableInSurvival(float hardness, WorldGameMode mode)
  {
    return mode != WorldGameMode::Creative && !(hardness > 0.0f);
  }

  /// Crack overlay stage index in [0, stage_count).
  static int CrackStageIndex(float progress, int stage_count = 10)
  {
    if (stage_count <= 0)
    {
      return 0;
    }
    if (!(progress > 0.0f))
    {
      return 0;
    }
    const int idx = static_cast<int>(std::floor(progress * static_cast<float>(stage_count)));
    return std::clamp(idx, 0, stage_count - 1);
  }
};

} // namespace cutum

#endif
