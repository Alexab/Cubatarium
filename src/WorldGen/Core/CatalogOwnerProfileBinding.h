#pragma once
#include "WorldGen/Core/CatalogOwnerComposition.h"
#include "WorldGen/Core/SourceInputStorageCensus.h"

namespace cutum
{
// These rows are the existing C06 owner ledger,not a runtime discovery list.
enum class CatalogOwnerRow {G01,G02,G03,G04,T01,T02,T03,T04,RQ01,RQ02,RQ03,R01,R02,R03,R04,R05,R06,P01,Count};
enum class CatalogInputFamily {PackRegistry,FeaturesRefs,BlockMerge,PrefabLibrary,WorldInputs,
  ManagedFiles,CpuTexture,GpuPublication,RuntimeText,Count};
struct CatalogRuntimeTextSourceCounts {size_t copies{},name_threads{};bool overflow{};};
inline CatalogRuntimeTextSourceCounts BoundCatalogRuntimeTextCopies(const CatalogOwnerSourceDomain &d)
{
  CatalogRuntimeTextSourceCounts out;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  out.name_threads=add(add(d.calling_threads,d.pool_workers),d.constructing_workers);
  // Per world:template,primary context,main snapshot and populator owner labels.
  // Per calling/worker thread:retained context plus local/settings/factory/
  // refresh/sample copy overlap. Sampling copies strip selectors;TLS key is
  // numeric. Counting six full bundles is conservative for those source sites.
  // Shared/cooperative callables and constructor/escaped source holders stay
  // explicit,including callable publication tails before destruction.
  out.copies=add(mul(d.worlds,4),mul(out.name_threads,6));
  out.copies=add(out.copies,mul(d.shared_work_slots,2));
  out.copies=add(out.copies,mul(add(mul(d.cooperative_workers,3),d.cooperative_scheduling_locals),2));
  out.copies=add(add(out.copies,d.construction_root_slots),d.escaped_root_slots);return out;
}
struct CatalogOwnerProfileBinding
{
  static constexpr uint64_t AllRows=(uint64_t{1}<<static_cast<size_t>(CatalogOwnerRow::Count))-1;
  static constexpr uint64_t AllFamilies=(uint64_t{1}<<static_cast<size_t>(CatalogInputFamily::Count))-1;
  uint64_t covered_rows{},supplied_families{};
  std::array<RetainedCatalogEnvelope,static_cast<size_t>(CatalogInputFamily::Count)> families{};
  // Supplied source-profile copy maximum,including persistent primary context,
  // queued settings,active local/context copies and externally retained values.
  // TLS numeric settings/owner keys add no source text;Populate and samples
  // already retire/neutralize their text. Arbitrary spare-capacity histories
  // require a declared capacity envelope;current string length is insufficient.
  size_t runtime_text_copies{},registry_name_threads{};
  bool runtime_history_known{};
  void Cover(CatalogOwnerRow row){covered_rows|=uint64_t{1}<<static_cast<size_t>(row);}
  void Supply(CatalogInputFamily family,RetainedCatalogEnvelope source){
    families[static_cast<size_t>(family)]=source;supplied_families|=uint64_t{1}<<static_cast<size_t>(family);
  }
};
// Plain settings/DOM/path roots use their existing address-deduplicated census.
// A managed document's retirement arena is not in plain DOM backing and must
// accompany it. Caller ensures these inputs are not also catalog-owned values.
// Decoder request prefixes already belong to catalog entry;never add a process
// or scoped decoder ledger's mirrored live bytes as another retained owner.
inline RetainedCatalogEnvelope JoinCatalogOwnerInputs(const RetainedCatalogEnvelope &catalogs,
    const SourceInputStorageCensus &inputs,size_t managed_arena_requests=0,bool arena_profile_known=true)
{
  auto out=catalogs;out.supported=out.supported && inputs.storage.unordered_model_supported && arena_profile_known;
  out.overflow=out.overflow || inputs.overflow || inputs.storage.overflow;
  detail::CandidateAdd(out.requested_heap_upper,inputs.RequestedHeapComponents(),out.overflow);
  detail::CandidateAdd(out.requested_heap_upper,managed_arena_requests,out.overflow);return out;
}
inline CatalogCandidateEnvelope BindCatalogOwnerProfiles(const CatalogOwnerSourceEnvelope &counts,
    const CatalogOwnerProfileBinding &binding,const RetainedCatalogEnvelope &actual_union,
    size_t new_source_requests,bool new_source_supported=true,bool new_source_overflow=false)
{
  RetainedCatalogEnvelope maximum;maximum.supported=true;
  bool complete=binding.covered_rows==CatalogOwnerProfileBinding::AllRows &&
    binding.supplied_families==CatalogOwnerProfileBinding::AllFamilies && binding.runtime_history_known;
  for(const auto &source:binding.families){maximum.supported=maximum.supported && source.supported;
    maximum.overflow=maximum.overflow || source.overflow;maximum.requested_heap_upper=std::max(maximum.requested_heap_upper,source.requested_heap_upper);}
  // A uniform transitive per-version maximum is intentionally conservative.
  // It must span all admitted live input versions in every named family,not
  // just current publications or sampled RSS. Unknown families do not mean0.
  auto flags=actual_union;flags.requested_heap_upper=0;
  auto out=ComposeCatalogOwnerSource(counts,maximum,flags,0,
      new_source_supported && complete,new_source_overflow);
  auto copies=binding.runtime_text_copies;detail::CandidateAdd(copies,binding.registry_name_threads,out.overflow);
  const auto text=binding.families[static_cast<size_t>(CatalogInputFamily::RuntimeText)].requested_heap_upper;
  if(copies && text>SIZE_MAX/copies){out.overflow=true;out.requested_heap_upper=SIZE_MAX;}
  else detail::CandidateAdd(out.requested_heap_upper,copies*text,out.overflow);
  if(actual_union.requested_heap_upper>out.requested_heap_upper)out.supported=false;
  detail::CandidateAdd(out.requested_heap_upper,new_source_requests,out.overflow);
  return out;
}
} // namespace cutum
