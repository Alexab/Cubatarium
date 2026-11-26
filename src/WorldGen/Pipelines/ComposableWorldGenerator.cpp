#include "World/Core/WorldOwnedCallbacks.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/RuntimeSettingsText.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Pipelines/ColumnGenerationService.h"
#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <algorithm>
#include <limits>
#include <typeinfo>

namespace cutum
{
int UComposableWorldGenerator::DirectCoarseCallback::operator()(int x,int z)const{return owner->ComputeCoarseSurfaceY(x,z);}
int UComposableWorldGenerator::SampleCoarseCallback::operator()(int x,int z)const{return owner->SampleCoarseSurfaceY(x,z);}
int UComposableWorldGenerator::CachedCoarseCallback::operator()(int x,int z)const{return cache->GetOrCompute(x,z,direct);}

PipelineStorageLayout UComposableWorldGenerator::GetRetainedStorageLayout() noexcept
{
  PipelineStorageLayout out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  const auto function=[](auto callback){using F=std::_Func_impl_no_alloc<decltype(callback),int,int,int>;return std::_Is_large<F>?sizeof(F):size_t{0};};
  const auto world=UBlockWorld::GetPipelineScratchStorageLayout();
  out.widths={sizeof(UComposableWorldGenerator),sizeof(UProceduralClimateBiomeSource),sizeof(ImageMapBiomeSource),
    function(DirectCoarseCallback{nullptr}),function(SampleCoarseCallback{nullptr}),function(CachedCoarseCallback{nullptr,{}}),
    sizeof(SpawnResolveCandidateEntry),sizeof(LakeCenterSurfaceYCache),world[0],world[1],world[2],world[3]};
  out.inline_children={sizeof(Ctx),sizeof(HeightSampler),sizeof(DensitySampler),sizeof(AquiferSampler),sizeof(BiomeSampler),
    sizeof(SampleBuilder),sizeof(ChunkCoarseCache),sizeof(LakeCenterSurfaceYCache),sizeof(DirectCoarseHeightFn)};
  out.stl_large_threshold=std::_Big_allocation_threshold;out.stl_large_slack=std::_Non_user_size;
  out.spawn_raw_upper=2500*3*sizeof(int)+PipelineStlAlignmentSlack(2500*3*sizeof(int));out.supported=true;
#endif
  return out;
}

PipelineStorageSnapshot UComposableWorldGenerator::InspectRetainedStorage(bool ordinary_heap_factory_known) const noexcept
{return InspectRetainedStorageForWorld(ordinary_heap_factory_known,nullptr);}

PipelineStorageSnapshot UComposableWorldGenerator::InspectRetainedStorageForWorld(bool ordinary_heap_factory_known,const UWorld *owner) const noexcept
{
  PipelineStorageSnapshot out;out.layout=GetRetainedStorageLayout();out.initialized=true;
  out.pipeline=reinterpret_cast<uintptr_t>(this);out.supported=ordinary_heap_factory_known && out.layout.supported && typeid(*this)==typeid(UComposableWorldGenerator);
  const auto set=[&](PipelineStorageCell cell,size_t value){out.requests[static_cast<size_t>(cell)]=value;};
  set(PipelineStorageCell::Object,out.layout.widths[0]);
  if(BiomeSource){out.biome_source=reinterpret_cast<uintptr_t>(BiomeSource.get());
    if(dynamic_cast<const UProceduralClimateBiomeSource*>(BiomeSource.get()))set(PipelineStorageCell::BiomeSource,out.layout.widths[1]);
    else if(dynamic_cast<const ImageMapBiomeSource*>(BiomeSource.get()))set(PipelineStorageCell::BiomeSource,out.layout.widths[2]);
    else out.supported=false;
  }
  const size_t text=AddTextCapacityBytes(RuntimeSettingsTextCapacityBytes(Ctx.Settings),ExternalStringCapacityBytes(Ctx.WorldgenOwnerPackId));
  set(PipelineStorageCell::ContextText,text);set(PipelineStorageCell::SampleText,GetRetainedSampleSettingsTextCapacityBytes());
  set(PipelineStorageCell::SpawnCandidates,SpawnResolveCandidates.capacity()*sizeof(SpawnResolveCandidateEntry));
  size_t callbacks=0;
  const auto direct=[&](const CoarseHeightCallback &fn){if(!fn)return true;const auto *p=fn.target<DirectCoarseCallback>();
    if(!p || p->owner!=this)return false;callbacks=AddTextCapacityBytes(callbacks,out.layout.widths[3]);return true;};
  if(!direct(DirectCoarseHeightFn))out.supported=false;
  if(Ctx.CoarseSurfaceYAt){const auto *p=Ctx.CoarseSurfaceYAt.target<SampleCoarseCallback>();
    if(!p || p->owner!=this)out.supported=false;else callbacks=AddTextCapacityBytes(callbacks,out.layout.widths[4]);}
  if(Ctx.OnColumnMeshDirty){
    const auto callback=InspectKnownOwnerCallback<WorldColumnMeshDirtyCallback>(Ctx.OnColumnMeshDirty,owner,owner!=nullptr);
    out.supported=out.supported && callback.supported;callbacks=AddTextCapacityBytes(callbacks,callback.requests);
  }
  if(BiomeSampler){const auto &fn=BiomeSampler->GetCoarseHeightCallbackForStorage();
    if(const auto *p=fn.target<CachedCoarseCallback>()){
      if(!ChunkCoarseCache || p->cache!=&*ChunkCoarseCache || !direct(p->direct))out.supported=false;
      callbacks=AddTextCapacityBytes(callbacks,out.layout.widths[5]);
    }else if(!direct(fn))out.supported=false;
  }
  set(PipelineStorageCell::Callbacks,callbacks);
  set(PipelineStorageCell::DensityCache,DensitySampler?DensitySampler->GetSamplingCacheStorageBytes():0);
  set(PipelineStorageCell::CoarseCache,GetCoarseMemoStorageBytes());
  size_t slack=PipelineStlAlignmentSlack(SpawnResolveCandidates.capacity()*sizeof(SpawnResolveCandidateEntry));
  for(const auto *text:{&Ctx.Settings.SeedText,&Ctx.Settings.WorldGenPackId,&Ctx.Settings.WorldGenPresetId,&Ctx.WorldgenOwnerPackId})
    slack+=PipelineStlAlignmentSlack(ExternalStringCapacityBytes(*text));
  if(SampleBuilder)slack+=SampleBuilder->GetRetainedSettingsAlignmentSlack();
  if(DensitySampler)slack+=PipelineStlAlignmentSlack(DensitySampler->GetSamplingCacheStorageBytes());
  if(ChunkCoarseCache){
    // Each vector uses the same4096-entry domain;both buffers exceed the
    // threshold at maximum pad. Exact capacities are reported by the cache.
    const auto capacities=ChunkCoarseCache->GetStorageCapacities();
    slack+=PipelineStlAlignmentSlack(capacities[0])+PipelineStlAlignmentSlack(capacities[1]);
  }
  set(PipelineStorageCell::StlAlignment,slack);
  out.idle=!ChunkCoarseCache && (!DensitySampler || !DensitySampler->GetSamplingCacheStorageBytes()) && !LakeMemoEntryCount;
  out.overflow=text==SIZE_MAX || callbacks==SIZE_MAX;out.supported=out.supported && !out.overflow;return out;
}

// Pipeline: climate sample -> height -> biomes -> surface fill -> ravines ->
// caves -> fluids -> ores -> vegetation -> ground_cover -> decoration ->
// structures -> lava -> fire.
UComposableWorldGenerator::UComposableWorldGenerator(
    WorldGenContext ctx, ComposableWorldGenConfig config)
    : IUWorldGenPipeline(std::move(ctx)), Config(ApplyPackPipelineMask(config)),
      StageMask(BuildWorldGenStageMask(Config, Ctx.Settings,
                                       UWorldGenPack::Get().Pipeline))
{
  if (Config.Aquifers && Ctx.Settings.EnableCaves &&
      (Ctx.Settings.FillWater || Ctx.Settings.FillLava))
  {
    AquiferSampler.emplace(
        Ctx.Settings.Seed, Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight,
        UWorldGenPack::AquifersConfig(), Ctx.Settings.MinHeight);
  }
  if (Config.TerrainMode == ComposableTerrainMode::Density3D)
  {
    DensityFieldParams density_params;
    density_params.routerV3 = Ctx.Settings.WorldGenProfileVersion >= 3;
    density_params.cavesInDensity = Config.Caves && Ctx.Settings.EnableCaves;
    density_params.caveAmplitude =
        std::max(0.0f, DensityFieldCaveAmplitudeBlocks(
                           Ctx.Settings.Caves.densityCaveAmplitude));
    const PackValleysConfig &valley_config = UWorldGenPack::ValleysConfig();
    density_params.riversEnabled =
        Config.Valleys && Ctx.Settings.WorldGenProfileVersion >= 2 &&
        (Ctx.Settings.WorldGenProfileVersion >= 3 ||
         !Ctx.Settings.Tuning.useDensityRefineParity);
    density_params.riverWidth = Ctx.Settings.Tuning.riverWidth;
    if (valley_config.Loaded)
    {
      density_params.riversEnabled =
          density_params.riversEnabled && valley_config.Enabled;
      density_params.riverNoiseScale = valley_config.RiverNoiseScale;
    }
    DensitySampler.emplace(Ctx.Settings.Seed, Ctx.Settings.SeaLevel,
                           Ctx.Settings.MaxHeight,
                           Ctx.Settings.Tuning.terrainRoughness, density_params,
                           Ctx.Settings.Caves, Ctx.Settings.MinHeight);
  }
  else if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap)
  {
    HeightSampler.emplace(Ctx.Settings.Seed, Ctx.Settings.SeaLevel,
                          Ctx.Settings.MaxHeight, Config.HeightPreset,
                          Ctx.Settings.Tuning.terrainRoughness,
                          Ctx.Settings.MinHeight);
  }
  if (Config.UseBiomeSurface)
  {
    BiomeSampler.emplace(Ctx.Settings.Seed, Ctx.Settings.Tuning,
                         Ctx.Settings.WorldGenProfileVersion);
    if (UWorldGenPack::Get().BiomeMode == WorldGenBiomeMode::Image)
    {
      BiomeSource = std::make_unique<ImageMapBiomeSource>(*BiomeSampler);
    }
    else
    {
      BiomeSource =
          std::make_unique<UProceduralClimateBiomeSource>(*BiomeSampler);
    }
    if (HeightSampler || DensitySampler)
    {
      DirectCoarseHeightFn = DirectCoarseCallback{this};
    }
    if (DirectCoarseHeightFn)
    {
      BiomeSampler->SetCoarseHeightCallback(DirectCoarseHeightFn);
    }
  }
  if (HeightSampler || DensitySampler)
  {
    SampleBuilder.emplace(HeightSampler.has_value() ? &*HeightSampler : nullptr,
                          BiomeSampler.has_value() ? &*BiomeSampler : nullptr,
                          Ctx.Settings, BiomeSource.get());
  }
  Ctx.CoarseSurfaceYAt = SampleCoarseCallback{this};
}

