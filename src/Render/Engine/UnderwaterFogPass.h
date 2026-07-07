#ifndef UNDERWATERFOGPASS_H
#define UNDERWATERFOGPASS_H

#include "App/Settings/RenderSettings.h"
#include "Render/Engine/FluidSurfaceMap.h"
#include "Render/Engine/ShaderManager.h"
#include "World/Math/BlockTypes.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class UWorld;

class UUnderwaterFogPass
{
public:
  void Update(UWorld &world, const RenderSettings &render,
              UFluidSurfaceMap &surface_map, const glm::vec3 &base_sky_color);
  void ApplyUniforms(const std::shared_ptr<UShaderProgram> &shader,
                     const glm::vec3 &camera_pos,
                     const UFluidSurfaceMap &surface_map,
                     bool apply_below_surface_fog = true) const;
  void ResetSkyTint(const glm::vec3 &base_sky_color);

  const glm::vec3 &GetSkyTint() const { return SmoothedSkyTint; }
  const glm::vec3 &GetFogColor() const { return SmoothedFogColor; }
  float GetFogHorizonBlend() const { return FogHorizonBlend; }

  const glm::vec3 &GetOverlayTintColor() const { return OverlayTintColor; }
  float GetOverlayTintAlpha() const { return OverlayTintAlpha; }
  BlockId GetOverlayBlockId() const { return OverlayBlockId; }

private:
  glm::vec3 SmoothedSkyTint{0.5f, 0.7f, 1.0f};
  glm::vec3 SmoothedFogColor{0.05f, 0.15f, 0.35f};
  float FogStart{0.0f};
  float FogEnd{1000.0f};
  float FogMinBlend{0.0f};
  float FogEnabled{0.0f};
  float FogHorizontal{0.0f};
  float FogDensity{1.0f};
  float FogHorizonBlend{0.0f};
  bool WasUnderwaterFog{false};
  float BelowSurfaceFogStrength{0.0f};
  float BelowSurfaceFogMin{0.52f};
  float BelowSurfaceFogScale{0.35f};
  float BelowSurfaceFogDepthMin{0.0f};
  bool CameraInFluid{false};
  bool BelowSurfaceFogBlockDepth{false};
  std::array<glm::vec3, UFluidSurfaceMap::kMaxFluidShaderSlots>
      BelowSurfaceFogColors{};
  glm::vec3 OverlayTintColor{0.0f};
  float OverlayTintAlpha{0.0f};
  BlockId OverlayBlockId{BLOCK_AIR};
};

} // namespace cutum

#endif // UNDERWATERFOGPASS_H
