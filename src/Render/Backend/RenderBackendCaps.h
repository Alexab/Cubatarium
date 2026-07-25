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
  bool ForceCpuBackends{false};
};

struct RenderBackendSelection
{
  MesherBackendKind Mesher{MesherBackendKind::CpuGreedy};
  MeshStoreBackendKind Store{MeshStoreBackendKind::CpuStaging};
  CullBackendKind Cull{CullBackendKind::CpuFrustum};
  bool Bound{false};
};

} // namespace cutum
