#include "Activity/Agents/WanderActivityAgent.h"
#include "Creatures/Movement/CreatureBodyProbe.h"
#include "Creatures/Movement/CreatureMovementLog.h"
#include <cmath>
#include <cstdlib>
#include <vector>

namespace cutum
{

namespace
{

constexpr int kMaxDirectionAttempts = 16;
constexpr float kStuckMoveThreshold = 0.35f;
constexpr float kStuckRepickSeconds = 1.25f;

glm::vec3 RandomWanderDirection(CreatureHabitat habitat)
{
  const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
  glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
  if (habitat == CreatureHabitat::Aquatic ||
      habitat == CreatureHabitat::Amphibious)
  {
    const float pitch =
        static_cast<float>((std::rand() % 201) - 100) / 100.0f * 0.45f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Lava)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.25f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Aerial)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.35f;
    dir.y = pitch;
  }
  if (glm::length(dir) > 1e-4f)
  {
    dir = glm::normalize(dir);
  }
  return dir;
}

bool TryWanderDirection(IWorldPerception &perception,
                        const CreatureActivityView &view,
                        CreatureHabitat habitat, const glm::vec3 &boundsSize,
                        const glm::vec3 &dir, glm::vec3 &outDirection,
                        WanderPickFailure *lastFailure)
{
  if (glm::length(glm::vec2(dir.x, dir.z)) <= 1e-4f &&
      habitat != CreatureHabitat::Aquatic && habitat != CreatureHabitat::Lava &&
      habitat != CreatureHabitat::Aerial)
  {
    return false;
  }
  glm::vec3 flatDir = dir;
  if (habitat == CreatureHabitat::Terrestrial ||
      habitat == CreatureHabitat::Amphibious)
  {
    flatDir = glm::normalize(glm::vec3(dir.x, 0.0f, dir.z));
  }
  else if (glm::length(flatDir) > 1e-4f)
  {
    flatDir = glm::normalize(flatDir);
  }
  const float probeDistance = WanderProbeDistance(boundsSize);
  const BodyMoveResult probe = perception.ProbeBodyMove(
      view.Id, view.bodyOrigin, flatDir * probeDistance, habitat,
      boundsSize, HabitatContext::WanderTarget);
  if (!probe.habitatOk)
  {
    if (lastFailure)
    {
      *lastFailure = WanderPickFailure::Habitat;
    }
    return false;
  }
  if (probe.blockedGeometry)
  {
    if (lastFailure)
    {
      *lastFailure = WanderPickFailure::Collision;
    }
    return false;
  }
  outDirection = flatDir;
  return true;
}

void ShuffleIndices(int *order, int count)
{
  for (int i = count - 1; i > 0; --i)
  {
    const int j = std::rand() % (i + 1);
    const int tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
}

bool PickWanderDirection(IWorldPerception &perception,
                         const CreatureActivityView &view,
                         CreatureHabitat habitat, const glm::vec3 &boundsSize,
                         const glm::vec3 &avoidDir, glm::vec3 &outDirection,
                         WanderPickFailure *outFailure)
{
  WanderPickFailure lastFailure = WanderPickFailure::Collision;
  if (habitat == CreatureHabitat::Aerial)
  {
    outDirection = RandomWanderDirection(CreatureHabitat::Aerial);
    if (outFailure)
    {
      *outFailure = WanderPickFailure::None;
    }
    return glm::length(outDirection) > 1e-4f;
  }

  static const glm::vec3 kCardinals[] = {
      glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, 0.0f, -1.0f),
      glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)),
      glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f)),
      glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f)),
      glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f))};
  constexpr int kCardinalCount = 8;

  std::vector<glm::vec3> validDirs;
  validDirs.reserve(static_cast<size_t>(kCardinalCount + kMaxDirectionAttempts));

  int cardinalOrder[kCardinalCount] = {0, 1, 2, 3, 4, 5, 6, 7};
  ShuffleIndices(cardinalOrder, kCardinalCount);
  for (int i = 0; i < kCardinalCount; ++i)
  {
    const glm::vec3 &dir = kCardinals[cardinalOrder[i]];
    glm::vec3 candidate(0.0f);
    if (TryWanderDirection(perception, view, habitat, boundsSize, dir,
                           candidate, &lastFailure))
    {
      validDirs.push_back(candidate);
    }
  }

  for (int attempt = 0; attempt < kMaxDirectionAttempts; ++attempt)
  {
    const glm::vec3 dir = RandomWanderDirection(habitat);
    glm::vec3 candidate(0.0f);
    if (TryWanderDirection(perception, view, habitat, boundsSize, dir,
                           candidate, &lastFailure))
    {
      validDirs.push_back(candidate);
    }
  }

  if (validDirs.empty())
  {
    if (outFailure)
    {
      *outFailure = lastFailure;
    }
    return false;
  }

  const bool avoid = glm::length(glm::vec2(avoidDir.x, avoidDir.z)) > 1e-4f;
  std::vector<glm::vec3> preferred;
  preferred.reserve(validDirs.size());
  if (avoid)
  {
    for (const glm::vec3 &dir : validDirs)
    {
      if (glm::dot(glm::vec2(dir.x, dir.z), glm::vec2(avoidDir.x, avoidDir.z)) >
          -0.85f)
      {
        preferred.push_back(dir);
      }
    }
  }
  const std::vector<glm::vec3> &pool =
      !preferred.empty() ? preferred : validDirs;
  outDirection = pool[static_cast<size_t>(std::rand()) % pool.size()];
  if (outFailure)
  {
    *outFailure = WanderPickFailure::None;
  }
  return true;
}

} // namespace

