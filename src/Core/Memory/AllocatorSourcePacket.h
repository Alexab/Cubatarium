#pragma once
#include <array>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace cutum {
inline constexpr size_t AllocatorSourceKinds=11;
struct AllocatorSourceNode {
  uintptr_t object{},origin{};size_t kind{},bytes{};bool covered{};
  bool operator==(const AllocatorSourceNode &b)const{return object==b.object && origin==b.origin && kind==b.kind && bytes==b.bytes && covered==b.covered;}
};
struct AllocatorSourcePhase {
  const uint64_t epoch;const size_t participants;
  explicit AllocatorSourcePhase(uint64_t id,size_t expected):epoch(id),participants(expected){}
  bool IsOpen()const{return Open.load(std::memory_order_acquire);}
  void Close(){Open.store(false,std::memory_order_release);}
private:std::atomic<bool> Open{true};
};
struct AllocatorSourcePacket {
  uint64_t epoch{};size_t participant{};std::thread::id thread;
  std::array<size_t,AllocatorSourceKinds> widths{};
  std::vector<AllocatorSourceNode> nodes;
  bool supported{};
  explicit AllocatorSourcePacket(size_t reserve=0){nodes.reserve(reserve);}
};
struct AllocatorSourceUnion {
  std::array<size_t,AllocatorSourceKinds> blocks{},covered{};
  size_t additional_requests{},covered_requests{},participants{},aliases{};
  bool supported{},overflow{};
};
// Packets contain plain identity tokens,no owners. All producers must keep
// their source graphs stable in this same open phase until joining finishes.
// Epoch IDs must be unique across source phases. Never join after release.
inline AllocatorSourceUnion JoinAllocatorSourcePackets(const AllocatorSourcePhase &phase,
    const std::vector<const AllocatorSourcePacket*> &packets,bool complete_domain){
  AllocatorSourceUnion out;out.supported=complete_domain && phase.IsOpen() && phase.epoch && phase.participants;
  std::vector<const AllocatorSourcePacket*> participants;std::vector<AllocatorSourceNode> nodes;
  std::array<size_t,AllocatorSourceKinds> widths{};
  for(const auto *packet:packets){
    if(!packet || !packet->supported || packet->epoch!=phase.epoch || packet->thread==std::thread::id{}){out.supported=false;continue;}
    if(participants.empty())widths=packet->widths;
    if(widths!=packet->widths){out.supported=false;continue;}
    bool repeated=false;
    for(const auto *prior:participants){
      if(prior->participant==packet->participant){repeated=true;if(prior->thread!=packet->thread || prior->nodes!=packet->nodes)out.supported=false;}
      else if(prior->thread==packet->thread)out.supported=false;
    }
    if(repeated)continue;participants.push_back(packet);
    for(const auto &node:packet->nodes){
      if(!node.object || node.kind>=widths.size() || !widths[node.kind] || node.bytes!=widths[node.kind]){out.supported=false;continue;}
      bool seen=false;
      for(auto &prior:nodes)if(prior.object==node.object){
        seen=true;++out.aliases;
        if(prior.kind!=node.kind || prior.bytes!=node.bytes || prior.origin!=node.origin)out.supported=false;
        prior.covered=prior.covered || node.covered;break;
      }
      if(!seen)nodes.push_back(node);
    }
  }
  out.participants=participants.size();if(out.participants!=phase.participants)out.supported=false;
  const auto add=[&](size_t &a,size_t b){if(b>SIZE_MAX-a){a=SIZE_MAX;out.overflow=true;out.supported=false;}else a+=b;};
  for(const auto &node:nodes){add(out.blocks[node.kind],1);
    if(node.covered){add(out.covered[node.kind],1);add(out.covered_requests,node.bytes);}else add(out.additional_requests,node.bytes);}
  if(!phase.IsOpen())out.supported=false;return out;
}
}
