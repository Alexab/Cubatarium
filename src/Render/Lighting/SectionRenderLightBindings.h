#pragma once

#include "Render/Lighting/SectionRenderLightBinding.h"
#include <memory>

namespace cutum
{
/// Physical-proof gate and lifetime owner for the GPU binding table. All
/// operations, including final lease release, run on the render context thread.
class SectionRenderLightBindings
{
public:
  SectionRenderLightBindings() = default;
  ~SectionRenderLightBindings();
  SectionRenderLightBindings(const SectionRenderLightBindings &) = delete;
  SectionRenderLightBindings &
  operator=(const SectionRenderLightBindings &) = delete;
  /// A world transition follows the previous world's last draw. Reset also
  /// invalidates retained CPU handles, even when the logical epoch is reused.
  bool BeginWorld(uint64_t world_epoch,
                  std::shared_ptr<const uint64_t> world_geometry_identity,
                  uint32_t binding_limit = 4096,
                  size_t atlas_bytes = 32u * 1024u * 1024u,
                  uint32_t page_limit = 4096,
                  bool enable_ordered_fallback = false);
  void Reset();
  std::shared_ptr<SectionRenderLightBinding>
  Prepare(uint64_t source_world_epoch,
          const SectionGeometryRenderInputs &geometry,
          const SectionRenderLightSnapshot &light);
  /// A field-only replacement is allowed only for identical geometry inputs.
  /// This updates shared metadata/texture entries, never vertex buffers.
  bool Refresh(const std::shared_ptr<SectionRenderLightBinding> &binding,
               uint64_t source_world_epoch,
               const SectionRenderLightSnapshot &light);
  bool
  IsResident(const std::shared_ptr<SectionRenderLightBinding> &binding) const;
  /// Resident handles may be retained in draw metadata. A fallback result is
  /// transient: submit this binding's draw before staging a different binding.
  /// Always use this operation before direct fallback, never cache its result.
  std::optional<std::array<uint32_t, 4>>
  PrepareDraw(const std::shared_ptr<SectionRenderLightBinding> &binding);
  bool Bind(unsigned atlas_unit, unsigned table_unit) const;
  std::array<uint32_t, 2> ShaderIncarnation() const;
  void CollectRetired();

private:
  struct State;
  struct Entry;
  std::shared_ptr<State> Current;
};
} // namespace cutum
