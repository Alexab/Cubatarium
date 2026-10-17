#ifndef RENDER_TEXTURE_UNITS_H
#define RENDER_TEXTURE_UNITS_H

#include "Render/GlIncludes.h"

namespace cutum
{

// Fragment sampler allocation shared by render passes. Keep these indices
// distinct: fog and scene-depth samplers are applied to the same greedy shader.
enum class RenderTextureUnit : GLint
{
  BlockAtlas = 0,
  FluidSurfaceY = 1,
  FluidIndex = 2,
  FluidBottomBlock = 3,
  WeatherDepth = 4,
  OpaqueDepth = 5,
};

constexpr GLint RenderTextureUnitIndex(RenderTextureUnit unit)
{
  return static_cast<GLint>(unit);
}

constexpr GLenum RenderTextureUnitEnum(RenderTextureUnit unit)
{
  return GL_TEXTURE0 + static_cast<GLenum>(RenderTextureUnitIndex(unit));
}

static_assert(RenderTextureUnitIndex(RenderTextureUnit::BlockAtlas) <
                  RenderTextureUnitIndex(RenderTextureUnit::FluidSurfaceY) &&
              RenderTextureUnitIndex(RenderTextureUnit::FluidSurfaceY) <
                  RenderTextureUnitIndex(RenderTextureUnit::FluidIndex) &&
              RenderTextureUnitIndex(RenderTextureUnit::FluidIndex) <
                  RenderTextureUnitIndex(RenderTextureUnit::FluidBottomBlock) &&
              RenderTextureUnitIndex(RenderTextureUnit::FluidBottomBlock) <
                  RenderTextureUnitIndex(RenderTextureUnit::WeatherDepth) &&
              RenderTextureUnitIndex(RenderTextureUnit::WeatherDepth) <
                  RenderTextureUnitIndex(RenderTextureUnit::OpaqueDepth),
              "render sampler units must remain distinct");

} // namespace cutum

#endif
