#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cutum
{
/// Immutable dense scratch domain. Index = x + width*(y + height*z).
/// Seeds must come from full-height geometry context, never old light bytes.
struct LightTransportInput
{
  uint32_t width{0}, height{0}, depth{0};
  std::vector<uint8_t> seeds; // sky low nibble, block high nibble
  std::vector<uint8_t> transmission; // 0 opaque, 1 transparent, 2 liquid
};
struct GpuLightTransportWork
{
  uint64_t dispatches{0}, readbacks{0}, scratch_bytes{0};
};
using GpuLightTransportExecutor = bool (*)(const LightTransportInput &,
    std::vector<uint8_t> &, GpuLightTransportWork &, std::string &);
/// Current desktop GL4.3 or GL3.3+ARB compute/SSBO context. Never performs CPU fallback or modifies
/// output on failure. Per-call resources retire before return; no global GL
/// objects may survive a context. Caller owns snapshot validation/publication.
bool TryGpuTransportLight(const LightTransportInput &input,
                           std::vector<uint8_t> &output,
                           GpuLightTransportWork &work, std::string &reason);
/// Count of actual submitted compute dispatch calls, including failed attempts.
uint64_t GpuLightTransportDispatchCount();
}
