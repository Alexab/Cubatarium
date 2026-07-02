#ifndef WORLDCOLLISION_H
#define WORLDCOLLISION_H

#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Math/CollisionVolume.h"
#include "World/Chunks/ChunkManager.h"
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UWorldEnvironment;

class UWorldCollision
{
public:
  UWorldCollision(UBlockWorld &blockWorld, UWorldEnvironment &environment);

  void SetBlockRegistry(UBlockRegistry *registry) { BlockRegistry = registry; }
  void SetEntityCollisionEnabled(bool enabled)
  {
    EntityCollisionEnabled = enabled;
  }
  bool IsEntityCollisionEnabled() const { return EntityCollisionEnabled; }
  void SetBroadphaseEnabled(bool enabled) { BroadphaseEnabled = enabled; }
  bool IsBroadphaseEnabled() const { return BroadphaseEnabled; }

  struct StepUpProbe
  {
    bool Valid{false};
    float DistanceToLedge{0.0f};
    glm::vec3 TargetPos{0.0f};
    glm::vec3 MoveDir{0.0f};
  };

  std::optional<float> QueryGroundFeetYUnder(int worldX, int worldZ,
                                             float referenceFeetY) const;
  std::optional<float> QueryGroundFeetYColumn(int worldX, int worldZ) const;
  std::optional<int> FindHighestSolidY(int x, int z) const;
  bool IsValidStandCell(const glm::ivec3 &cell, const PlayerCapsule &cap) const;
  bool IsValidStandFootprint(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                             float feetY) const;

  bool CheckBlockCollisionVolume(const CollisionVolume &vol) const;
  bool CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                    CreatureId skipCreatureId) const;
  bool CheckCollisionVolume(const CollisionVolume &vol,
                            CreatureId skipCreatureId = 0) const;
  bool HasGroundSupportVolume(const CollisionVolume &vol, float feetY) const;

  glm::vec3 ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                const glm::vec3 &delta,
                                const glm::vec3 &currentSizeBlocks,
                                CreatureId skipCreatureId = 0) const;

  bool CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                      CreatureId skipCreatureId) const;
  bool DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                      CreatureId skipCreatureId = 0) const;
  bool HasGroundSupport(const glm::vec3 &eyePos,
                        const PlayerCapsule &cap) const;
  glm::vec3 ResolveMovement(const glm::vec3 &eyePos, const glm::vec3 &delta,
                            const PlayerCapsule &cap,
                            CreatureId skipCreatureId = 0) const;

  StepUpProbe ProbeStepUp(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                          const PlayerCapsule &cap,
                          float maxTriggerDistance) const;
  bool GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                        const PlayerCapsule &cap, float maxTriggerDistance,
                        glm::vec3 &outLanding) const;
  bool TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                 const PlayerCapsule &cap, float maxTriggerDistance) const;

  bool CheckPositionFree(const glm::vec3 &position, float size = 1.0f) const;
  std::optional<glm::vec3>
  FindNearestFreeCubePosition(const glm::vec3 &position, const glm::vec3 &front,
                              const PlayerCapsule &cap) const;

  void InvalidateChunkMovementSolid(glm::ivec3 chunk_coord);
  void RebuildChunkMovementSolid(glm::ivec3 chunk_coord);
  void RemoveChunkMovementSolidCache(glm::ivec3 chunk_coord);

private:
  bool QueryChunkMovementSolid(glm::ivec3 chunk_coord) const;
  bool MayContainSolid(const CollisionVolume &vol) const;
  UBlockWorld &BlockWorld;
  UWorldEnvironment &Environment;
  UBlockRegistry *BlockRegistry{nullptr};
  bool EntityCollisionEnabled{true};
  bool BroadphaseEnabled{false};
  mutable std::unordered_map<glm::ivec3, bool, IVec3Hash> ChunkMovementSolid;
};

} // namespace cutum

#endif // WORLDCOLLISION_H
