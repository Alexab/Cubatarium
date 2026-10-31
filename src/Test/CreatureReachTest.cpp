#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureFacing.h"
#include "Creatures/Core/CreatureFeetSnap.h"
#include "Creatures/Core/CreatureReach.h"
#include "Creatures/Environment/CreatureSpawnPlacement.h"
#include "Creatures/Player/PlayerCapsule.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_reach_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Near(float a, float b, float eps = 1e-4f)
{
  return std::abs(a - b) < eps;
}

int main()
{
  using namespace cutum;

  const glm::vec3 human(0.6f, 1.8f, 0.6f);
  const glm::vec3 ogre(2.58f, 3.14f, 2.86f);

  // Footprint gap: touching / overlapping bodies have no gap.
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human, glm::vec3(0.6f, 0.f, 0.f),
                                  human),
              0.0f),
         "touching bodies have zero gap");
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human, glm::vec3(2.0f, 0.f, 0.f),
                                  human),
              1.4f),
         "gap along X is center distance minus half widths");
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human,
                                  glm::vec3(1.6f, 0.f, 1.6f), human),
              std::sqrt(2.0f)),
         "diagonal gap combines both axes");

  // Melee distance keeps the old center distance for standard bodies.
  Expect(Near(MeleeDistanceXZ(glm::vec3(0.f), human, glm::vec3(1.2f, 0.f, 0.f),
                              human),
              1.2f),
         "standard pair: melee distance equals center distance");

  // An ogre pressed against the player (closest approach along X) used to be
  // 1.59 center-to-center, beyond its 1.4 attack range: it could never hit.
  const glm::vec3 ogreOrigin(0.f);
  const glm::vec3 playerTouching(0.5f * (ogre.x + human.x), 0.f, 0.f);
  Expect(MeleeDistanceXZ(ogreOrigin, ogre, playerTouching, human) <= 1.4f,
         "ogre touching the player is within its 1.4 attack range");
  Expect(MeleeDistanceXZ(ogreOrigin, ogre,
                         playerTouching + glm::vec3(3.0f, 0.f, 0.f), human) >
             1.4f,
         "ogre three blocks away is out of range");
  // Never longer than the center distance (narrow bodies keep their reach).
  const glm::vec3 thin(0.38f, 1.7f, 0.38f);
  Expect(MeleeDistanceXZ(glm::vec3(0.f), thin, glm::vec3(1.0f, 0.f, 0.f), thin) <=
             1.0f + 1e-4f,
         "narrow pair never exceeds center distance");

  // Ray picking: entry distance, start inside reports zero, miss and range.
  float t = -1.0f;
  Expect(RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                           glm::vec3(2.f, -1.f, -1.f), glm::vec3(3.f, 1.f, 1.f),
                           8.0f, t) &&
             Near(t, 2.0f),
         "ray enters box at x = 2");
  Expect(RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                           glm::vec3(-1.f), glm::vec3(1.f), 8.0f, t) &&
             Near(t, 0.0f),
         "ray starting inside reports t = 0 (why the viewer must be skipped)");
  Expect(!RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                            glm::vec3(2.f, 2.f, -1.f), glm::vec3(3.f, 3.f, 1.f),
                            8.0f, t),
         "ray misses a box beside it");
  Expect(!RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                            glm::vec3(2.f, -1.f, -1.f), glm::vec3(3.f, 1.f, 1.f),
                            1.5f, t),
         "box beyond the reach (or behind the first solid block) is not hit");

  // Footprint turns with the body in 90-degree steps, with hysteresis.
  Expect(!WantsSwappedFootprint(0.0f, false), "facing +Z keeps the box");
  Expect(!WantsSwappedFootprint(180.0f, false), "facing -Z keeps the box");
  Expect(WantsSwappedFootprint(90.0f, false), "facing +X swaps the box");
  Expect(WantsSwappedFootprint(-90.0f, false), "facing -X swaps the box");
  Expect(!WantsSwappedFootprint(50.0f, false),
         "50 degrees: not swapped yet (hysteresis)");
  Expect(WantsSwappedFootprint(50.0f, true),
         "50 degrees: stays swapped (hysteresis)");
  Expect(!WantsSwappedFootprint(30.0f, true), "30 degrees: swaps back");
  Expect(WantsSwappedFootprint(-120.0f, false) &&
             !WantsSwappedFootprint(-160.0f, true),
         "negative yaws fold the same way");
  const glm::vec3 whale(5.0f, 3.31f, 11.0f);
  const glm::vec3 turned = SwapFootprintXZ(whale);
  Expect(Near(turned.x, 11.0f) && Near(turned.y, 3.31f) && Near(turned.z, 5.0f),
         "swap exchanges X and Z, keeps height");

  // Melee line of sight over a fake block grid (blocks centered on integers).
  std::vector<glm::ivec3> solid;
  const auto segmentClear = [&solid](const glm::vec3 &from, const glm::vec3 &to)
  {
    for (int i = 0; i <= 200; ++i)
    {
      const glm::vec3 p = from + (to - from) * (static_cast<float>(i) / 200.0f);
      const glm::ivec3 cell(static_cast<int>(std::floor(p.x + 0.5f)),
                            static_cast<int>(std::floor(p.y + 0.5f)),
                            static_cast<int>(std::floor(p.z + 0.5f)));
      if (std::find(solid.begin(), solid.end(), cell) != solid.end())
      {
        return false;
      }
    }
    return true;
  };
  // Attacker feet on y = 0.5 (ground block row 0), eye 1.6 above.
  const glm::vec3 eye(0.0f, 2.1f, 0.0f);
  const glm::vec3 feet(1.2f, 0.5f, 0.0f);
  Expect(HasMeleeLineOfSight(eye, feet, human, segmentClear),
         "open ground: target in sight");
  // Wall column at x = 1 between attacker and a target at x = 2.
  const glm::vec3 behindWall(2.0f, 0.5f, 0.0f);
  solid = {glm::ivec3(1, 1, 0), glm::ivec3(1, 2, 0), glm::ivec3(1, 3, 0)};
  Expect(!HasMeleeLineOfSight(eye, behindWall, human, segmentClear),
         "two-high wall hides the target behind it");
  // One-block-high wall: the head is still visible over it.
  solid = {glm::ivec3(1, 1, 0)};
  Expect(HasMeleeLineOfSight(eye, behindWall, human, segmentClear),
         "low wall: head above it is in sight");
  // Diagonal: a pillar at (1, 1) hides a target at (2, 2) entirely.
  const std::vector<glm::ivec3> pillar = {
      glm::ivec3(1, 1, 1), glm::ivec3(1, 2, 1), glm::ivec3(1, 3, 1),
      glm::ivec3(1, 4, 1)};
  solid = pillar;
  Expect(!HasMeleeLineOfSight(eye, glm::vec3(2.0f, 0.5f, 2.0f), human,
                              segmentClear),
         "target behind a pillar on the diagonal is hidden");
  // Wide target: the same pillar hides its center, but its near edge is in
  // the open.
  Expect(HasMeleeLineOfSight(eye, glm::vec3(3.5f, 0.5f, 2.0f), ogre,
                             segmentClear),
         "wide target with its edge in the open is in sight");
  Expect(!segmentClear(eye, glm::vec3(3.5f, 0.5f + 0.5f * ogre.y, 2.0f)),
         "(the wide target's center line is blocked)");

  // Spawn placement: ahead distance clears the viewer for any body size.
  Expect(Near(SpawnAheadDistance(0.3f, human), 3.0f),
         "small body keeps the old 3-block spawn distance");
  const float ogreAhead = SpawnAheadDistance(0.3f, ogre);
  Expect(ogreAhead >= 0.3f + 0.5f * std::hypot(ogre.x, ogre.z),
         "ogre spawns clear of the viewer at any yaw");
  Expect(SpawnAheadDistance(0.3f, whale) > 6.0f, "whale spawns further out");

  // Grid alignment: odd cell counts center on a block, even on a boundary.
  Expect(Near(AlignSpawnAxis(33.3f, 0.9f), 33.0f), "1-cell body centers on a block");
  Expect(Near(AlignSpawnAxis(33.3f, 1.1f), 33.5f), "2-cell body centers on a boundary");
  Expect(Near(AlignSpawnAxis(-12.7f, 2.86f), -13.0f), "3-cell body centers on a block");
  Expect(Near(AlignSpawnAxis(10.0f, 2.0f), 10.5f), "exactly 2 wide is 2 cells");
  // An aligned cow stays inside its two columns (edges within 0.5 + 1).
  const float cowX = AlignSpawnAxis(33.0f, 1.1f);
  Expect(cowX - 0.55f >= 32.5f - 0.5f && cowX + 0.55f <= 34.5f,
         "aligned 1.1-wide body spans whole cells");

  // Candidates: never on the viewer, nearest first, step grows with size.
  const glm::vec3 viewer(32.0f, 64.5f, -12.0f);
  const glm::vec3 base(35.0f, 64.5f, -12.0f);
  const auto ogreCandidates =
      BuildSizedSpawnCandidates(base, ogre, viewer, human);
  Expect(!ogreCandidates.empty(), "ogre has candidates");
  for (const glm::vec3 &c : ogreCandidates)
  {
    Expect(HorizontalBodyGapXZ(c, ogre, viewer, human) > 0.2f,
           "no ogre candidate overlaps the viewer");
  }
  Expect(Near(ogreCandidates.front().x, 35.0f) &&
             Near(ogreCandidates.front().z, -12.0f) &&
             Near(ogreCandidates.front().y, base.y),
         "first candidate is the aligned base");
  const auto pigCandidates = BuildSizedSpawnCandidates(
      base, glm::vec3(0.9f, 0.9f, 0.9f), viewer, human);
  const auto whaleCandidates =
      BuildSizedSpawnCandidates(base, whale, viewer, human);
  float pigReach = 0.0f;
  float whaleReach = 0.0f;
  for (const glm::vec3 &c : pigCandidates)
  {
    pigReach = std::max(pigReach, std::abs(c.z - base.z));
  }
  for (const glm::vec3 &c : whaleCandidates)
  {
    whaleReach = std::max(whaleReach, std::abs(c.z - base.z));
  }
  Expect(Near(pigReach, 3.0f), "small body searches +-3 blocks");
  Expect(whaleReach >= 11.0f, "whale searches about its own length");

  // Facing: turn toward travel only when it agrees with the wish.
  const glm::vec2 east(1.0f, 0.0f);
  Expect(FacingDirectionFromMove(glm::vec2(0.1f, 0.0f), east).has_value(),
         "moving where it wants: face the move");
  Expect(FacingDirectionFromMove(glm::vec2(0.07f, 0.07f), east).has_value(),
         "sliding 45 degrees along a wall still faces the slide");
  Expect(!FacingDirectionFromMove(glm::vec2(-0.3f, 0.0f), east).has_value(),
         "pushed back against the wish: keep the yaw");
  Expect(!FacingDirectionFromMove(glm::vec2(0.0f, 0.3f), east).has_value(),
         "pushed sideways: keep the yaw");
  Expect(!FacingDirectionFromMove(glm::vec2(0.2f, 0.0f), glm::vec2(0.0f))
              .has_value(),
         "pushed while idle: keep the yaw");
  Expect(!FacingDirectionFromMove(glm::vec2(0.0f), east).has_value(),
         "no travel: keep the yaw (no spinning in place)");

  // Feet snap: never into blocks (edge still over a ledge), otherwise as
  // before (follow the ground when anchored, heal small gaps).
  Expect(!ShouldSnapFeetToGround(65.5f, 64.5f, 1.0f, true, false),
         "edge over the ledge: no drop into the ledge block");
  Expect(ShouldSnapFeetToGround(65.5f, 64.5f, 1.0f, true, true),
         "walked clear of the ledge: step down");
  Expect(ShouldSnapFeetToGround(65.5f, 66.0f, 1.0f, false, true),
         "small climb within the snap range");
  Expect(!ShouldSnapFeetToGround(65.5f, 62.5f, 1.0f, false, true),
         "unanchored body does not snap down a 3-block drop");
  Expect(!ShouldSnapFeetToGround(65.5f, 66.0f, 1.0f, true, false),
         "no snap up into a ceiling");

  // Motor capsule from creature bounds: both axes, slightly tighter.
  {
    const PlayerCapsule biped = PlayerCapsule::FromCreatureBlocks(human, 1.6f);
    Expect(Near(biped.halfX(), 0.276f) && Near(biped.halfZ(), 0.276f),
           "0.6 biped keeps its 0.276 slide-past-lips half width");
    const PlayerCapsule badger =
        PlayerCapsule::FromCreatureBlocks(glm::vec3(0.7f, 0.8f, 1.2f), 0.6f);
    Expect(Near(badger.halfZ(), 0.575f) && Near(badger.halfX(), 0.325f),
           "long body: the motor box is long too (was 0.322 square)");
    const PlayerCapsule whaleCap =
        PlayerCapsule::FromCreatureBlocks(whale, 2.0f);
    Expect(Near(whaleCap.halfZ(), 5.475f) && Near(whaleCap.halfExtents().z, 5.475f),
           "big body loses at most 0.025 per side");
    Expect(Near(PlayerCapsule::Standing().halfZ(), 0.3f),
           "player capsule stays square");
  }

  std::cout << "creature_reach_test: ok" << std::endl;
  return 0;
}
