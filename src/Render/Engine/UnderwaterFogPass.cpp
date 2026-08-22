#include "Render/Engine/UnderwaterFogPass.h"

#include "Render/Camera/Camera.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/FluidUnderwaterFogLogic.h"
#include "Render/Engine/HorizonFogColor.h"
#include "Render/GlIncludes.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"

#include <glm/gtc/type_ptr.hpp>

namespace cutum
{

void UUnderwaterFogPass::Update(UWorld &world, const RenderSettings &render,
                                UFluidSurfaceMap &surface_map,
                                const glm::vec3 &base_sky_color,
                                const glm::mat3 &inv_view_rot)
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
  BlockId eye_fluid = BLOCK_AIR;
  const bool camera_submerged = column.valid && eye.y < column.surfaceY;
  CameraInFluid = camera_submerged;
  if (camera_submerged)
  {
    eye_fluid = column.fluidId;
  }

  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const int eye_block_y = WorldCoordToBlockIndex(eye.y);
  const glm::ivec3 camera_block_xz(WorldCoordToBlockIndex(eye.x), eye_block_y,
                                   WorldCoordToBlockIndex(eye.z));
  const bool nearby_fluid =
      camera_submerged || world.HasNearbyFluidSurface(camera_block_xz);
  bool map_ready = surface_map.IsValid();
  if (nearby_fluid)
  {
    const bool moving =
        world.GetLastMovementSpeed() >
        world.GetProceduralSettings().MovementPrefetchThreshold;
    const bool enter_throttle = FluidMapShouldThrottleEnter(
        world.IsEnterFovLitPassActive(),
        world.GetPhysicsTelemetry().PostLoadRingNotReady > 0,
        world.GetPhysicsTelemetry().VisibleBlackFocusN,
        world.GetLastFluidMapDirtyChunks());
    const bool cruise_throttle =
        FluidMapShouldThrottleCruise(
            world.GetLastFluidMapDirtyChunks(), world.GetLastMovementFrameMs(),
            moving, world.GetPhysicsTelemetry().DarkFaceVoidNearN,
            world.GetPhysicsTelemetry().VisibleBlackFocusN) ||
        enter_throttle;
    map_ready = surface_map.Update(
        world.GetBlockWorld(), registry, mesh_service.GetCache(),
        camera_block_xz, eye_block_y, mesh_service.GetMeshRevision(),
        world.GetLastMovementFrameMs(), cruise_throttle, enter_throttle);
  }

  const bool partial_submerge =
      column.valid && cutum::IsPartialSubmerge(eye.y, column.surfaceY);
  const bool column_underwater_fog =
      cutum::ShouldApplyUnderwaterFogToColumn(
          camera_submerged, partial_submerge, column.surfaceBlockY,
          column.bottomBlockY);
  const bool per_column_active =
      cutum::ShouldUsePerColumnUnderwaterFog(map_ready, nearby_fluid) &&
      column_underwater_fog;