void UWanderActivityAgent::OnCreatureAdded(CreatureId Id)
{
  Members.insert(Id);
  WanderAgentState &st = State[Id];
  st.direction = RandomWanderDirection(CreatureHabitat::Terrestrial);
  st.anchorOrigin = glm::vec3(0.0f);
  st.stuckSeconds = 0.0f;
  ResetWanderState(Id, 2.0f, 4.0f);
  st.timer = 0.0f;
}

void UWanderActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
}

void UWanderActivityAgent::ResetWanderState(CreatureId Id, float intervalMin,
                                            float intervalMax)
{
  const float span = intervalMax - intervalMin;
  WanderAgentState &st = State[Id];
  st.timer =
      intervalMin + static_cast<float>(std::rand() % 1001) / 1000.0f * span;
}

void UWanderActivityAgent::Tick(IWorldPerception &perception,
                                ICreatureActivitySink &sink, float dt)
{
  for (const CreatureId Id : Members)
  {
    const std::optional<CreatureActivityView> view = sink.GetCreatureView(Id);
    if (!view || view->possessed || view->isPlayerCharacter)
    {
      continue;
    }
    const std::optional<CreatureBehaviorSnapshot> snapshot =
        sink.GetBehaviorSnapshot(Id);
    if (!snapshot)
    {
      continue;
    }

    WanderAgentState &st = State[Id];
    const float intervalMin = snapshot->behavior.wanderIntervalMin;
    const float intervalMax = snapshot->behavior.wanderIntervalMax;

    if (!perception.HabitatAllows(HabitatContext::WanderCurrent,
                                  snapshot->habitat, view->bodyOrigin,
                                  snapshot->boundsSize))
    {
      if (!st.habitatBlocked)
      {
        LogCreatureWanderNoDirection(view->Id, view->typeId, view->bodyOrigin,
                                     false, WanderPickFailure::HabitatIdle);
        st.habitatBlocked = true;
      }
      st.direction = glm::vec3(0.0f);
      CreatureIntent idleIntent;
      idleIntent.moveDirWorld = glm::vec3(0.0f);
      idleIntent.moveSpeed = snapshot->behavior.moveSpeed;
      idleIntent.clearOnApply = false;
      sink.SetIntent(Id, idleIntent);
      continue;
    }
    st.habitatBlocked = false;

    const float movedSinceAnchor =
        glm::length(view->bodyOrigin - st.anchorOrigin);
    if (movedSinceAnchor < kStuckMoveThreshold)
    {
      st.stuckSeconds += dt;
    }
    else
    {
      st.stuckSeconds = 0.0f;
    }

    st.timer -= dt;
    const bool stuckRepick = st.stuckSeconds >= kStuckRepickSeconds;
    if (st.timer <= 0.0f || stuckRepick)
    {
      if (stuckRepick)
      {
        st.stuckSeconds = 0.0f;
      }
      ResetWanderState(Id, intervalMin, intervalMax);
      st.anchorOrigin = view->bodyOrigin;
      const glm::vec3 avoidDir = stuckRepick ? st.direction : glm::vec3(0.0f);
      glm::vec3 nextDir(0.0f);
      WanderPickFailure pickFailure = WanderPickFailure::None;
      if (!PickWanderDirection(perception, *view, snapshot->habitat,
                               snapshot->boundsSize, avoidDir, nextDir,
                               &pickFailure))
      {
        if (pickFailure != WanderPickFailure::None &&
            glm::length(nextDir) <= 1e-4f)
        {
          LogCreatureWanderNoDirection(view->Id, view->typeId, view->bodyOrigin,
                                       true, pickFailure);
        }
        if (snapshot->habitat == CreatureHabitat::Aerial)
        {
          nextDir = RandomWanderDirection(CreatureHabitat::Aerial);
        }
        else
        {
          nextDir = RandomWanderDirection(snapshot->habitat);
        }
      }
      st.direction = nextDir;
    }

    CreatureIntent intent;
    intent.moveDirWorld = st.direction;
    float moveSpeed = snapshot->behavior.moveSpeed;
    if (snapshot->habitat == CreatureHabitat::Aerial)
    {
      if (snapshot->locomotion.flySpeed > 0.0f)
      {
        moveSpeed = snapshot->locomotion.flySpeed;
      }
      else if (snapshot->locomotion.walkSpeed > 0.0f)
      {
        moveSpeed = snapshot->locomotion.walkSpeed;
      }
    }
    else if (snapshot->locomotion.walkSpeed > 0.0f)
    {
      moveSpeed = snapshot->locomotion.walkSpeed;
    }
    intent.moveSpeed = moveSpeed;
    intent.clearOnApply = false;
    sink.SetIntent(Id, intent);
  }
}

} // namespace cutum
