#pragma once

#include "Render/Backend/RenderBackendCaps.h"
#include <memory>

namespace cutum
{

class IUChunkMesher;
class IUMeshGpuStore;
class IUChunkCull;

/// Owns bound mesher / store / cull for the session (BindOnce).
class URenderBackendBundle
{
public:
  URenderBackendBundle();
  ~URenderBackendBundle();
  URenderBackendBundle(URenderBackendBundle &&) noexcept;
  URenderBackendBundle &operator=(URenderBackendBundle &&) noexcept;

  std::unique_ptr<IUChunkMesher> Mesher;
  std::unique_ptr<IUMeshGpuStore> Store;
  std::unique_ptr<IUChunkCull> Cull;
  RenderBackendSelection Selection{};
};

class URenderBackendFactory
{
public:
  /// Select backends from caps. Idempotent: second BindOnce returns false.
  static bool BindOnce(URenderBackendBundle &bundle,
                       const RenderBackendCaps &caps);

  static RenderBackendSelection Select(const RenderBackendCaps &caps);

  static bool IsBound(const URenderBackendBundle &bundle)
  {
    return bundle.Selection.Bound;
  }
};

} // namespace cutum
