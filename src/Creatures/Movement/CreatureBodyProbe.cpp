#include "Creatures/Movement/CreatureBodyProbe.h"

#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Movement/CreatureBodyStepUp.h"
#include "Creatures/Movement/CreatureMovementLog.h"
#include "World/Core/World.h"

namespace cutum
{

BodyMoveResult ProbeMove(const UWorld &world, CreatureId id,
                         const glm::vec3 &origin, const glm::vec3 &delta,
                         CreatureHabitat habitat, const glm::vec3 &sizeBlocks,
                         HabitatContext targetContext)
{
  const bool blocksOnlyProbe = targetContext == HabitatContext::WanderTarget;
  glm::vec3 resolved = world.ResolveMovementBody(origin, delta, sizeBlocks, id,
                                                 blocksOnlyProbe);
  resolved = SnapBodyToColumnGround(world, resolved, sizeBlocks, id,
                                    1.05f);
  if (IsOnRaisedFooting(world, origin) ||
      (!IsInDepression(world, origin) &&
       !CreatureHasGroundSupport(
           SampleCreatureFootprint(world, resolved, sizeBlocks))))
  {
    glm::vec3 dropped = resolved;
    if (TryCreatureLedgeDrop(world, id, dropped, delta, sizeBlocks))
    {
      resolved = dropped;
    }
  }
  EnvironmentSample env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
  const EnvironmentSample originEnv =
      ProbeEnvironmentAt(world, origin, sizeBlocks);
  BodyMoveResult result = EvaluateResolvedMove(origin, resolved, habitat,
                                               targetContext, env, sizeBlocks);
  result.footprintAtTarget =
      SampleCreatureFootprint(world, resolved, sizeBlocks);

  const float intendedXZ = glm::length(glm::vec2(delta.x, delta.z));
  const bool partialHorizMove =
      intendedXZ > 1e-4f && result.movedXZ < intendedXZ * 0.85f;
  const CreatureStepUpProbe stepProbe =
      CreatureStepUpAllowed(world, id, habitat, originEnv.inFluid) &&
              intendedXZ > 1e-4f
          ? ProbeCreatureStepUp(world, id, origin, delta, sizeBlocks)
          : CreatureStepUpProbe{};
  const bool shouldStepUp =
      !IsOnRaisedFooting(world, origin) &&
      (targetContext == HabitatContext::MoveApply ||
       targetContext == HabitatContext::WanderTarget) &&
      result.habitatOk && stepProbe.valid &&
      stepProbe.landingBodyOrigin.y > origin.y + 0.45f &&
      (result.blockedGeometry || partialHorizMove ||
       stepProbe.landingBodyOrigin.y > resolved.y + 0.45f);

  if (shouldStepUp)
  {
    glm::vec3 stepped = origin;
    if (TryCreatureStepUp(world, id, stepped, delta, sizeBlocks))
    {
      resolved = stepped;
      env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
      result = EvaluateResolvedMove(origin, resolved, habitat, targetContext,
                                    env, sizeBlocks);
      result.footprintAtTarget =
          SampleCreatureFootprint(world, resolved, sizeBlocks);
    }
  }
  else if (result.blockedGeometry &&
           CreatureStepUpAllowed(world, id, habitat, originEnv.inFluid))
  {
    glm::vec3 stepped = resolved;
    if (TryCreatureStepUp(world, id, stepped, delta, sizeBlocks))
    {
      resolved = stepped;
      env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
      result = EvaluateResolvedMove(origin, resolved, habitat, targetContext,
                                    env, sizeBlocks);
      result.footprintAtTarget =
          SampleCreatureFootprint(world, resolved, sizeBlocks);
    }
    else if (IsInDepression(world, origin))
    {
      glm::vec3 escaped = origin;
      if (TryCreatureEscapeStepUp(world, id, escaped, sizeBlocks, habitat,
                                  originEnv.inFluid))
      {
        resolved = escaped;
        env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
        result = EvaluateResolvedMove(origin, resolved, habitat, targetContext,
                                      env, sizeBlocks);
        result.footprintAtTarget =
            SampleCreatureFootprint(world, resolved, sizeBlocks);
      }
    }
    else
    {
      const bool onRaised = IsOnRaisedFooting(world, origin);
      if (!onRaised)
      {
        glm::vec3 escaped = origin;
        if (TryCreatureEscapeStepUp(world, id, escaped, sizeBlocks, habitat,
                                    originEnv.inFluid))
        {
          resolved = escaped;
          env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
          result = EvaluateResolvedMove(origin, resolved, habitat, targetContext,
                                        env, sizeBlocks);
          result.footprintAtTarget =
              SampleCreatureFootprint(world, resolved, sizeBlocks);
        }
      }
      else
      {
        glm::vec3 lowered = resolved;
        if (!TryCreatureLedgeDrop(world, id, lowered, delta, sizeBlocks))
        {
          lowered = SnapBodyToColumnGround(world, resolved, sizeBlocks, id,
                                           1.05f);
        }
        if (lowered.y < origin.y - 0.02f)
        {
          resolved = lowered;
          env = ProbeEnvironmentAt(world, resolved, sizeBlocks);
          result = EvaluateResolvedMove(origin, resolved, habitat, targetContext,
                                        env, sizeBlocks);
          result.footprintAtTarget =
              SampleCreatureFootprint(world, resolved, sizeBlocks);
        }
      }
    }
  }

  result.resolvedOrigin = resolved;

  if (result.blockedGeometry && result.habitatOk)
  {
    const float descended = origin.y - resolved.y;
    if (descended >= 0.45f && result.movedXZ >= kMinMoveApplyXZ)
    {
      result.blockedGeometry = false;
      result.blocked = result.blockedHabitat;
    }
  }

  if (result.blockedGeometry || result.blockedHabitat)
  {
    const FootprintSample originFootprint =
        SampleCreatureFootprint(world, origin, sizeBlocks);
    LogCreatureMoveProbe(id, world, origin, delta, result, targetContext,
                         originEnv, originFootprint);
  }

  return result;
}

} // namespace cutum
