#include "World/Core/World.h"
#include "World/Core/WorldOwnedCallbacks.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "World/Lighting/FlatLightingPipeline.h"
#include "World/Lighting/FullLightingPipeline.h"
#include "World/Lighting/GpuFullLightingPipeline.h"
#include <typeinfo>

namespace cutum {
WorldOwnedStorageLayout GetWorldOwnedStorageLayout() noexcept {
  WorldOwnedStorageLayout out;out.generation=UComposableWorldGenerator::GetRetainedStorageLayout();
  out.lighting={sizeof(UFlatLightingPipeline),sizeof(UFullLightingPipeline),sizeof(UGpuFullLightingPipeline),UGpuFullLightingPipeline::GetInnerInlineBytesForStorage()};
  out.world_inline=sizeof(UWorld);out.block_world_inline=sizeof(UBlockWorld);out.function_inline=sizeof(std::function<void()>);
  out.supported=out.generation.supported;return out;
}
struct WorldOwnedStorageInspector {
  static WorldOwnedStorageSnapshot Inspect(const UWorld &world,const UCore *core,const UApplication *application,bool known) noexcept {
    WorldOwnedStorageSnapshot out;out.layout=GetWorldOwnedStorageLayout();out.world=reinterpret_cast<uintptr_t>(&world);
    out.supported=known && out.layout.supported;out.generation.layout=out.layout.generation;out.generation.supported=out.layout.supported;
    out.borrowed_context_matches=true;out.callbacks[0].supported=known;
    if(world.WorldGen){
      const auto *pipeline=dynamic_cast<const UComposableWorldGenerator*>(world.WorldGen.get());
      if(!pipeline){out.supported=false;out.generation.supported=false;}
      else {
        out.generation=pipeline->InspectRetainedStorageForWorld(known,&world);
        const auto &ctx=pipeline->GetContext();
        out.borrowed_context_matches=&ctx.World==&world.BlockWorld && &ctx.Registry==world.BlockRegistry.get() &&
          ctx.Objects==world.ObjectLibrary && (!ctx.ObjectFeatures || ctx.ObjectFeatures==&world.ResolvedObjectFeatures);
        out.callbacks[0]=InspectKnownOwnerCallback<WorldColumnMeshDirtyCallback>(ctx.OnColumnMeshDirty,&world,known);
      }
    }
    if(const auto *light=world.LightingPipeline.get()){
      out.lighting=reinterpret_cast<uintptr_t>(light);
      if(typeid(*light)==typeid(UFlatLightingPipeline)){out.lighting_kind=1;out.lighting_requests=out.layout.lighting[0];}
      else if(typeid(*light)==typeid(UFullLightingPipeline)){out.lighting_kind=2;out.lighting_requests=out.layout.lighting[1];}
      else if(typeid(*light)==typeid(UGpuFullLightingPipeline)){out.lighting_kind=3;out.lighting_requests=out.layout.lighting[2];}
      else out.supported=false;
    }
    const auto &blocks=world.BlockWorld;const auto &manager=blocks.GetChunkManager();
    out.callbacks[1]=InspectEmptyCallback(blocks.KnownOpenSkyAbsentQuery,known);
    out.callbacks[2]=InspectKnownOwnerCallback<WorldKnownAirFloorCallback>(blocks.KnownAirColumnFloorQuery,&world,known);
    out.callbacks[3]=InspectKnownOwnerCallback<WorldKnownAirVersionCallback>(blocks.KnownAirEvidenceVersionQuery,&world,known);
    out.callbacks[4]=InspectKnownOwnerCallback<WorldKnownAirSliceCallback>(blocks.KnownAirInternalSliceQuery,&world,known);
    out.callbacks[5]=InspectKnownOwnerCallback<WorldPopulationInvalidationCallback>(manager.OnChunkBecameNonAir,&world,known);
    out.callbacks[6]=InspectEmptyCallback(manager.BeforeChunkAllocation,known);
    out.callbacks[7]=InspectKnownOwnerCallback<CoreAfterWorldLoadedCallback>(world.OnAfterWorldDataLoaded,core,known);
    out.callbacks[8]=InspectKnownOwnerCallback<ApplicationBlockCatalogCallback>(world.OnBlockRegistryChangedCallback,application,known);
    out.callbacks[9]=InspectKnownOwnerCallback<ApplicationCreatureCatalogCallback>(world.OnCreatureCatalogChangedCallback,application,known);
    for(const auto &callback:out.callbacks)out.supported=out.supported && callback.supported;
    PipelineStorageEnvelope sum;sum.supported=out.supported && out.borrowed_context_matches;AddPipelineStorage(sum,out.generation,false);
    PipelineStorageAdd(sum.additional_requests,out.lighting_requests,sum);
    for(size_t i=1;i<out.callbacks.size();++i)PipelineStorageAdd(sum.additional_requests,out.callbacks[i].requests,sum);
    out.overflow=sum.overflow;out.supported=sum.supported && !sum.overflow;return out;
  }
};
WorldOwnedStorageSnapshot UWorld::InspectOwnedGeneratorsAndCallbacks(const UCore *core,const UApplication *application,bool source_history_known)const noexcept {
  return WorldOwnedStorageInspector::Inspect(*this,core,application,source_history_known);
}
} // namespace cutum
