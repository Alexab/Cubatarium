#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cutum
{
enum class GlogResourceField : size_t {Supported,DestinationBytes,FileObjectBytes,PrefixBytes,SinkVectorBytes,DirectoryVectorBytes,
  StaticInline,RetainedRequests,Destinations,SinkCount,SinkCapacity,DirectoryCount,DirectoryCapacity,PrefixPresent,CustomLoggers,
  OpenFiles,StringSlots,StringRequests,SinkArrayRequests,DirectoryArrayRequests,MaxStringRequest,ProcessStringRequests,Overflow,
  FlagCount,DestinationStringRequests,DirectoryStringRequests,ProcessStringSlots,FlagStaticInline,DestinationLimit,StringObjectBytes,Reserved,Version,Count};
using GlogResourceCensus=std::array<size_t,static_cast<size_t>(GlogResourceField::Count)>;
inline size_t GlogValue(const GlogResourceCensus &s,GlogResourceField f) noexcept {return s[static_cast<size_t>(f)];}
struct GlogResourceDomain
{
  // Additional external requested-byte envelope,including caller-pending and
  // replacement-overlap owners;exclude bytes already charged in other scopes.
  size_t sink_capacity_upper{},directory_capacity_upper{},string_request_upper{},external_owner_requests{};
  bool history_known{},external_owners_known{};
};
struct GlogResourceEnvelope
{
  size_t static_inline{},retained_upper{},configuration_peak_upper{};
  bool supported{},overflow{};
};
// Selected retained C++ resources and serial configuration operations only.
// stdio/CRT/locale,cleaner scans,VLOG/symbolizer/signal/utility state and arbitrary
// callback graphs need separate terms. Capacity histories,not live sizes,bound
// vectors and strings. String request upper already includes MSVC alignment.
inline GlogResourceEnvelope BoundGlogResources(const GlogResourceCensus &s,const GlogResourceDomain &d)
{
  GlogResourceEnvelope out;const auto q=[&](GlogResourceField f){return GlogValue(s,f);};
  out.supported=q(GlogResourceField::Supported) && q(GlogResourceField::Version)==1 && !q(GlogResourceField::Overflow) &&
    d.history_known && d.external_owners_known && q(GlogResourceField::Destinations)<=q(GlogResourceField::DestinationLimit) &&
    q(GlogResourceField::SinkCapacity)<=d.sink_capacity_upper && q(GlogResourceField::DirectoryCapacity)<=d.directory_capacity_upper &&
    q(GlogResourceField::MaxStringRequest)<=d.string_request_upper && (!q(GlogResourceField::CustomLoggers)||d.external_owner_requests>0);
  for(auto f:{GlogResourceField::DestinationBytes,GlogResourceField::PrefixBytes,GlogResourceField::SinkVectorBytes,
      GlogResourceField::DirectoryVectorBytes,GlogResourceField::DestinationLimit,GlogResourceField::StringObjectBytes})if(!q(f))out.supported=false;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){out.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto buffer=[&](size_t n){return n>=4096?add(n,39):n;};
  const auto sinks=buffer(mul(d.sink_capacity_upper,sizeof(void*)));
  const auto dirs=buffer(mul(d.directory_capacity_upper,q(GlogResourceField::StringObjectBytes)));
  auto n=mul(q(GlogResourceField::DestinationLimit),q(GlogResourceField::DestinationBytes));
  n=add(add(n,q(GlogResourceField::SinkVectorBytes)),q(GlogResourceField::DirectoryVectorBytes));
  n=add(add(add(n,q(GlogResourceField::PrefixBytes)),sinks),dirs);
  const auto strings=add(add(mul(q(GlogResourceField::DestinationLimit),3),q(GlogResourceField::ProcessStringSlots)),d.directory_capacity_upper);
  n=add(add(n,mul(strings,d.string_request_upper)),d.external_owner_requests);
  out.static_inline=q(GlogResourceField::StaticInline);out.retained_upper=n;
  auto transient=add(add(sinks,dirs),mul(2,d.string_request_upper));
  transient=add(add(transient,q(GlogResourceField::PrefixBytes)),q(GlogResourceField::DestinationBytes));
  out.configuration_peak_upper=add(n,transient);
  if(q(GlogResourceField::RetainedRequests)>n)out.supported=false;
  return out;
}
} // namespace cutum

// Configuration must be quiescent;do not call from a logging callback.
// Locks log -> sink -> file;never calls callbacks or initializes caches.
extern "C" bool CubatariumGlogResourceSource(size_t *out,size_t count) noexcept;
