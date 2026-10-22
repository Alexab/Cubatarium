#ifndef WORLDMETADATAJSON_H
#define WORLDMETADATAJSON_H

#include "Game/WorldDifficulty.h"

#include <nlohmann/json.hpp>

namespace cutum
{

inline WorldDifficulty ReadWorldDifficulty(const nlohmann::json &data)
{
  if (data.contains("difficulty") && data["difficulty"].is_string())
  {
    return WorldDifficultyFromString(data["difficulty"].get<std::string>());
  }
  return WorldDifficulty::Normal;
}

inline bool ReadHardcoreWorldCompleted(const nlohmann::json &data)
{
  return data.contains("hardcore_dead") && data["hardcore_dead"].is_boolean() &&
         data["hardcore_dead"].get<bool>();
}

struct WorldDeathState
{
  bool PlayerDead{false};
  bool HardcoreCompleted{false};
};

inline WorldDeathState ReadWorldDeathState(const nlohmann::json &data)
{
  const bool hardcoreCompleted = ReadHardcoreWorldCompleted(data);
  return {hardcoreCompleted, hardcoreCompleted};
}

inline void WriteWorldDifficulty(nlohmann::json &data,
                                 WorldDifficulty difficulty)
{
  data["difficulty"] = WorldDifficultyToString(difficulty);
}

inline void WriteHardcoreWorldCompleted(nlohmann::json &data, bool completed)
{
  data["hardcore_dead"] = completed;
}

} // namespace cutum

#endif
