#ifndef GL_STATE_MASK_H
#define GL_STATE_MASK_H

#include <cstdint>

namespace cutum
{

enum class GlStateBit : uint32_t
{
  DepthTest = 1u << 0,
  DepthFunc = 1u << 1,
  DepthMask = 1u << 2,
  Blend = 1u << 3,
  CullFace = 1u << 4,
  StencilTest = 1u << 5,
  StencilOps = 1u << 6,
  ColorMask = 1u << 7,
  ViewportFb = 1u << 8,
};

using GlStateMask = uint32_t;

inline constexpr GlStateMask operator|(GlStateBit a, GlStateBit b)
{
  return static_cast<GlStateMask>(a) | static_cast<GlStateMask>(b);
}

inline constexpr GlStateMask operator|(GlStateMask a, GlStateBit b)
{
  return a | static_cast<GlStateMask>(b);
}

/// FBO icon render (UPrefabIconCache).
inline constexpr GlStateMask kGlMaskIconFbo =
    GlStateBit::ViewportFb | GlStateBit::DepthTest | GlStateBit::Blend |
    GlStateBit::CullFace;

/// Greedy transparent multi-pass (depth/stencil/color mask only).
inline constexpr GlStateMask kGlMaskTransparentPipeline =
    GlStateBit::DepthFunc | GlStateBit::DepthMask | GlStateBit::StencilTest |
    GlStateBit::StencilOps | GlStateBit::ColorMask;

} // namespace cutum

#endif
