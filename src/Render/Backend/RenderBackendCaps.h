#pragma once

#include <string>

namespace cutum
{

enum class RenderPlatformKind
{
  Desktop,
  Android
};

enum class MesherBackendKind
{
  CpuGreedy,
  GpuGreedy,
  AndroidHybridGpu
};

enum class MeshStoreBackendKind
{
  CpuStaging,
  MdiVertexPool
};

enum class CullBackendKind
{
  CpuFrustum,
  GpuFrustum
};

struct RenderBackendCaps
{
  RenderPlatformKind Platform{RenderPlatformKind::Desktop};
  bool HasCompute{false};
  bool HasMultiDrawIndirect{false};
  bool HasSsbo{false};
  bool HasGlMapBufferRange{false};
  bool PreferSinglePassTransparent{false};
  bool ForceCpuBackends{false};
  /// Effective Android GPU after probe + policy (GPU-by-default when capable).
  bool AllowAndroidGpu{false};
  bool ProbeCompleted{false};
  std::string GlVersion;
  std::string GlRenderer;
  /// Telemetry: user_off | probe_fail | allowlist | force_cpu | ok | n/a
  std::string AndroidGpuDenyReason{"n/a"};
};

struct RenderBackendSelection
{
  MesherBackendKind Mesher{MesherBackendKind::CpuGreedy};
  MeshStoreBackendKind Store{MeshStoreBackendKind::CpuStaging};
  CullBackendKind Cull{CullBackendKind::CpuFrustum};
  bool Bound{false};
};

/// Prefer Gpu skylight seed when desktop compute caps allow.
inline bool PreferGpuLightingSeed(const RenderBackendCaps &caps)
{
  if (caps.ForceCpuBackends)
  {
    return false;
  }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  (void)caps;
  return false;
#else
  return caps.HasCompute && caps.HasSsbo &&
         caps.Platform == RenderPlatformKind::Desktop;
#endif
}

/// Heuristic: discrete GPU (safe for SSBO atomics counting-sort). iGPU deny
/// list matches AMD/Intel integrated that regressed emerge when sort enabled.
inline bool PreferGpuOpaqueCountingSort(const RenderBackendCaps &caps)
{
  if (caps.ForceCpuBackends || !caps.HasCompute || !caps.HasSsbo)
  {
    return false;
  }
  if (caps.Platform != RenderPlatformKind::Desktop)
  {
    return false;
  }
  const std::string &r = caps.GlRenderer;
  // Known iGPU / APU strings — keep CPU counting-sort (emerge regress).
  if (r.find("AMD Radeon Graphics") != std::string::npos ||
      r.find("Radeon(TM) Graphics") != std::string::npos ||
      r.find("Intel(R) UHD") != std::string::npos ||
      r.find("Intel(R) HD") != std::string::npos ||
      r.find("Intel(R) Iris") != std::string::npos ||
      r.find("Mali-") != std::string::npos ||
      r.find("Adreno") != std::string::npos)
  {
    return false;
  }
  // Discrete-ish tokens.
  return r.find("GeForce") != std::string::npos ||
         r.find("RTX") != std::string::npos ||
         r.find("GTX") != std::string::npos ||
         r.find("Radeon RX") != std::string::npos ||
         r.find("Radeon Pro") != std::string::npos ||
         r.find("Arc") != std::string::npos;
}

/// Platform compile-time defaults (no GL context required).
RenderBackendCaps DetectRenderBackendCaps();

/// Mutate caps from the current GL context (call after glewInit / eglMakeCurrent).
void ProbeOpenGLRenderBackendCaps(RenderBackendCaps &caps);

/// Session cache: Detect → Probe → Policy → BindOnce.
void SetActiveRenderBackendCaps(const RenderBackendCaps &caps);
const RenderBackendCaps &GetActiveRenderBackendCaps();
void InvalidateRenderBackendCapsCache();

/// Detect + Probe + store (policy applied separately with RenderSettings).
void RefreshRenderBackendCapsFromGl();

} // namespace cutum
