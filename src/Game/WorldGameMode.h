#ifndef WORLD_GAME_MODE_H
#define WORLD_GAME_MODE_H

#include <string>

namespace cutum
{

enum class WorldGameMode
{
  Creative,
  Survival,
};

inline const char *WorldGameModeToString(WorldGameMode mode)
{
  switch (mode)
  {
  case WorldGameMode::Survival:
    return "survival";
  case WorldGameMode::Creative:
  default:
    return "creative";
  }
}

inline WorldGameMode WorldGameModeFromString(const std::string &value)
{
  if (value == "survival")
  {
    return WorldGameMode::Survival;
  }
  return WorldGameMode::Creative;
}

} // namespace cutum

#endif
