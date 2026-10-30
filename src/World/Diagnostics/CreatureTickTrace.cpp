#include "World/Diagnostics/CreatureTickTrace.h"

#include "Creatures/Core/Creature.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace cutum
{

void RecordCreatureTickTrace(UWorld &world, bool tickWorldAi)
{
  // Diagnostics (env CUBA_QA_TICKDUMP_ID=<creatureId>): per-frame trace of
  // one creature for the first 400 frames (position, locomotion, intent).
  if (const char *qaTraceId = std::getenv("CUBA_QA_TICKDUMP_ID"))
  {
    static const CreatureId traceId = std::strtoull(qaTraceId, nullptr, 10);
    static std::ofstream traceFile(std::string(qaTraceId) + "_trace.txt",
                                   std::ios::trunc);
    static int traceFrame = 0;
    static const int traceFrames = [] {
      const char *frames = std::getenv("CUBA_QA_TICKDUMP_FRAMES");
      return frames ? std::max(1, std::atoi(frames)) : 400;
    }();
    ++traceFrame;
    if (traceFile && traceFrame <= traceFrames)
    {
      world.ForEachCreature([&](UCreature &creature)
                      {
                        if (creature.GetId() != traceId)
                        {
                          return;
                        }
                        const glm::vec3 p = creature.GetBodyOrigin();
                        const BlockId below = world.GetBlockWorld().GetBlock(
                            glm::ivec3(static_cast<int>(std::floor(p.x)),
                                       static_cast<int>(std::floor(p.y)) - 1,
                                       static_cast<int>(std::floor(p.z))));
                        const CollisionVolume bodyVol =
                            CollisionVolumeFromBody(p, creature.GetBounds().currentSizeBlocks);
                        const bool blockCol = world.CheckBlockCollisionVolume(bodyVol);
                        const bool creatureCol =
                            world.CheckCreatureCollisionVolume(bodyVol, traceId);
                        const glm::ivec3 topChunk =
                            UChunkManager::WorldToChunk(glm::ivec3(
                                static_cast<int>(std::floor(p.x)),
                                static_cast<int>(std::floor(p.y + bodyVol.halfExtents.y)),
                                static_cast<int>(std::floor(p.z))));
                        const glm::vec3 stepBody = p - glm::vec3(0.0f, 0.25f, 0.0f);
                        const CollisionVolume stepVol =
                            CollisionVolumeFromBody(stepBody, creature.GetBounds().currentSizeBlocks);
                        const bool stepBlockCol = world.CheckBlockCollisionVolume(stepVol);
                        const bool stepCreatureCol =
                            world.CheckCreatureCollisionVolume(stepVol, traceId);
                        const glm::ivec3 chunkCoord =
                            UChunkManager::WorldToChunk(
                                glm::ivec3(static_cast<int>(std::floor(p.x)),
                                           static_cast<int>(std::floor(p.y)),
                                           static_cast<int>(std::floor(p.z))));
                        traceFile << traceFrame << " x=" << p.x << " z=" << p.z
                                  << " y=" << p.y
                                  << " support="
                                  << world.HasGroundSupportVolume(bodyVol, p.y)
                                  << " bodySupport="
                                  << world.HasBodySupport(bodyVol)
                                  << " feetY=" << creature.GetLocomotion().GetFeetY()
                                  << " anchored=" << creature.GetLocomotion().IsFeetAnchored()
                                  << " vy=" << creature.GetLocomotion().GetVerticalVelocity()
                                  << " onGround=" << creature.GetLocomotion().IsOnGround()
                                  << " mode=" << static_cast<int>(creature.GetMovementMode())
                                  << " factsState="
                                  << static_cast<int>(creature.GetLocomotionFacts().state)
                                  << " factsSpeed="
                                  << creature.GetLocomotionFacts().horizontalSpeed
                                  << " factsPhase="
                                  << creature.GetLocomotionFacts().animPhase
                                  << " factsGround="
                                  << creature.GetLocomotionFacts().onGround
                                  << " suggestedAnim="
                                  << static_cast<int>(creature.GetIntent().suggestedAnim)
                                  << " speed=" << creature.GetIntent().moveSpeed
                                  << " dirLen=" << glm::length(creature.GetIntent().moveDirWorld)
                                  << " dir=" << creature.GetIntent().moveDirWorld.x
                                  << "," << creature.GetIntent().moveDirWorld.z
                                  << " blockBelow=" << static_cast<int>(below)
                                  << " chunk=(" << chunkCoord.x << ","
                                  << chunkCoord.y << "," << chunkCoord.z << ")"
                                  << " chunkLoaded="
                                  << world.GetBlockWorld().GetChunkManager().HasChunk(chunkCoord)
                                  << " blockCol=" << blockCol
                                  << " creatureCol=" << creatureCol
                                  << " topChunk=(" << topChunk.x << ","
                                  << topChunk.y << "," << topChunk.z << ")"
                                  << " topLoaded="
                                  << world.GetBlockWorld().GetChunkManager().HasChunk(topChunk)
                                  << " stepBlockCol=" << stepBlockCol
                                  << " stepCreatureCol=" << stepCreatureCol
                                  << " size=" << creature.GetBounds().currentSizeBlocks.x
                                  << "," << creature.GetBounds().currentSizeBlocks.y
                                  << "," << creature.GetBounds().currentSizeBlocks.z
                                  << std::endl;
                      });
    }
  }
  // Snapshot of every creature at frames 60 and 300.
  if (const char *qaDump = std::getenv("CUBA_QA_TICKDUMP"))
  {
    static int dumpFrames[] = {60, 300};
    static size_t dumpIdx = 0;
    static int lastDumped = -1;
    static int frameNo = 0;
    ++frameNo;
    if (dumpIdx < 2 && frameNo == dumpFrames[dumpIdx])
    {
      std::ofstream f(qaDump, std::ios::app);
      f << "== frame " << frameNo << " tick_world_ai=" << tickWorldAi
        << std::endl;
      world.ForEachCreature([&](UCreature &creature)
                      {
                        const glm::vec3 p = creature.GetBodyOrigin();
                        f << creature.GetId() << " " << creature.GetTypeId()
                          << " y=" << p.y << " mode="
                          << static_cast<int>(creature.GetMovementMode())
                          << " onGround=" << creature.GetLocomotion().IsOnGround()
                          << " possessed=" << creature.IsPossessed()
                          << " intentSpeed=" << creature.GetIntent().moveSpeed
                          << std::endl;
                      });
      ++dumpIdx;
    }
  }
}

} // namespace cutum
