#ifndef WORLD_DIFFICULTY_H
#define WORLD_DIFFICULTY_H

#include <string>

namespace cutum
{

enum class WorldDifficulty
{
  Peaceful,
  Easy,
  Normal,
  Hard,
  Hardcore,
};

struct WorldDifficultyProfile
{
  float NeedsDrainMultiplier{1.f};
  float HostileSpawnPressureMultiplier{1.f};
  float HostileDamageMultiplier{1.f};
};

inline WorldDifficultyProfile
WorldDifficultyProfileFor(WorldDifficulty difficulty)
{
  switch (difficulty)
  {
  case WorldDifficulty::Peaceful:
    return {0.f, 0.f, 0.f};
  case WorldDifficulty::Easy:
    return {0.5f, 0.5f, 0.75f};
  case WorldDifficulty::Hard:
  case WorldDifficulty::Hardcore:
    return {1.5f, 1.5f, 1.25f};
  case WorldDifficulty::Normal:
  default:
    return {1.f, 1.f, 1.f};
  }
}

inline const char *WorldDifficultyToString(WorldDifficulty difficulty)
{
  switch (difficulty)
  {
  case WorldDifficulty::Peaceful:
    return "peaceful";
  case WorldDifficulty::Easy:
    return "easy";
  case WorldDifficulty::Normal:
    return "normal";
  case WorldDifficulty::Hard:
    return "hard";
  case WorldDifficulty::Hardcore:
    return "hardcore";
  default:
    return "normal";
  }
}

inline WorldDifficulty WorldDifficultyFromString(const std::string &value)
{
  if (value == "peaceful")
  {
    return WorldDifficulty::Peaceful;
  }
  if (value == "easy")
  {
    return WorldDifficulty::Easy;
  }
  if (value == "hard")
  {
    return WorldDifficulty::Hard;
  }
  if (value == "hardcore")
  {
    return WorldDifficulty::Hardcore;
  }
  return WorldDifficulty::Normal;
}

} // namespace cutum

#endif