void UComposableWorldGenerator::BeginChunkCoarseCache(int base_x, int base_z,
                                                      int pad)
{
  EndChunkCoarseCache();
  if(pad<0 || pad>UDensityFieldSampler::kMaxChunkCachePad) return;
  const int64_t origin_x=int64_t(base_x)-pad,origin_z=int64_t(base_z)-pad;
  if(origin_x<std::numeric_limits<int>::min() || origin_z<std::numeric_limits<int>::min()) return;
  if (DensitySampler)
  {
    DensitySampler->BeginChunkSamplingCache(base_x, base_z, pad);
  }
  if (!DirectCoarseHeightFn)
  {
    return;
  }
  const int size = CHUNK_SIZE + pad * 2;
  ChunkCoarseCache.emplace(static_cast<int>(origin_x),static_cast<int>(origin_z),size,size);
  UCoarseHeightCache *cache = &*ChunkCoarseCache;
  if (BiomeSampler)
  {
    CoarseHeightCallback direct = DirectCoarseHeightFn;
    BiomeSampler->SetCoarseHeightCallback(CachedCoarseCallback{cache,std::move(direct)});
  }
}

void UComposableWorldGenerator::PrimeChunkCoarseY(int x, int z, int y)
{
  if (ChunkCoarseCache)
  {
    ChunkCoarseCache->Put(x, z, y);
  }
}

