#pragma once

#include "Render/Engine/FluidSurfaceMap.h"

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
  const char *BackendName() const override { return "gpu_fluid_surface"; }
  UFluidSurfaceMap &Map() override { return Surface; }
  const UFluidSurfaceMap &Map() const override { return Surface; }

private:
  // Compute scan later; CPU map used for parity until then.
  UFluidSurfaceMap Surface;
};

} // namespace cutum
