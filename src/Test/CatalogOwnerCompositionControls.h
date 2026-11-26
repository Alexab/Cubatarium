#pragma once
#include "WorldGen/Core/CatalogOwnerComposition.h"

namespace catalog_composition_fixture
{
inline cutum::CatalogOwnerSourceDomain OwnerCountProfile()
{
  cutum::CatalogOwnerSourceDomain d;
  d.resource_roots=8;d.worlds=2;d.calling_threads=1;d.pool_workers=54;d.constructing_workers=8;
  d.cooperative_workers=8;d.cooperative_scheduling_locals=1;
  d.mesh_completed_slots=96;d.relight_completed_slots=64;d.installed_fields=11;d.world_memos=2;
  d.mesh_geometry_cells=7;d.mesh_light_cells=7;d.binding_leases=13;d.capture_snapshots=9;
  d.awaiting_results=4;d.pending_gpu=3;d.no_surface_proofs=5;d.backend_leases=17;
  d.save_receipts=6;d.persistence_owners=2;d.cooperative_sessions=2;
  d.escaped_root_slots=2;d.construction_root_slots=4;
  d.thread_domain_known=d.caller_domain_known=d.source_profile_known=d.completed_capacities_positive=true;return d;
}
inline nlohmann::json OwnerControls(const cutum::RetainedCatalogEnvelope &source_root,
    const cutum::RetainedCatalogEnvelope &actual)
{
  using namespace cutum;auto d=OwnerCountProfile();const auto counts=BoundCatalogOwnerSource(d);
  bool ok=counts.supported && !counts.overflow && counts.thread_upper==63 && counts.thread_reader_slots==441 &&
    counts.pin_slots==252 && counts.shared_work_catalog_slots==32 && counts.cooperative_content_slots==100 &&
    counts.completed_catalog_slots==256 && counts.publication_catalog_slots==126 && counts.save_catalog_slots==10 && counts.catalog_root_slots==1238;
  const auto unknown=[&](int which){auto q=d;if(which==0)q.thread_domain_known=false;if(which==1)q.caller_domain_known=false;
    if(which==2)q.source_profile_known=false;if(which==3)q.completed_capacities_positive=false;return !BoundCatalogOwnerSource(q).supported;};
  for(int i=0;i<4;++i)ok=ok && unknown(i);
  auto huge=d;huge.resource_roots=SIZE_MAX;const bool add_overflow=BoundCatalogOwnerSource(huge).overflow;
  huge=d;huge.cooperative_workers=SIZE_MAX;const bool mul_overflow=BoundCatalogOwnerSource(huge).overflow;
  huge=d;huge.constructing_workers=SIZE_MAX;const bool thread_overflow=BoundCatalogOwnerSource(huge).overflow;
  auto too_large=source_root;too_large.requested_heap_upper=SIZE_MAX;
  const bool byte_overflow=ComposeCatalogOwnerSource(counts,too_large,actual,0).overflow;
  auto absent=source_root;absent.requested_heap_upper=0;
  const bool omitted_budget=!ComposeCatalogOwnerSource(counts,absent,actual,0).supported;
  const auto valid=ComposeCatalogOwnerSource(counts,source_root,actual,0);
  ok=ok && add_overflow && mul_overflow && thread_overflow && byte_overflow && omitted_budget && valid.supported && !valid.overflow;
  return {{"pass",ok},{"thread_upper",counts.thread_upper},{"reader_slots",counts.thread_reader_slots},{"pin_slots",counts.pin_slots},
    {"shared_work_slots",counts.shared_work_catalog_slots},{"cooperative_content_slots",counts.cooperative_content_slots},
    {"completed_catalog_slots",counts.completed_catalog_slots},{"publication_catalog_slots",counts.publication_catalog_slots},
    {"save_catalog_slots",counts.save_catalog_slots},{"root_slots_upper",counts.catalog_root_slots},
    {"unknown_thread_caller_source_and_zero_capacity_rejected",unknown(0)&&unknown(1)&&unknown(2)&&unknown(3)},
    {"add_multiply_thread_byte_overflow_rejected",add_overflow&&mul_overflow&&thread_overflow&&byte_overflow},
    {"missing_source_budget_rejected",omitted_budget},{"supplied_root_source_upper",source_root.requested_heap_upper},
    {"count_weighted_retained_upper",valid.requested_heap_upper},
    {"scope","Conservative source count profile for two supplied worlds,54 member+8 constructing pool workers,explicit artifact/caller counts;not observed live thread count or process budget"}};
}
} // namespace catalog_composition_fixture