  UnderwaterFogColors.fill(glm::vec3(0.0f));
  UnderwaterFogStart = 0.0f;
  UnderwaterFogEnd = 9.0f;
  UnderwaterFogMinBlend = 0.5f;
  UnderwaterFogColor = glm::vec3(0.05f, 0.15f, 0.35f);
  const BlockId water_id = registry.GetIdByTypeName("water");
  const BlockId lava_id = registry.GetIdByTypeName("lava");
  if (water_id != BLOCK_AIR)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(water_id))
    {
      UnderwaterFogColors[1] = fv->FogColor;
      UnderwaterFogStart = fv->FogStart;
      UnderwaterFogEnd = fv->FogEnd;
      UnderwaterFogMinBlend = fv->FogMinBlend;
      UnderwaterFogColor = fv->FogColor;
    }
  }
  if (lava_id != BLOCK_AIR)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(lava_id))
    {
      UnderwaterFogColors[2] = fv->FogColor;
    }
  }
  if (camera_submerged && eye_fluid != BLOCK_AIR)
  {
    if (const FluidViewProfile *fv = registry.GetFluidView(eye_fluid))
    {
      UnderwaterFogColor = fv->FogColor;
      UnderwaterFogStart = fv->FogStart;
      UnderwaterFogEnd = fv->FogEnd;
      UnderwaterFogMinBlend = fv->FogMinBlend;
    }
  }

  CameraColumnSurfaceY = column.valid ? column.surfaceY : 1e9f;
  CameraColumnFluidIndex =
      column.valid
          ? static_cast<float>(FluidSurfaceIndexForBlock(column.fluidId,
                                                         registry))
          : 0.0f;
  CameraColumnBottomBlockY =
      column.valid ? static_cast<float>(column.bottomBlockY) : 1e9f;
  UnderwaterFogSubmerged = camera_submerged ? 1.0f : 0.0f;
  UnderwaterFogEnabled = per_column_active ? 1.0f : 0.0f;
  AirFogEnabled = 0.0f;

  const float day = std::clamp(env.DayNightFactor, 0.0f, 1.0f);
  const float moon = std::clamp(env.MoonNightFactor, 0.0f, 1.0f);
  HorizonFogColorInput color_in;
  color_in.base_sky = base_sky_color;
  color_in.day = day;
  color_in.moon = moon;
  color_in.weather_atten = env.WeatherSkyAttenuation;
  color_in.cloudiness = env.Cloudiness;
  color_in.precip = env.PrecipitationIntensity;
  color_in.celestial_bodies = &env.CelestialBodies;
  const AtmosphericSkyColors atmospheric = ComputeAtmosphericSkyColors(color_in);
  glm::vec3 target_sky = atmospheric.sky_tint;
  FogEnabled = 0.0f;
  FogHorizontal = 0.0f;
  FogHorizonBlend = 0.0f;
  FogHorizonElevation = 0.35f;
  OverlayTintAlpha = 0.0f;
  OverlayBlockId = BLOCK_AIR;

  const bool entering_underwater = camera_submerged && !WasUnderwaterFog;
  const float underwater_fog_mix = entering_underwater ? 1.0f : 0.15f;
  if (camera_submerged)
  {
    const bool use_global_fluid_fog =
        cutum::ShouldUseGlobalUnderwaterFog(camera_submerged, map_ready);
    if (const FluidViewProfile *fv = registry.GetFluidView(eye_fluid))
    {
      if (registry.GetRenderStyle(eye_fluid) == BlockRenderStyle::Fluid)
      {
        target_sky = fv->FogColor;
        if (entering_underwater)
        {
          SmoothedFogColor = fv->FogColor;
          SmoothedSkyTint = fv->FogColor;
        }
        else
        {
          SmoothedFogColor =
              glm::mix(SmoothedFogColor, fv->FogColor, underwater_fog_mix);
        }
      }
    }
    // Air distance fog masks shore / unfinished streaming edge. Per-column
    // underwater fog still covers fluid span (uUnderwaterFog*). When the
    // surface map is missing, global uFog* uses the air distance range so the
    // world edge stays hidden (near fluid is thicker than the 9-block preset).
    if (render.DistanceFog)
    {
      const DistanceFogParams distance_fog = ComputeDistanceFog(
          world.GetEffectiveFogRenderDistance(), atmospheric.fog_color,
          render.DistanceFogStartRatio, world.GetEffectiveFogStartRatio(),
          render.DistanceFogDensity,
          world.GetEffectiveFogEndMarginBlocks(
              render.DistanceFogEndMarginBlocks));
      FogStart = distance_fog.Start;
      FogEnd = distance_fog.End;
      FogDensity = distance_fog.Density;
      FogMinBlend = 0.0f;
      FogHorizontal = render.DistanceFogHorizontal ? 1.0f : 0.0f;
      FogHorizonBlend = 1.0f;
      if (use_global_fluid_fog)
      {
        FogEnabled = 1.0f;
        AirFogEnabled = 0.0f;
      }
      else
      {
        FogEnabled = 0.0f;
        AirFogEnabled = 1.0f;
      }
    }
    else if (use_global_fluid_fog)
    {
      if (const FluidViewProfile *fv = registry.GetFluidView(eye_fluid))
      {
        if (registry.GetRenderStyle(eye_fluid) == BlockRenderStyle::Fluid)
        {
          FogEnabled = 1.0f;
          FogStart = fv->FogStart;
          FogEnd = fv->FogEnd;
          FogMinBlend = fv->FogMinBlend;
        }
      }
    }
  }
  else if (render.DistanceFog)
  {
    const DistanceFogParams distance_fog = ComputeDistanceFog(
        world.GetEffectiveFogRenderDistance(), atmospheric.fog_color,
        render.DistanceFogStartRatio, world.GetEffectiveFogStartRatio(),
        render.DistanceFogDensity,
        world.GetEffectiveFogEndMarginBlocks(
            render.DistanceFogEndMarginBlocks));
    AirFogEnabled = 1.0f;
    FogStart = distance_fog.Start;
    FogEnd = distance_fog.End;
    FogDensity = distance_fog.Density;
    FogMinBlend = 0.0f;
    FogHorizontal = render.DistanceFogHorizontal ? 1.0f : 0.0f;
    FogHorizonBlend = 1.0f;
    SmoothedFogColor = glm::mix(SmoothedFogColor, distance_fog.Color, 0.2f);
    // B: blend already saturated at 1 — widen sky horizon band over unfinished
    // water so empty columns read as fog, not clear skydome.
    if (world.GetNearWaterUnfinishedFog())
    {
      FogHorizonElevation = 0.22f;
    }
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

  if (!entering_underwater)
  {
    SmoothedSkyTint = glm::mix(SmoothedSkyTint, target_sky,
                               camera_submerged ? underwater_fog_mix : 0.15f);
  }

  if (camera_submerged && !partial_submerge)
  {
    UnderwaterSkyAmount = 1.0f;
    ScreenWaterlineNdc = -2.0f;
  }
  else if (partial_submerge && column.valid)
  {
    UnderwaterSkyAmount = 0.0f;
    ScreenWaterlineNdc = cutum::ComputeScreenWaterlineNdc(
        eye, column.surfaceY, inv_view_rot);
  }
  else
  {
    UnderwaterSkyAmount = 0.0f;
    ScreenWaterlineNdc = -2.0f;
  }

  if (camera->IsIsometricProjection())
  {
    // Parallel projection: Minecraft-style XZ horizon fog and NDC waterline
    // splits read poorly; keep underwater local fog only.
    FogHorizontal = 0.0f;
    AirFogEnabled = 0.0f;
    ScreenWaterlineNdc = -2.0f;
  }

  WasUnderwaterFog = camera_submerged;
}

