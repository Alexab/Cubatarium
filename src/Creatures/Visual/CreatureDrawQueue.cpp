#include "Creatures/Visual/CreatureDrawQueue.h"

#include <algorithm>

namespace cutum
{

void CreatureDrawQueue::Flush(ICreatureDrawSink &sink)
{
  SortCreatureDrawRequests(Requests);

  for (const CreatureDrawRequest &req : Requests)
  {
    sink.SetCreatureLight(req.Light);
    switch (req.Kind)
    {
    case CreatureDrawKind::TexturedPart:
      sink.DrawTexturedPart(req.Mvp, req.Texture, req.PartMesh);
      break;
    case CreatureDrawKind::SkeletalMesh:
      if (req.SkeletalMesh != nullptr)
      {
        sink.DrawBoneSkeletonMesh(req.Mvp, req.Texture,
                                        *req.SkeletalMesh);
      }
      break;
    case CreatureDrawKind::SkinnedMesh:
      if (req.SkinnedPrimitive != nullptr && req.BoneMatrices != nullptr)
      {
        sink.DrawSkinnedMesh(req.Mvp, req.Texture,
                                       *req.SkinnedPrimitive,
                                       req.BoneMatrices);
      }
      break;
    case CreatureDrawKind::WireframeBox:
      sink.DrawBoxWireframe(req.Mvp, req.WireColor);
      break;
    case CreatureDrawKind::Billboard:
      sink.DrawBillboard(req.Mvp, req.Texture, req.Tint);
      break;
    }
  }
}

} // namespace cutum
