#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Persistence/WorldPersistence.h"
#include "Render/Engine/GeometryEngine.h"
#include "App/Application.h"
namespace cutum {
struct MemberCallbackStorageInspector {
  static MemberCallbackStorageSnapshot Inspect(UWorld &world,const UGeometryEngine *geometry,const UApplication *application,const AllocatorSourcePhase &phase,bool known) noexcept {
    MemberCallbackStorageSnapshot out;out.world=reinterpret_cast<uintptr_t>(&world);
    known=known && phase.IsOpen() && phase.epoch && phase.participants==1;
    if(world.Streaming){out.Merge(world.Streaming->InspectStoredCallbacks(world,known));
      if(world.MeshService)out.Merge(world.Streaming->GetEmergeCoordinator().InspectStoredCallbacks(*world.MeshService,world,known));}
    if(world.MeshService){out.Merge(world.MeshService->InspectStoredDependencyCallback(known));out.Merge(world.InspectStoredEnterCallback(known));
      if(geometry)out.Merge(geometry->InspectStoredLightCallbacks(world,phase,known));
      else {const auto &cache=world.MeshService->GetCache();SetEmptyMemberCallback(out,MemberCallbackCell::PrepareLight,&cache.PrepareRenderLight,known);
        SetEmptyMemberCallback(out,MemberCallbackCell::PackedSwitch,&cache.OnPackedRepresentationSwitch,known);}}
    if(world.Persistence)out.Merge(world.Persistence->InspectStoredLightObserver(known));
    if(application)out.Merge(application->InspectStoredActionCallbacks(known));
    else {const std::function<void()> *absent=nullptr;SetEmptyMemberCallback(out,MemberCallbackCell::PendingAction,absent,known);SetEmptyMemberCallback(out,MemberCallbackCell::OperationComplete,absent,known);}
    return out;
  }
};
MemberCallbackStorageSnapshot UWorld::InspectStoredMemberCallbacks(const UGeometryEngine *geometry,const UApplication *application,const AllocatorSourcePhase &phase,bool known) const noexcept {
  return MemberCallbackStorageInspector::Inspect(*const_cast<UWorld*>(this),geometry,application,phase,known);
}
} // namespace cutum
