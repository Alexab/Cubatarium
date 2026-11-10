#pragma once

#include "Render/Engine/FluidSurfaceMap.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Backend/RenderBackendCaps.h"
#include <memory>

namespace cutum
{

/// Fluid surface height/index provider. Bound once at init.
class IUFluidSurfaceProvider
{
public:
  virtual ~IUFluidSurfaceProvider() = default;
  virtual const char *BackendName() const = 0;
  virtual UFluidSurfaceMap &Map() = 0;
  virtual const UFluidSurfaceMap &Map() const = 0;
};

class UCpuFluidSurfaceMap final : public IUFluidSurfaceProvider
{
public:
  const char *BackendName() const override { return "cpu_fluid_surface"; }
  UFluidSurfaceMap &Map() override { return Surface; }
  const UFluidSurfaceMap &Map() const override { return Surface; }

private:
  UFluidSurfaceMap Surface;
};

class UGpuFluidSurfaceMap final : public IUFluidSurfaceProvider
{
public:
  UGpuFluidSurfaceMap()
  {
    // P7: PreferGpu on; pack early-out + flags bottom-walk keep wall sane.
    SetPreferGpuFluidColumnScan(true);
  }
  const char *BackendName() const override { return "gpu_fluid_surface"; }
  UFluidSurfaceMap &Map() override { return Surface; }
  const UFluidSurfaceMap &Map() const override { return Surface; }
  uint64_t GetComputeDispatchCount() const
  {
    return GpuFluidColumnScanDispatchCount();
  }

private:
  // Column scan compute (Desktop); CPU FindFluidColumnSurface remains fallback.
  UFluidSurfaceMap Surface;
};

/// Capability-driven fluid provider (no caller #ifdef).
inline std::unique_ptr<IUFluidSurfaceProvider>
CreateFluidSurfaceProvider(const RenderBackendCaps &caps)
{
  const bool want_gpu =
      !caps.ForceCpuBackends && caps.HasCompute && caps.HasSsbo &&
      (caps.Platform == RenderPlatformKind::Desktop || caps.AllowAndroidGpu);
  if (want_gpu)
  {
    return std::make_unique<UGpuFluidSurfaceMap>();
  }
  return std::make_unique<UCpuFluidSurfaceMap>();
}

} // namespace cutum
