#include "Creatures/Visual/CreatureDrawQueue.h"

#include "Render/Engine/GeometryEngine.h"
#include <algorithm>

namespace cutum
{

void CreatureDrawQueue::Clear() { Requests.clear(); }

void CreatureDrawQueue::Push(CreatureDrawRequest request)
{
  Requests.push_back(std::move(request));
}

void CreatureDrawQueue::Flush(UGeometryEngine &engine)
{
  std::stable_sort(
      Requests.begin(), Requests.end(),
      [](const CreatureDrawRequest &a, const CreatureDrawRequest &b)
      {
        if (a.Kind != b.Kind)
        {
          return static_cast<int>(a.Kind) < static_cast<int>(b.Kind);
        }
        return a.Texture < b.Texture;
      });

  for (const CreatureDrawRequest &req : Requests)
  {
    switch (req.Kind)
    {
    case CreatureDrawKind::TexturedPart:
      engine.DrawCreatureTexturedPart(req.Mvp, req.Texture, req.PartMesh);
      break;
    case CreatureDrawKind::SkeletalMesh:
      if (req.SkeletalMesh != nullptr)
      {
        engine.DrawCreatureBoneSkeletonMesh(req.Mvp, req.Texture,
                                        *req.SkeletalMesh);
      }
      break;
    case CreatureDrawKind::SkinnedMesh:
      if (req.SkinnedPrimitive != nullptr)
      {
        engine.DrawCreatureSkinnedMesh(req.Mvp, req.Texture,
                                       *req.SkinnedPrimitive, req.BoneMatrices);
      }
      break;
    case CreatureDrawKind::WireframeBox:
      engine.DrawBoxWireframe(req.Mvp, req.WireColor);
      break;
    }
  }
}

} // namespace cutum
