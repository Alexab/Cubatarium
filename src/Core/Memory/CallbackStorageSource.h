#pragma once
#include <functional>
#include <cstdint>
namespace cutum {
struct CallbackStorageSnapshot {size_t requests{},capture_inline{};uintptr_t cell{},owner{};bool supported{},present{};};
template<class Capture,class R,class... Args>
inline size_t KnownCallbackRequestBytes(const std::function<R(Args...)> &) noexcept {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  using Implementation=std::_Func_impl_no_alloc<Capture,R,Args...>;
  return std::_Is_large<Implementation>?sizeof(Implementation):size_t{0};
#else
  return 0;
#endif
}
template<class Capture,class R,class... Args,class Owner>
inline CallbackStorageSnapshot InspectKnownOwnerCallback(const std::function<R(Args...)> &fn,const Owner *expected,bool owner_graph_known) noexcept {
  CallbackStorageSnapshot out;out.cell=reinterpret_cast<uintptr_t>(&fn);out.capture_inline=sizeof(Capture);out.present=bool(fn);
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=owner_graph_known;
  if(!fn)return out;
  const auto *target=fn.template target<Capture>();
  if(!target || !expected || target->owner!=expected){out.supported=false;return out;}
  out.owner=reinterpret_cast<uintptr_t>(target->owner);out.requests=KnownCallbackRequestBytes<Capture>(fn);
  const auto address=reinterpret_cast<uintptr_t>(target),cell=reinterpret_cast<uintptr_t>(&fn);
  const bool internal=address>=cell && address-cell<sizeof(fn);
  if(internal!=(out.requests==0))out.supported=false;
#endif
  return out;
}
template<class R,class... Args>
inline CallbackStorageSnapshot InspectEmptyCallback(const std::function<R(Args...)> &fn,bool history_known) noexcept {
  CallbackStorageSnapshot out;out.cell=reinterpret_cast<uintptr_t>(&fn);out.present=bool(fn);out.supported=history_known && !fn;return out;
}
} // namespace cutum
