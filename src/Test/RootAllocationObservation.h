#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
// Separate executable observation: no production allocator replacement,owner
// mirror or live ledger is added to the application. Constructor child blocks
// are recorded independently so the selected root is found by containment.
namespace root_observation {
struct Record {uintptr_t address{};size_t bytes{},epoch{};};
inline std::array<Record,16384> Records;
struct Counts {size_t epoch{},live{},blocks{},peak{},attempts{},refusals{},fail_after{SIZE_MAX};bool active{};};
inline Counts State;
struct alignas(std::max_align_t) Header {size_t bytes,epoch,slot;};
constexpr size_t Prefix=(sizeof(Header)+15)/16*16;
inline const Record *Holding(const void *object,size_t extent) noexcept {
  const auto address=reinterpret_cast<uintptr_t>(object);
  for(const auto &r:Records)if(r.address && r.epoch==State.epoch && address>=r.address && address-r.address<=r.bytes && extent<=r.bytes-(address-r.address))return &r;
  return nullptr;
}
inline void *Allocate(size_t bytes){
  if(bytes>SIZE_MAX-Prefix)throw std::bad_alloc{};
  const auto epoch=State.active?State.epoch:0;size_t slot=SIZE_MAX;
  if(epoch){if(State.attempts++>=State.fail_after){++State.refusals;throw std::bad_alloc{};}
    for(size_t i=0;i<Records.size();++i)if(!Records[i].address){slot=i;break;}if(slot==SIZE_MAX)std::abort();}
  auto *memory=static_cast<unsigned char*>(std::malloc(Prefix+std::max(bytes,size_t{1})));if(!memory)throw std::bad_alloc{};
  ::new(memory) Header{bytes,epoch,slot};auto *result=memory+Prefix;
  if(epoch){Records[slot]={reinterpret_cast<uintptr_t>(result),bytes,epoch};State.live+=bytes;++State.blocks;State.peak=std::max(State.peak,State.live);}
  return result;
}
inline void Free(void *pointer) noexcept {
  if(!pointer)return;auto *memory=static_cast<unsigned char*>(pointer)-Prefix;const auto h=*reinterpret_cast<const Header*>(memory);
  if(h.epoch && h.epoch==State.epoch){if(h.slot>=Records.size() || Records[h.slot].address!=reinterpret_cast<uintptr_t>(pointer))std::abort();Records[h.slot]={};State.live-=h.bytes;--State.blocks;}
  std::free(memory);
}
struct Scope {
  explicit Scope(size_t failure=SIZE_MAX){if(State.active || State.live || State.blocks)std::abort();const auto epoch=State.epoch+1;State={};State.epoch=epoch;State.fail_after=failure;State.active=true;}
  ~Scope(){State.active=false;}
};
} // namespace root_observation
void *operator new(size_t n){return root_observation::Allocate(n);}
void *operator new[](size_t n){return root_observation::Allocate(n);}
void operator delete(void *p)noexcept{root_observation::Free(p);}
void operator delete[](void *p)noexcept{root_observation::Free(p);}
void operator delete(void *p,size_t)noexcept{root_observation::Free(p);}
void operator delete[](void *p,size_t)noexcept{root_observation::Free(p);}
void *operator new(size_t n,const std::nothrow_t&)noexcept{try{return root_observation::Allocate(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t&)noexcept{try{return root_observation::Allocate(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t&)noexcept{root_observation::Free(p);}
void operator delete[](void *p,const std::nothrow_t&)noexcept{root_observation::Free(p);}
