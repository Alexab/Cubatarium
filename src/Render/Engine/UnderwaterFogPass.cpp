#include "Render/Engine/UnderwaterFogPass.h"

#include "Render/Camera/Camera.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/FluidUnderwaterFogLogic.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"

#include <glm/gtc/type_ptr.hpp>

namespace cutum
{

void UUnderwaterFogPass::Update(UWorld &world, const RenderSettings &render,
                                UFluidSurfaceMap &surface_map,
                                const glm::vec3 &base_sky_color)
{
  auto camera = world.GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }

  const glm::vec3 eye = camera->GetPosition();
  const SampledFluidState fluid =
      world.SampleFluidPhysics(eye, camera->GetPlayerCapsule());
  const FluidColumnSurface column = world.FindFluidColumnSurface(eye);
  BlockId eyeFluid = BLOCK_AIR;
  const bool cameraInFluid = column.valid && eye.y < column.surfaceY;
  CameraInFluid = cameraInFluid;
  if (cameraInFluid)
  {
    eyeFluid = column.fluidId;
  }

  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
  const int eyeBlockY = WorldCoordToBlockIndex(eye.y);
  const glm::ivec3 cameraBlockXZ(WorldCoordToBlockIndex(eye.x), eyeBlockY,
                                 WorldCoordToBlockIndex(eye.z));
  const bool nearbyFluid =
      cameraInFluid || world.HasNearbyFluidSurface(cameraBlockXZ);
  bool mapReady = surface_map.IsValid();
  if (nearbyFluid)
  {
    mapReady = surface_map.Update(world.GetBlockWorld(), registry,
                                  mesh_service.GetCache(), cameraBlockXZ,
                                  eyeBlockY, mesh_service.GetMeshRevision());
  }
  const bool columnFogActive =
      cutum::ShouldUsePerColumnBelowSurfaceFog(mapReady, nearbyFluid);
  BelowSurfaceFogStrength =
      cutum::BelowSurfaceFogStrength(columnFogActive, cameraInFluid);
  BelowSurfaceFogDepthMin = cutum::BelowSurfaceFogDepthMin(cameraInFluid);

  BelowSurfaceFogColors.fill(glm::vec3(0.0f));
  BelowSurfaceFogMin = 0.52f;
  BelowSurfaceFogScale = 0.35f;
  const BlockId water_id = registry.GetIdByTypeName("water");
  const BlockId lava_id = registry.GetIdByTypeName("lava");
  if (water_id != BLOCK_AIR)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(water_id))
    {
      BelowSurfaceFogColors[1] = fv->FogColor;
      BelowSurfaceFogMin = fv->BelowSurfaceFogMin;
      BelowSurfaceFogScale = fv->BelowSurfaceFogScale;
    }
  }
  if (lava_id != BLOCK_AIR)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(lava_id))
    {
      BelowSurfaceFogColors[2] = fv->FogColor;
    }
  }

  glm::vec3 targetSky = base_sky_color;
  FogEnabled = 0.0f;
  FogHorizontal = 0.0f;
  FogHorizonBlend = 0.0f;
  OverlayTintAlpha = 0.0f;
  OverlayBlockId = BLOCK_AIR;

  const bool enteringUnderwater = cameraInFluid && !WasUnderwaterFog;
  const float underwaterFogMix = enteringUnderwater ? 1.0f : 0.15f;
  if (cameraInFluid)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(eyeFluid))
    {
      if (registry.GetRenderStyle(eyeFluid) == BlockRenderStyle::Fluid)
      {
        if (ShouldUseGlobalUnderwaterFog(cameraInFluid, mapReady))
        {
          FogEnabled = 1.0f;
          FogStart = fv->FogStart;
          FogEnd = fv->FogEnd;
          FogMinBlend = fv->FogMinBlend;
        }
        targetSky = fv->FogColor;
        if (enteringUnderwater)
        {
          SmoothedFogColor = fv->FogColor;
          SmoothedSkyTint = fv->FogColor;
        }
        else
        {
          SmoothedFogColor =
              glm::mix(SmoothedFogColor, fv->FogColor, underwaterFogMix);
        }
      }
    }
  }
  else if (render.DistanceFog)
  {
    const DistanceFogParams distance_fog = ComputeDistanceFog(
        world.GetEffectiveRenderDistance(), SmoothedSkyTint,
        render.DistanceFogStartRatio, world.GetEffectiveFogStartRatio(),
        render.DistanceFogDensity);
    FogEnabled = 1.0f;
    FogStart = distance_fog.Start;
    FogEnd = distance_fog.End;
    FogDensity = distance_fog.Density;
    FogMinBlend = 0.0f;
    FogHorizontal = render.DistanceFogHorizontal ? 1.0f : 0.0f;
    FogHorizonBlend = 1.0f;
    SmoothedFogColor = glm::mix(SmoothedFogColor, distance_fog.Color, 0.15f);
  }
  if (fluid.inFluid)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(fluid.dominantFluid))
    {
      if (fv->OverlayAlpha > 0.01f &&
          registry.GetRenderStyle(fluid.dominantFluid) ==
              BlockRenderStyle::Cross)
      {
        OverlayTintAlpha = fv->OverlayAlpha;
        OverlayTintColor = fv->OverlayColor;
        OverlayBlockId = fluid.dominantFluid;
      }
    }
  }

  if (!enteringUnderwater)
  {
    SmoothedSkyTint = glm::mix(SmoothedSkyTint, targetSky,
                               cameraInFluid ? underwaterFogMix : 0.15f);
  }
  WasUnderwaterFog = cameraInFluid;
}

