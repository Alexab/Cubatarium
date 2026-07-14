#include "Game/CreatureVisualQaSpawner.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraBasisLogic.h"
#include "World/Core/World.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cutum
{

UCreatureVisualQaSpawner::UCreatureVisualQaSpawner(UWorld &world) : World(world)
{
  RefreshQueue();
}

void UCreatureVisualQaSpawner::RefreshQueue()
{
  Queue.clear();
  const auto storage = World.GetCreatureDefinitionStorage();
  if (storage)
  {
    Queue = storage->ListSpawnable();
  }
  if (Index >= Queue.size())
  {
    Index = 0;
  }
}

glm::vec3 UCreatureVisualQaSpawner::GridOriginForIndex(size_t index,
                                                       size_t total) const
{
  constexpr int kCols = 10;
  constexpr float kRowSpacing = 3.0f;
  constexpr float kColSpacing = 2.0f;
  const int row = static_cast<int>(index / kCols);
  const int col = static_cast<int>(index % kCols);
  const int colCenter = kCols / 2;

  glm::vec3 forward(0.0f, 0.0f, -1.0f);
  glm::vec3 right(1.0f, 0.0f, 0.0f);
  glm::vec3 base(0.0f, 0.0f, 0.0f);
  if (auto camera = World.GetCurrentUserCamera())
  {
    base = camera->GetPosition();
    glm::vec3 up;
    ComputeFpsCameraBasis(camera->GetYaw(), camera->GetPitch(), forward, right,
                          up);
    forward.y = 0.0f;
    right.y = 0.0f;
    const float forwardLen = glm::length(forward);
    const float rightLen = glm::length(right);
    if (forwardLen > 1e-5f)
    {
      forward /= forwardLen;
    }
    if (rightLen > 1e-5f)
    {
      right /= rightLen;
    }
  }

  const float depth = 4.0f + static_cast<float>(row) * kRowSpacing;
  const float lateral = static_cast<float>(col - colCenter) * kColSpacing;
  (void)total;
  return base + forward * depth + right * lateral;
}

CreatureVisualQaSpawnResult UCreatureVisualQaSpawner::SpawnNextSpecies()
{
  CreatureVisualQaSpawnResult result;
  RefreshQueue();
  if (Queue.empty())
  {
    result.Message = "QA spawn: no spawnable species";
    return result;
  }

  if (LastSpawnedId != 0)
  {
    World.RemoveCreature(LastSpawnedId);
    LastSpawnedId = 0;
  }

  const std::string &species = Queue[Index];
  const size_t displayIndex = Index + 1;
  Index = (Index + 1) % Queue.size();

  CreatureId maxId = 0;
  World.ForEachCreature([&](const UCreature &creature)
                        { maxId = std::max(maxId, creature.GetId()); });

  if (World.SpawnCreatureByView(species))
  {
    World.ForEachCreature(
        [&](const UCreature &creature)
        {
          if (creature.GetId() > maxId)
          {
            LastSpawnedId = creature.GetId();
          }
        });
    result.Ok = true;
    std::ostringstream oss;
    oss << "[" << displayIndex << "/" << Queue.size() << "] " << species;
    result.Message = oss.str();
    return result;
  }

  std::ostringstream oss;
  oss << "[" << displayIndex << "/" << Queue.size() << "] " << species
      << " skip: " << World.GetCreatureSpawnBlockedHint(species);
  result.Message = oss.str();
  return result;
}

CreatureVisualQaSpawnResult UCreatureVisualQaSpawner::SpawnAllInGrid()
{
  CreatureVisualQaSpawnResult result;
  RefreshQueue();
  if (Queue.empty())
  {
    result.Message = "QA batch: no spawnable species";
    return result;
  }

  int spawned = 0;
  int skipped = 0;
  for (size_t i = 0; i < Queue.size(); ++i)
  {
    const std::string &species = Queue[i];
    const glm::vec3 origin = GridOriginForIndex(i, Queue.size());
    if (World.SpawnCreature(species, origin) != 0)
    {
      ++spawned;
    }
    else
    {
      ++skipped;
    }
  }

  LastSpawnedId = 0;
  Index = 0;
  result.Ok = spawned > 0;
  std::ostringstream oss;
  oss << "batch: " << spawned << "/" << Queue.size() << " ok";
  if (skipped > 0)
  {
    oss << ", " << skipped << " skip";
  }
  result.Message = oss.str();
  return result;
}

} // namespace cutum
