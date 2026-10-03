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
};

inline const char *WorldDifficultyToString(WorldDifficulty difficulty)
{
  switch (difficulty)
  {
  case WorldDifficulty::Peaceful:
    return "peaceful";
  case WorldDifficulty::Easy:
    return "easy";
  case WorldDifficulty::Normal:
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
  return WorldDifficulty::Normal;
}

} // namespace cutum

#endif
