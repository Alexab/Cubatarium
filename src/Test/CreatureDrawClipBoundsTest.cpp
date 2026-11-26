#include "Creatures/Visual/CreatureDrawQueue.h"
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

using namespace cutum;
namespace
{
void Expect(bool value, const char *why)
{
  if (!value)
  {
    std::cerr << why << '\n';
    std::exit(1);
  }
}
CreatureDrawRequest Box(const glm::mat4 &matrix)
{
  CreatureDrawRequest r;
  r.Kind = CreatureDrawKind::TexturedPart;
  r.Mvp = matrix;
  return r;
}
} // namespace
int main()
{
  const glm::mat4 identity(1.f);
  for (int axis = 0; axis < 3; ++axis)
    for (float side : {-1.f, 1.f})
    {
      glm::vec3 outside(0);
      outside[axis] = 2.f * side;
      CreatureDrawQueue queue;
      queue.Push(Box(glm::translate(identity, outside)));
      Expect(!queue.RangeIntersectsClip(0),
             "all six clip planes reject a fully exterior body");
      glm::vec3 limb(0);
      limb[axis] = .75f * side;
      queue.Push(Box(glm::translate(identity, limb)));
      Expect(queue.RangeIntersectsClip(0),
             "visible limb/equipment keeps an exterior actor alive");
      queue.DiscardSince(1);
      Expect(queue.Size() == 1 && !queue.RangeIntersectsClip(0),
             "tail rollback preserves previous actor requests");
    }
  CreatureDrawQueue queue;
  queue.Push(Box(identity));
  const size_t next = queue.Size();
  queue.Push(Box(glm::translate(identity, glm::vec3(5, 0, 0))));
  Expect(!queue.RangeIntersectsClip(next),
         "previous visible actors cannot hide rejection of the current range");
  Expect(queue.RangeIntersectsClip(0), "combined geometry stays conservative");
  queue.DiscardSince(next);
  Expect(queue.Size() == 1, "discard removes only the culled actor");
  queue.DiscardSince(100);
  Expect(queue.Size() == 1,
         "invalid discard index cannot append phantom draws");
  CreatureDrawQueue separated;
  separated.Push(Box(glm::translate(identity, glm::vec3(-3, 0, 0))));
  separated.Push(Box(glm::translate(identity, glm::vec3(3, 0, 0))));
  Expect(!separated.RangeIntersectsClip(0),
         "a union spanning the camera does not keep two entirely exterior "
         "parts alive");
  const auto meshOwner = std::make_shared<BoneSkeletonCubeMeshCpu>();
  auto &mesh = *meshOwner;
  mesh.interleavedPosUv = {-4, 0, 0, 0, 0, 4, 1, 0, 0, 0};
  mesh.GpuHash = 17;
  CreatureDrawRequest part;
  part.Kind = CreatureDrawKind::SkeletalMesh;
  part.SkeletalMesh = meshOwner;
  part.Mvp = glm::translate(identity, glm::vec3(3, 0, 0));
  CreatureDrawClipBounds bounds;
  bounds.Add(part);
  Expect(bounds.Intersects(),
         "mesh vertices outside a unit box remain part of the envelope");
  glm::vec3 low, high;
  Expect(CreatureMeshLocalClipBounds(mesh, low, high) && low.x == -4.f,
         "cache computed actual static vertices");
  mesh.interleavedPosUv[0] = -8;
  mesh.GpuHash = 18;
  Expect(CreatureMeshLocalClipBounds(mesh, low, high) && low.x == -8.f,
         "GPU content change invalidates local bounds");
  mesh.GpuHash = 0;
  mesh.interleavedPosUv[0] = -9;
  CreatureMeshLocalClipBounds(mesh, low, high);
  mesh.interleavedPosUv[0] = -10;
  Expect(CreatureMeshLocalClipBounds(mesh, low, high) && low.x == -10.f,
         "unkeyed custom meshes never reuse a stale vertex bound");
  CreatureDrawClipBounds skin;
  part.Kind = CreatureDrawKind::SkinnedMesh;
  skin.Add(part);
  Expect(skin.Intersects(), "an unsupported skin never substitutes bind bounds "
                            "for an animated attachment");
  mesh.GpuHash = 19;
  mesh.interleavedPosUv[0] = std::numeric_limits<float>::quiet_NaN();
  part.Kind = CreatureDrawKind::SkeletalMesh;
  CreatureDrawClipBounds invalid;
  invalid.Add(part);
  Expect(invalid.Intersects(), "nonfinite data fails open");
  const auto vp = glm::perspective(glm::radians(60.f), 1.f, .1f, 100.f);
  CreatureDrawClipBounds behind;
  behind.Add(Box(vp * glm::translate(identity, glm::vec3(0, 0, 5))));
  Expect(!behind.Intersects(),
         "negative homogeneous W behind a perspective camera is rejected");
  CreatureDrawClipBounds near;
  near.Add(Box(vp * glm::translate(identity, glm::vec3(0, 0, -.2f))));
  Expect(near.Intersects(), "an actor crossing the near plane stays visible");
  // Camera-facing sprite has no depth: its transformed quad can remain
  // visible after its narrow physical Z box leaves a side plane.
  CreatureDrawRequest sprite;
  sprite.Kind = CreatureDrawKind::Billboard;
  sprite.Mvp = glm::translate(identity, glm::vec3(1.2f, 0, 0)) *
               glm::scale(identity, glm::vec3(1, 2, 1));
  CreatureDrawClipBounds billboard;
  billboard.Add(sprite);
  Expect(billboard.Intersects(), "actual sprite quad included");
  auto item = std::make_shared<ItemDrawRequest>();
  item->Low = glm::vec3(-4, 0, 0);
  item->High = glm::vec3(4, 1, 0);
  CreatureDrawRequest surface;
  surface.Kind = CreatureDrawKind::ItemSurface;
  surface.ItemSurface = item;
  surface.Mvp = glm::translate(identity, glm::vec3(3, 0, 0));
  CreatureDrawClipBounds itemBounds;
  itemBounds.Add(surface);
  Expect(itemBounds.Intersects(),
         "item requests use their true envelope rather than a unit cube");
  surface.Mvp = glm::translate(identity, glm::vec3(8, 0, 0));
  CreatureDrawClipBounds itemOutside;
  itemOutside.Add(surface);
  Expect(!itemOutside.Intersects(),
         "item geometry entirely outside the clip planes is rejected");
  item->Low = glm::vec3(-4, -.5f, -.5f);
  item->High = glm::vec3(4, .5f, .5f);
  item->Model = glm::translate(identity, glm::vec3(12, 0, 0));
  Expect(
      ItemIntersectsDrawDistance(*item, glm::vec3(0), 10),
      "long item crossing distance horizon survives even with exterior origin");
  item->Model = glm::translate(identity, glm::vec3(15, 0, 0));
  Expect(!ItemIntersectsDrawDistance(*item, glm::vec3(0), 10),
         "entire actual item envelope beyond horizon is rejected");
  item->Model = glm::translate(identity, glm::vec3(0, 15, 0));
  Expect(!ItemIntersectsDrawDistance(*item, glm::vec3(0), 10),
         "vertical separation uses same three-dimensional domain as creatures");
  item->Model = glm::translate(identity, glm::vec3(-112, -100, -100));
  Expect(ItemIntersectsDrawDistance(*item, glm::vec3(-100), 10),
         "negative world coordinates retain horizon-crossing item");
  item->Model = glm::translate(identity, glm::vec3(0, 12, 0)) *
                glm::rotate(identity, glm::radians(90.f), glm::vec3(0, 0, 1));
  Expect(ItemIntersectsDrawDistance(*item, glm::vec3(0), 10),
         "rotated long item uses transformed corners");
  item->Model = glm::translate(identity, glm::vec3(0, 15, 0)) *
                glm::scale(identity, glm::vec3(1, 12, 1));
  Expect(ItemIntersectsDrawDistance(*item, glm::vec3(0), 10),
         "nonuniform scale reaches into the visible domain");
  item->Model = glm::translate(identity, glm::vec3(338, 107, 10));
  Expect(!ItemIntersectsDrawDistance(*item, glm::vec3(0), 64),
         "native remote loose-item case cannot request out-of-domain light");
  CreatureDrawQueue distanceQueue;
  distanceQueue.Push(Box(identity));
  const auto firstItem = distanceQueue.Size();
  distanceQueue.Push(surface);
  Expect(
      !distanceQueue.ItemRangeIntersectsDistance(firstItem, glm::vec3(0), 64),
      "preexisting visible actor cannot retain a remote loose-item range");
  auto nearItem = std::make_shared<ItemDrawRequest>(*item);
  nearItem->Model = identity;
  auto nearSurface = surface;
  nearSurface.ItemSurface = nearItem;
  distanceQueue.Push(nearSurface);
  Expect(distanceQueue.ItemRangeIntersectsDistance(firstItem, glm::vec3(0), 64),
         "one visible carried part preserves the whole item submission");
  Expect(ItemIntersectsDrawDistance(*item, glm::vec3(0), 0),
         "explicit disabled distance bound keeps requests");
  item->Model[0][0] = std::numeric_limits<float>::quiet_NaN();
  Expect(ItemIntersectsDrawDistance(*item, glm::vec3(0), 64),
         "unknown nonfinite world envelope fails open");
  CreatureDrawQueue legacyDistance;
  legacyDistance.Push(Box(identity));
  Expect(legacyDistance.ItemRangeIntersectsDistance(0, glm::vec3(0), 64),
         "unsupported legacy geometry is not replaced by an invented bound");
  auto legacy = Box(identity);
  legacy.Kind = CreatureDrawKind::SkeletalMesh;
  legacy.WorldBounds = TransformDrawWorldBounds(
      glm::vec3(-.5f), glm::vec3(.5f),
      glm::translate(identity, glm::vec3(338, 107, 10)));
  CreatureDrawQueue legacyFar;
  legacyFar.Push(legacy);
  Expect(!legacyFar.ItemRangeIntersectsDistance(0, glm::vec3(0), 64),
         "legacy glTF and block drop with real world bounds obey distance");
  legacy.WorldBounds = TransformDrawWorldBounds(
      glm::vec3(-4, -.5f, -.5f), glm::vec3(4, .5f, .5f),
      glm::translate(identity, glm::vec3(12, 0, 0)));
  CreatureDrawQueue legacyWide;
  legacyWide.Push(legacy);
  Expect(legacyWide.ItemRangeIntersectsDistance(0, glm::vec3(0), 10),
         "wide legacy primitive cannot be culled by its origin");
  const auto farWorld = TransformDrawWorldBounds(
      glm::vec3(-4, -.5f, -.5f), glm::vec3(4, .5f, .5f),
      glm::translate(identity, glm::vec3(10000000, 0, 0)));
  Expect(farWorld && BoundsIntersectDrawDistance(*farWorld,
                                                 glm::vec3(9999986, 0, 0), 10),
         "outward world-bound rounding preserves a distant tangent item");
  auto projectiveModel = identity;
  projectiveModel[3][3] = 2;
  Expect(
      !TransformDrawWorldBounds(glm::vec3(-1), glm::vec3(1), projectiveModel),
      "non-affine model cannot invent a finite world envelope");
  std::cout << "creature_draw_clip_bounds_test: passed\n";
}