void UUnderwaterFogPass::ApplyUniforms(
    const std::shared_ptr<UShaderProgram> &shader, const glm::vec3 &camera_pos,
    const UFluidSurfaceMap &surface_map, bool apply_below_surface_fog) const
{
  shader->SetVec3("uCameraPos", camera_pos);
  shader->SetVec3("uFogColor", SmoothedFogColor);
  shader->SetFloat("uFogStart", FogStart);
  shader->SetFloat("uFogEnd", FogEnd);
  shader->SetFloat("uFogMinBlend", FogMinBlend);
  shader->SetFloat("uFogEnabled", FogEnabled);
  shader->SetFloat("uFogHorizontal", FogHorizontal);
  shader->SetFloat("uFogDensity", FogDensity);
  const float below_surface_fog =
      apply_below_surface_fog ? BelowSurfaceFogStrength : 0.0f;
  shader->SetFloat("uBelowSurfaceFog", below_surface_fog);
  shader->SetFloat("uBelowSurfaceFogMin", BelowSurfaceFogMin);
  shader->SetFloat("uBelowSurfaceFogScale", BelowSurfaceFogScale);
  shader->SetFloat("uBelowSurfaceFogDepthMin", BelowSurfaceFogDepthMin);
  if (below_surface_fog > 0.001f && surface_map.IsValid())
  {
    shader->SetVec2("uFluidSurfaceOrigin", surface_map.GetOriginBlockXZ());
    shader->SetVec2("uFluidSurfaceInvSize", surface_map.GetInvSizeBlocks());
    shader->SetInt("uFluidSurfaceYMap", 1);
    shader->SetInt("uFluidIndexMap", 2);
    shader->SetInt("uFluidBottomBlockMap", 3);
    surface_map.Bind(1, 2, 3);
  }
  else
  {
    shader->SetVec2("uFluidSurfaceOrigin", glm::vec2(0.0f));
    shader->SetVec2("uFluidSurfaceInvSize", glm::vec2(0.0f));
    shader->SetFloat("uBelowSurfaceFogDepthMin", 0.0f);
    shader->SetInt("uFluidSurfaceYMap", 1);
    shader->SetInt("uFluidIndexMap", 2);
    shader->SetInt("uFluidBottomBlockMap", 3);
  }
  const GLint colorLoc = shader->GetUniformLocation("uBelowSurfaceFogColors");
  if (colorLoc != -1)
  {
    glUniform3fv(colorLoc, UFluidSurfaceMap::kMaxFluidShaderSlots,
                 glm::value_ptr(BelowSurfaceFogColors[0]));
  }
}

void UUnderwaterFogPass::ResetSkyTint(const glm::vec3 &base_sky_color)
{
  SmoothedSkyTint = base_sky_color;
}

} // namespace cutum
