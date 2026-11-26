#pragma once
#include "WorldGen/Core/CatalogConstructionComposition.h"

namespace cutum
{
// Source-profile cardinalities,not imposed process quotas. All supplied live
// worlds/pools,constructing components,escaped histories and caller threads
// must be covered. A zero capacity is lossless,so it is never a finite bound.
struct CatalogOwnerSourceDomain
{
  size_t resource_roots{},worlds{},calling_threads{1},pool_workers{},constructing_workers{};
  size_t shared_work_slots{8},cooperative_workers{},cooperative_scheduling_locals{};
  size_t mesh_completed_slots{},relight_completed_slots{},installed_fields{},world_memos{};
  size_t mesh_geometry_cells{},mesh_light_cells{},binding_leases{},capture_snapshots{};
  size_t awaiting_results{},pending_gpu{},no_surface_proofs{},backend_leases{};
  size_t save_receipts{},persistence_owners{},cooperative_sessions{},escaped_root_slots{},construction_root_slots{};
  bool thread_domain_known{},caller_domain_known{},source_profile_known{},completed_capacities_positive{};
};
struct CatalogOwnerSourceEnvelope
{
  size_t thread_upper{},thread_reader_slots{},pin_slots{},shared_work_catalog_slots{},cooperative_content_slots{};
  size_t completed_catalog_slots{},publication_catalog_slots{},save_catalog_slots{},catalog_root_slots{};
  bool supported{},overflow{};
};
inline CatalogOwnerSourceEnvelope BoundCatalogOwnerSource(const CatalogOwnerSourceDomain &d)
{
  CatalogOwnerSourceEnvelope out;
  out.supported=d.thread_domain_known && d.caller_domain_known && d.source_profile_known && d.completed_capacities_positive;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  out.thread_upper=add(add(d.calling_threads,d.pool_workers),d.constructing_workers);
  out.thread_reader_slots=mul(out.thread_upper,7);out.pin_slots=mul(out.thread_upper,4);
  // Generation captures4 content handles;mesh<=2 catalog publications and
  // relight1. Four per shared slot is conservative across the entire union.
  out.shared_work_catalog_slots=mul(d.shared_work_slots,4);
  // Results can be drained before the worker destroys the published callable.
  // Logical InFlight2W plus returning callables<=W,each with4 content handles.
  out.cooperative_content_slots=mul(add(mul(d.cooperative_workers,3),d.cooperative_scheduling_locals),4);
  out.completed_catalog_slots=add(mul(d.mesh_completed_slots,2),d.relight_completed_slots);
  auto pubs=add(mul(d.installed_fields,2),d.world_memos);
  for(auto n:{d.mesh_geometry_cells,d.mesh_light_cells,mul(d.binding_leases,2),d.capture_snapshots,
      mul(d.awaiting_results,2),mul(d.pending_gpu,2),d.no_surface_proofs,mul(d.backend_leases,2)})pubs=add(pubs,n);
  out.publication_catalog_slots=pubs;
  out.save_catalog_slots=add(add(d.save_receipts,d.persistence_owners),d.cooperative_sessions);
  size_t slots=3; // Active Pack,Features,Refs;Biomes aliases Pack-owned registry.
  for(auto n:{d.resource_roots,mul(d.worlds,2),out.thread_reader_slots,out.pin_slots,
      out.shared_work_catalog_slots,out.cooperative_content_slots,out.completed_catalog_slots,
      out.publication_catalog_slots,out.save_catalog_slots,d.escaped_root_slots,d.construction_root_slots})slots=add(slots,n);
  out.catalog_root_slots=slots;return out;
}
// A per-root transitive retained envelope must come from an accepted source/
// retained input profile spanning every admitted live version,not merely the
// current catalog or sampled RSS. Unknown history cannot be silently zero.
inline CatalogCandidateEnvelope ComposeCatalogOwnerSource(const CatalogOwnerSourceEnvelope &owners,
    const RetainedCatalogEnvelope &per_root,const RetainedCatalogEnvelope &actual_union,
    size_t new_source_requests,bool new_source_supported=true,bool new_source_overflow=false)
{
  CatalogCandidateEnvelope out;
  out.supported=owners.supported && per_root.supported && actual_union.supported && new_source_supported;
  out.overflow=owners.overflow || per_root.overflow || actual_union.overflow || new_source_overflow;
  if(owners.catalog_root_slots && per_root.requested_heap_upper>SIZE_MAX/owners.catalog_root_slots){out.overflow=true;out.requested_heap_upper=SIZE_MAX;}
  else out.requested_heap_upper=owners.catalog_root_slots*per_root.requested_heap_upper;
  if(actual_union.requested_heap_upper>out.requested_heap_upper)out.supported=false;
  detail::CandidateAdd(out.requested_heap_upper,new_source_requests,out.overflow);return out;
}
} // namespace cutum
