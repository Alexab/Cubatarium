#pragma once

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
  GpuGreedy
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
  /// Opt-in for Android/GLES GPU backends (default off until A1+).
  bool AllowAndroidGpu{false};
};

struct RenderBackendSelection
{
  MesherBackendKind Mesher{MesherBackendKind::CpuGreedy};
  MeshStoreBackendKind Store{MeshStoreBackendKind::CpuStaging};
  CullBackendKind Cull{CullBackendKind::CpuFrustum};
  bool Bound{false};
};

/// Probe platform defaults (same behavior as former GeometryEngine hardcode).
RenderBackendCaps DetectRenderBackendCaps();

} // namespace cutum
