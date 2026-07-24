#ifndef WORLDCOLLISION_H
#define WORLDCOLLISION_H

#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/CollisionVolume.h"
#include "World/Raycast/BlockRaycast.h"
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace cutum
{

struct PhysicsTelemetry;

class UBlockRegistry;
class UBlockWorld;
class UWorldEnvironment;

struct BlockPlacementResolve
{
  std::optional<BlockRayHit> break_hit;
  std::optional<glm::ivec3> place_block_pos;
};

struct SampledFluidState
{
  bool inFluid{false};
  BlockId dominantFluid{BLOCK_AIR};
  float blendWeight{0.0f};
  float DragHorizontal{0.0f};
  float SinkSpeed{0.0f};
  float RiseSpeed{0.0f};
};

class UWorldCollision
{
public:
  explicit UWorldCollision(UBlockWorld &blockWorld,
                           UWorldEnvironment *environment = nullptr);

  void SetBlockRegistry(UBlockRegistry *registry) { BlockRegistry = registry; }
  void SetEntityCollisionEnabled(bool enabled)
  {
    EntityCollisionEnabled = enabled;
  }
  bool IsEntityCollisionEnabled() const { return EntityCollisionEnabled; }
  void SetBroadphaseEnabled(bool enabled) { BroadphaseEnabled = enabled; }
  bool IsBroadphaseEnabled() const { return BroadphaseEnabled; }
  void SetCollisionDdaEnabled(bool enabled) { CollisionDdaEnabled = enabled; }
  bool IsCollisionDdaEnabled() const { return CollisionDdaEnabled; }
  void SetTelemetry(PhysicsTelemetry *telemetry) { Telemetry = telemetry; }

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
  BlockPlacementResolve ResolveBlockPlacement(const glm::vec3 &eye,
                                              const glm::vec3 &front,
                                              const PlayerCapsule &cap,
                                              float max_distance = 8.0f) const;
  /// Ray from `ray_origin`; self/pit checks use `player_eye` (iso elevated cam).
  BlockPlacementResolve ResolveBlockPlacement(const glm::vec3 &ray_origin,
                                              const glm::vec3 &front,
                                              const PlayerCapsule &cap,
                                              float max_distance,
                                              const glm::vec3 &player_eye) const;
  std::optional<glm::vec3>
  FindNearestFreeCubePosition(const glm::vec3 &position, const glm::vec3 &front,
                              const PlayerCapsule &cap) const;

  bool CheckRayIntersection(
      const glm::vec3 &position, const glm::vec3 &front,
      std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
          &distance_map) const;
  bool CheckRayIntersection(const glm::vec3 &position, const glm::vec3 &front,
                            glm::vec3 &intersection, float &distance,
                            size_t &cube_index, int &cube_side,
                            size_t &object_index) const;

  SampledFluidState SampleFluidPhysicsVolume(const CollisionVolume &vol) const;
  FluidColumnSurface FindFluidColumnSurfaceAt(int bx, int bz, int hintY) const;
  FluidColumnSurface FindFluidColumnSurfaceEye(const glm::vec3 &eye) const;

  void InvalidateChunkMovementSolid(glm::ivec3 chunk_coord);
  void RebuildChunkMovementSolid(glm::ivec3 chunk_coord);
  void RemoveChunkMovementSolidCache(glm::ivec3 chunk_coord);

private:
  bool IsPlaceableForSolidBlock(glm::ivec3 pos) const;
  bool CanPlaceClassic(glm::ivec3 place_pos, const glm::vec3 &eye,
                       const glm::vec3 &front, const PlayerCapsule &cap,
                       float max_distance) const;
  bool QueryChunkMovementSolid(glm::ivec3 chunk_coord) const;
  uint64_t QueryChunkOccupancyMask(glm::ivec3 chunk_coord) const;
  bool MayContainSolid(const CollisionVolume &vol) const;
  UBlockWorld &BlockWorld;
  UWorldEnvironment *Environment{nullptr};
  UBlockRegistry *BlockRegistry{nullptr};
  PhysicsTelemetry *Telemetry{nullptr};
  bool EntityCollisionEnabled{true};
  bool BroadphaseEnabled{false};
  bool CollisionDdaEnabled{false};
  static constexpr int SubchunkSize = 4;
  static constexpr int SubchunksPerAxis = CHUNK_SIZE / SubchunkSize;
  static constexpr int SubchunkCount =
      SubchunksPerAxis * SubchunksPerAxis * SubchunksPerAxis;
  mutable std::unordered_map<glm::ivec3, uint64_t, IVec3Hash>
      ChunkOccupancyMask;
};

} // namespace cutum

#endif // WORLDCOLLISION_H
