#pragma once
#include <memory>
#include <cstdint>
namespace cutum {
class SectionRenderLightBindings;class SectionRenderLightBinding;
struct SectionGeometryRenderInputs;class SectionRenderLightSnapshot;
struct RenderPrepareLightCallback {
  std::weak_ptr<SectionRenderLightBindings> owner;
  std::shared_ptr<SectionRenderLightBinding> operator()(uint64_t epoch,const SectionGeometryRenderInputs &geometry,const SectionRenderLightSnapshot &light)const;
};
} // namespace cutum
