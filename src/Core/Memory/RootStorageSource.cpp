#include "Core/Memory/RootStorageSource.h"
#include "World/Core/World.h"
#include "App/Core.h"
#include "App/Application.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Lighting/SectionRenderLightBindings.h"
namespace cutum {
RootStorageLayout GetRootStorageLayout() noexcept {
  RootStorageLayout out;
  out.objects={sizeof(UWorld),sizeof(UCore),sizeof(UApplication),sizeof(UGeometryEngine),sizeof(SectionRenderLightBindings)};
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 && _MSVC_STL_UPDATE == 202604L && _ITERATOR_DEBUG_LEVEL == 0
  out.coallocated={sizeof(std::_Ref_count_obj2<UWorld>),sizeof(std::_Ref_count_obj2<UCore>),sizeof(std::_Ref_count_obj2<UApplication>),sizeof(std::_Ref_count_obj2<UGeometryEngine>),sizeof(std::_Ref_count_obj2<SectionRenderLightBindings>)};
  out.supported=true;
#endif
  return out;
}
} // namespace cutum