void UComposableWorldGenerator::EndChunkCoarseCache()
{
  LakeMemoEntryCount=0;
  if (DensitySampler)
  {
    DensitySampler->EndChunkSamplingCache();
  }
  if (!BiomeSampler)
  {
    ChunkCoarseCache.reset();
    return;
  }
  if (DirectCoarseHeightFn)
  {
    BiomeSampler->SetCoarseHeightCallback(DirectCoarseHeightFn);
  }
  ChunkCoarseCache.reset();
}

ColumnSampleContext
UComposableWorldGenerator::BuildColumnSample(int world_x, int world_z) const
{
  if (SampleBuilder && HeightSampler)
  {
    return SampleBuilder->Build(world_x, world_z);
  }
  if (DensitySampler && BiomeSampler)
  {
    ColumnSampleContext sample;
    sample.Climate = SampleClimate(world_x, world_z, Ctx.Settings.Seed);
    sample.MacroHeight01 = std::clamp(
        OverworldMacroHeight01(world_x, world_z, Ctx.Settings.Seed), 0.f, 1.f);
    sample.PreliminarySurfaceY = SampleCoarseSurfaceY(world_x, world_z);
    sample.Biomes =
        BiomeSampler->WeightsAt(world_x, world_z, sample.PreliminarySurfaceY,
                                Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
    sample.DominantBiome = DominantBiome(sample.Biomes);
    sample.SurfaceBiomeHandle =
        PickSurfaceBiomeHandle(world_x, world_z, sample.Biomes);
    sample.SurfaceBiomeKey = BiomeKeyRefForHandle(sample.SurfaceBiomeHandle);
    const int raw_y = DensitySampler->SurfaceYAt(world_x, world_z);
    sample.RawSurfaceY = raw_y;
    sample.SurfaceY = raw_y;
    // Profile 3 keeps the voxel surface identical to the density field; the
    // integer refinement chain only stays for legacy profiles.
    if (Ctx.Settings.Tuning.useDensityRefineParity &&
        Ctx.Settings.WorldGenProfileVersion < 3)
    {
      sample.SurfaceY =
          BiomeSampler->RefineSurfaceY(world_x, world_z, raw_y, Ctx.Settings);
    }
    const CoarseHeightCallback coarse_fn = [this](int hx, int hz)
    { return SampleCoarseSurfaceY(hx, hz); };
    sample.SurfaceGradient =
        SampleCoarseHeightGradient(world_x, world_z, coarse_fn);
    sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, sample.Biomes);
    sample.SpawnSurfaceOverride =
        ShouldSpawnSurfaceOverride(world_x, world_z, Ctx.Settings);
    const float denom =
        std::max(1.f, static_cast<float>(Ctx.Settings.MaxHeight -
                                         Ctx.Settings.SeaLevel));
    sample.HeightNorm = std::clamp(
        static_cast<float>(sample.SurfaceY - Ctx.Settings.SeaLevel) / denom,
        0.f, 1.f);
    sample.CoastFactor = ComputeCoastBeachStrength(
        world_x, world_z, sample.SurfaceY, Ctx.Settings, coarse_fn);
    return sample;
  }
  ColumnSampleContext sample;
  sample.PreliminarySurfaceY = SampleCoarseSurfaceY(world_x, world_z);
  sample.SurfaceY = SampleSurfaceY(world_x, world_z);
  sample.Biomes =
      SampleBiomeWeights(world_x, world_z, sample.PreliminarySurfaceY);
  sample.DominantBiome = DominantBiome(sample.Biomes);
  sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, sample.Biomes);
  return sample;
}

