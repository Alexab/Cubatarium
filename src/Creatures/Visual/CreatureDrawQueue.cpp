#include "Creatures/Visual/CreatureDrawQueue.h"

#include "Render/Engine/GeometryEngine.h"
#include <algorithm>

namespace cutum
{

void CreatureDrawQueue::Flush(UGeometryEngine &engine)
{
  SortCreatureDrawRequests(Requests);

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
      if (req.SkinnedPrimitive != nullptr && req.BoneMatrices != nullptr)
      {
        engine.DrawCreatureSkinnedMesh(req.Mvp, req.Texture,
                                       *req.SkinnedPrimitive,
                                       req.BoneMatrices);
      }
      break;
    case CreatureDrawKind::WireframeBox:
      engine.DrawBoxWireframe(req.Mvp, req.WireColor);
      break;
    case CreatureDrawKind::Billboard:
      engine.DrawCreatureBillboard(req.Mvp, req.Texture, req.Tint);
      break;
    }
  }
}

} // namespace cutum
