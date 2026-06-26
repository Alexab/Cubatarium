#include "WorldGen/Features/BuiltinWorldGenFeatures.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include <array>

namespace cutum
{

namespace
{

class ULavaPoolBuiltinFeature : public IBuiltinWorldGenFeature
{
public:
  WorldGenStageId StageId() const override
  {
    return WorldGenStageId::LavaPools;
  }

  bool TryPlace(WorldGenContext &ctx, const ColumnSampleContext &sample,
                int world_x, int world_z) const override
  {
    return TryPlaceLavaPool(ctx, world_x, world_z, sample.SurfaceY,
                            sample.DominantBiome);
  }
};

class UFirePatchBuiltinFeature : public IBuiltinWorldGenFeature
{
public:
  WorldGenStageId StageId() const override
  {
    return WorldGenStageId::FirePatch;
  }

  bool TryPlace(WorldGenContext &ctx, const ColumnSampleContext &sample,
                int world_x, int world_z) const override
  {
    if (ctx.Settings.FillFire && ctx.Prefabs && world_x == 8 && world_z == 8)
    {
      const glm::ivec3 anchor(world_x, sample.SurfaceY + 1, world_z);
      if (PlacePrefabAt(ctx, "fire_patch", anchor, sample.SurfaceY))
      {
        return true;
      }
    }
    return TryPlaceFirePatch(ctx, world_x, world_z, sample.SurfaceY,
                             sample.DominantBiome, ctx.Blocks.Grass);
  }
};

ULavaPoolBuiltinFeature gLavaPoolFeature;
UFirePatchBuiltinFeature gFirePatchFeature;

const std::array<const IBuiltinWorldGenFeature *, 2> kBuiltinFeatures = {
    &gLavaPoolFeature, &gFirePatchFeature};

} // namespace

const IBuiltinWorldGenFeature *BuiltinWorldGenFeatureFor(WorldGenStageId id)
{
  for (const IBuiltinWorldGenFeature *feature : kBuiltinFeatures)
  {
    if (feature->StageId() == id)
    {
      return feature;
    }
  }
  return nullptr;
}

} // namespace cutum
