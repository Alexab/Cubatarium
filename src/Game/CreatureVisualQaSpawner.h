#ifndef CREATUREVISUALQASPAWNER_H
#define CREATUREVISUALQASPAWNER_H

#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include <string>
#include <vector>

namespace cutum
{

class UWorld;

struct CreatureVisualQaSpawnResult
{
  std::string Message;
  bool Ok{false};
};

/// Dev/creative hotkey helper: cycle or batch-spawn all catalog spawnable
/// species.
class UCreatureVisualQaSpawner
{
public:
  explicit UCreatureVisualQaSpawner(UWorld &world);

  CreatureVisualQaSpawnResult SpawnNextSpecies();
  CreatureVisualQaSpawnResult SpawnAllInGrid();

private:
  void RefreshQueue();
  glm::vec3 GridOriginForIndex(size_t index, size_t total) const;

  UWorld &World;
  std::vector<std::string> Queue;
  size_t Index{0};
  CreatureId LastSpawnedId{0};
};

} // namespace cutum

#endif // CREATUREVISUALQASPAWNER_H