void UUnderwaterFogPass::ApplyUniforms(
    const std::shared_ptr<UShaderProgram> &shader, const glm::vec3 &camera_pos,
    const UFluidSurfaceMap &surface_map, bool apply_underwater_fog) const
{
  shader->SetVec3("uCameraPos", camera_pos);
  shader->SetVec3("uFogColor", SmoothedFogColor);
  shader->SetFloat("uFogStart", FogStart);
  shader->SetFloat("uFogEnd", FogEnd);
  shader->SetFloat("uFogMinBlend", FogMinBlend);
  shader->SetFloat("uFogEnabled", FogEnabled);
  shader->SetFloat("uFogHorizontal", FogHorizontal);
  shader->SetFloat("uFogDensity", FogDensity);
  shader->SetFloat("uAirFogEnabled", AirFogEnabled);
  const float underwater_fog_enabled =
      apply_underwater_fog ? UnderwaterFogEnabled : 0.0f;
  shader->SetFloat("uUnderwaterFogEnabled", underwater_fog_enabled);
  shader->SetFloat("uUnderwaterFogStart", UnderwaterFogStart);
  shader->SetFloat("uUnderwaterFogEnd", UnderwaterFogEnd);
  shader->SetFloat("uUnderwaterFogMinBlend", UnderwaterFogMinBlend);
  shader->SetFloat("uUnderwaterFogSubmerged", UnderwaterFogSubmerged);
  shader->SetFloat("uCameraColumnSurfaceY", CameraColumnSurfaceY);
  shader->SetFloat("uCameraColumnFluidIndex", CameraColumnFluidIndex);
  shader->SetFloat("uCameraColumnBottomBlockY", CameraColumnBottomBlockY);
  if (underwater_fog_enabled > 0.001f && surface_map.IsValid())
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
    shader->SetInt("uFluidSurfaceYMap", 1);
    shader->SetInt("uFluidIndexMap", 2);
    shader->SetInt("uFluidBottomBlockMap", 3);
  }
  const GLint color_loc = shader->GetUniformLocation("uUnderwaterFogColors");
  if (color_loc != -1)
  {
    glUniform3fv(color_loc, UFluidSurfaceMap::kMaxFluidShaderSlots,
                 glm::value_ptr(UnderwaterFogColors[0]));
  }
}

void UUnderwaterFogPass::ResetSkyTint(const glm::vec3 &base_sky_color)
{
  SmoothedSkyTint = base_sky_color;
}

void UUnderwaterFogPass::ResetAtmosphericColors(const glm::vec3 &sky_tint,
                                                const glm::vec3 &fog_color)
{
  SmoothedSkyTint = sky_tint;
  SmoothedFogColor = fog_color;
}

} // namespace cutum