BiomeId UComposableWorldGenerator::BiomeAt3D(int world_x, int world_y,
                                             int world_z, int surface_y) const
{
  if (!BiomeSampler)
  {
    return BiomeId::Plains;
  }
  if (BiomeSource)
  {
    return BiomeSource->Sample3D(world_x, world_y, world_z, surface_y,
                                 Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
  }
  return BiomeSampler->At3D(world_x, world_y, world_z, surface_y,
                            Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
}

BiomeKey UComposableWorldGenerator::BiomeKeyAt3D(int world_x, int world_y,
                                                 int world_z,
                                                 int surface_y) const
{return BiomeKeyRefAt3D(world_x,world_y,world_z,surface_y).Get();}

BiomeKeyRef UComposableWorldGenerator::BiomeKeyRefAt3D(int world_x,int world_y,int world_z,int surface_y) const
{
  if (!BiomeSampler)
  {
    return BiomeKeyRefForId(BiomeId::Plains);
  }
  if (BiomeSource)
  {
    return BiomeSource->Sample3DKeyRef(world_x, world_y, world_z, surface_y,
                                    Ctx.Settings.SeaLevel,
                                    Ctx.Settings.MaxHeight);
  }
  return BiomeSampler->At3DKeyRef(world_x, world_y, world_z, surface_y,
                               Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
}

int UComposableWorldGenerator::SampleSurfaceY(int world_x, int world_z) const
{
  if (SampleBuilder && HeightSampler)
  {
    return BuildColumnSample(world_x, world_z).SurfaceY;
  }
  if (DensitySampler && BiomeSampler)
  {
    return BuildColumnSample(world_x, world_z).SurfaceY;
  }
  switch (Config.TerrainMode)
  {
  case ComposableTerrainMode::Flat:
    return Ctx.Settings.FlatSurfaceY;
  case ComposableTerrainMode::LegacyHash:
    return LegacyHashSurfaceY(world_x, world_z, Ctx.Settings);
  case ComposableTerrainMode::NoiseHeightmap:
  default:
  {
    int natural_y = HeightSampler->SurfaceYAt(world_x, world_z);
    if (Config.UseBiomeSurface && BiomeSampler)
    {
      natural_y = BiomeSampler->RefineSurfaceY(world_x, world_z, natural_y,
                                               Ctx.Settings);
    }
    return natural_y;
  }
  case ComposableTerrainMode::Density3D:
    return DensitySampler->SurfaceYAt(world_x, world_z);
  }
}

int UComposableWorldGenerator::SampleCoarseSurfaceY(int world_x,
                                                    int world_z) const
{
  if (ChunkCoarseCache && DirectCoarseHeightFn)
  {
    return ChunkCoarseCache->GetOrCompute(world_x, world_z,
                                          DirectCoarseHeightFn);
  }
  return ComputeCoarseSurfaceY(world_x, world_z);
}

int UComposableWorldGenerator::ComputeCoarseSurfaceY(int world_x,
                                                     int world_z) const
{
  if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap &&
      HeightSampler)
  {
    return HeightSampler->CoarseSurfaceYAt(world_x, world_z);
  }
  if (Config.TerrainMode == ComposableTerrainMode::Density3D && DensitySampler)
  {
    return DensitySampler->CoarseSurfaceYAt(world_x, world_z);
  }
  return SampleSurfaceY(world_x, world_z);
}

BiomeWeightSet UComposableWorldGenerator::SampleBiomeWeights(int world_x,
                                                             int world_z,
                                                             int coarse_y) const
{
  if (BiomeSource)
  {
    return BiomeSource->SampleSurfaceWeights(world_x, world_z, coarse_y,
                                             Ctx.Settings.SeaLevel,
                                             Ctx.Settings.MaxHeight);
  }
  if (!BiomeSampler)
  {
    BiomeWeightSet weights;
    weights.SetWeight(BiomeHandleForId(BiomeId::Plains), 1.0f);
    return weights;
  }
  return BiomeSampler->WeightsAt(world_x, world_z, coarse_y,
                                 Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
}

BiomeId UComposableWorldGenerator::SampleBiome(int world_x, int world_z,
                                               int coarse_y) const
{
  return DominantBiome(SampleBiomeWeights(world_x, world_z, coarse_y));
}

ColumnLayerRule UComposableWorldGenerator::BuildTerrainRuleFromSample(
    int world_x, int world_z, const ColumnSampleContext &sample) const
{
  ColumnLayerRule rule;
  rule.fillerBlock = Ctx.Blocks.Stone;

  if (Config.UseBiomeSurface && BiomeSampler)
  {
    const BiomeSurfaceRule biome_rule =
        EvaluateSurfaceRule(world_x, world_z, sample, Ctx);
    rule.surfaceBlock = biome_rule.surface;
    rule.subsurfaceBlock = biome_rule.subsurface;
    return rule;
  }

  rule.surfaceBlock = Ctx.Blocks.Grass;
  rule.subsurfaceBlock = Ctx.Blocks.Dirt;
  if ((Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap ||
       Config.TerrainMode == ComposableTerrainMode::Density3D) &&
      Config.HeightPreset == HeightPreset::Mountains &&
      (HeightSampler || DensitySampler))
  {
    int stone_above = -1;
    if (HeightSampler)
    {
      stone_above = HeightSampler->params().stoneSurfaceAboveY;
    }
    else if (DensitySampler)
    {
      stone_above = MountainsStoneSurfaceAboveY(Ctx.Settings.SeaLevel,
                                                Ctx.Settings.MaxHeight);
    }
    if (stone_above > 0 && sample.SurfaceY >= stone_above)
    {
      rule.surfaceBlock = Ctx.Blocks.Stone;
      rule.subsurfaceBlock = Ctx.Blocks.Stone;
    }
  }
  return rule;
}

ColumnLayerRule
UComposableWorldGenerator::BuildTerrainRule(int world_x, int world_z,
                                            int surface_y, BiomeId biome,
                                            const BiomeWeightSet &weights) const
{
  ColumnSampleContext sample;
  sample.SurfaceY = surface_y;
  sample.Biomes = weights;
  sample.SurfaceBiomeHandle = PickSurfaceBiomeHandle(world_x, world_z, weights);
  sample.SurfaceBiomeKey = BiomeKeyRefForHandle(sample.SurfaceBiomeHandle);
  sample.DominantBiome = biome;
  sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, weights);
  sample.MacroHeight01 = std::clamp(
      OverworldMacroHeight01(world_x, world_z, Ctx.Settings.Seed), 0.f, 1.f);
  sample.SpawnSurfaceOverride =
      ShouldSpawnSurfaceOverride(world_x, world_z, Ctx.Settings);
  return BuildTerrainRuleFromSample(world_x, world_z, sample);
}

void UComposableWorldGenerator::GenerateColumn(int world_x, int world_z)
{
  UColumnGenerationService::GenerateColumn(*this, world_x, world_z);
}

int UComposableWorldGenerator::SurfaceYAt(int world_x, int world_z) const
{
  return SampleSurfaceY(world_x, world_z);
}

int UComposableWorldGenerator::LakeCenterSurfaceYAt(int world_x,
                                                    int world_z) const
{
  const uint64_t key =
      (static_cast<uint64_t>(static_cast<uint32_t>(world_x)) << 32U) |
      static_cast<uint32_t>(world_z);
  const auto first=LakeCenterSurfaceYCache.begin();
  auto position=std::lower_bound(first,first+LakeMemoEntryCount,key,
      [](const LakeMemoEntry &entry,uint64_t value) {return entry.key<value;});
  if (position!=first+LakeMemoEntryCount && position->key==key)
  {
    return position->value;
  }
  const int surfaceY = SurfaceYAt(world_x, world_z);
  if (LakeMemoEntryCount==LakeCenterSurfaceYCache.size())
  {
    LakeMemoEntryCount=0;
    position=first;
  }
  std::move_backward(position,first+LakeMemoEntryCount,first+LakeMemoEntryCount+1);
  *position={key,surfaceY};
  ++LakeMemoEntryCount;
  return surfaceY;
}

} // namespace cutum
