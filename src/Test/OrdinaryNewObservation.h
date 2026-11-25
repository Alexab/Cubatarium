#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <new>
// Isolated single-thread fixture: actual production resolver, ordinary new
// requests only. Inputs/report allocations are outside each observed epoch.
namespace observation
{
struct Counts {size_t epoch{},live{},blocks{},peak{},attempts{},refusals{},max_request{},fail_after{SIZE_MAX},max_single{SIZE_MAX};bool active{};};
inline Counts State;
struct alignas(std::max_align_t) Header {size_t bytes,epoch;};
constexpr size_t Prefix=(sizeof(Header)+15)/16*16;
void *Allocate(size_t bytes) {
  if(bytes>SIZE_MAX-Prefix) throw std::bad_alloc{};
  const size_t epoch=State.active?State.epoch:0;
  if(epoch) {
    State.max_request=std::max(State.max_request,bytes);
    const auto index=State.attempts++;
    if(index>=State.fail_after || bytes>State.max_single) {++State.refusals;throw std::bad_alloc{};}
  }
  auto *memory=static_cast<unsigned char *>(std::malloc(Prefix+std::max(bytes,size_t{1})));
  if(!memory) throw std::bad_alloc{};
  ::new(memory) Header{bytes,epoch};
  if(epoch) {State.live+=bytes;++State.blocks;State.peak=std::max(State.peak,State.live);}
  return memory+Prefix;
}
void Free(void *pointer) noexcept {
  if(!pointer) return;
  auto *memory=static_cast<unsigned char *>(pointer)-Prefix;
  const auto h=*reinterpret_cast<const Header *>(memory);std::free(memory);
  if(h.epoch && h.epoch==State.epoch) {State.live-=h.bytes;--State.blocks;}
}
struct Scope {
  Scope(size_t failure=SIZE_MAX,size_t maxSingle=SIZE_MAX) {
    if(State.active || State.live || State.blocks) std::abort();
    const size_t epoch=State.epoch+1;State={};State.epoch=epoch;State.active=true;
    State.fail_after=failure;State.max_single=maxSingle;
  }
  ~Scope(){State.active=false;}
};
}
void *operator new(size_t n){return observation::Allocate(n);}
void *operator new[](size_t n){return observation::Allocate(n);}
void operator delete(void *p) noexcept {observation::Free(p);}
void operator delete[](void *p) noexcept {observation::Free(p);}
void operator delete(void *p,size_t) noexcept {observation::Free(p);}
void operator delete[](void *p,size_t) noexcept {observation::Free(p);}
void *operator new(size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t &) noexcept {observation::Free(p);}
void operator delete[](void *p,const std::nothrow_t &) noexcept {observation::Free(p);}
