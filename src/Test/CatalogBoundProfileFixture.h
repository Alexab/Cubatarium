#pragma once
#include "WorldGen/Core/CatalogOwnerProfileBinding.h"
#include "Test/CatalogOwnerCompositionControls.h"

namespace catalog_bound_profile_fixture
{
using namespace cutum;using Json=nlohmann::json;
// Selected actual operation graph:entry plus all admitted new source requests
// bounds every version introduced by that finite input schedule. Unselected
// application owners are not claimed observed here. The same binder requires
// explicit complete source maxima when used for a whole application profile.
struct Profile
{
  RetainedCatalogEnvelope versions;
  CatalogOwnerSourceEnvelope counts=BoundCatalogOwnerSource(catalog_composition_fixture::OwnerCountProfile());
  explicit Profile(RetainedCatalogEnvelope entry):versions(entry){}
  CatalogOwnerProfileBinding Binding()const {
    CatalogOwnerProfileBinding out;
    for(auto row:{CatalogOwnerRow::G01,CatalogOwnerRow::G02,CatalogOwnerRow::G03,CatalogOwnerRow::G04,
        CatalogOwnerRow::T01,CatalogOwnerRow::T02,CatalogOwnerRow::T03,CatalogOwnerRow::T04,
        CatalogOwnerRow::RQ01,CatalogOwnerRow::RQ02,CatalogOwnerRow::RQ03,
        CatalogOwnerRow::R01,CatalogOwnerRow::R02,CatalogOwnerRow::R03,CatalogOwnerRow::R04,CatalogOwnerRow::R05,CatalogOwnerRow::R06,CatalogOwnerRow::P01})out.Cover(row);
    for(auto family:{CatalogInputFamily::PackRegistry,CatalogInputFamily::FeaturesRefs,CatalogInputFamily::BlockMerge,
        CatalogInputFamily::PrefabLibrary,CatalogInputFamily::WorldInputs,CatalogInputFamily::ManagedFiles,
        CatalogInputFamily::CpuTexture,CatalogInputFamily::GpuPublication,CatalogInputFamily::RuntimeText})out.Supply(family,versions);
    // Capacity upper is supplied by this finite admitted input history,not
    // measured thread behavior. Copy counts include declared caller/constructor
    // overlap;the source mapping is in the final C06 binding certificate.
    const auto texts=BoundCatalogRuntimeTextCopies(catalog_composition_fixture::OwnerCountProfile());
    out.runtime_text_copies=texts.copies;out.registry_name_threads=texts.name_threads;out.runtime_history_known=!texts.overflow;return out;
  }
  CatalogCandidateEnvelope Phase(RetainedCatalogEnvelope actual,size_t added,bool supported,bool overflow){
    detail::CandidateAdd(versions.requested_heap_upper,added,versions.overflow);
    versions.supported=versions.supported && supported;versions.overflow=versions.overflow || overflow;
    return BindCatalogOwnerProfiles(counts,Binding(),actual,added,supported,overflow);
  }
  Json Controls(RetainedCatalogEnvelope actual)const {
    const auto full=Binding();const auto valid=BindCatalogOwnerProfiles(counts,full,actual,0);
    const auto with_weak=JoinCatalogPipelineWeakShells(valid,counts,true);
    const auto weak=BoundPipelineWeakShells(counts.thread_upper,counts.supported,true);
    auto row=full;row.covered_rows&=~(uint64_t{1}<<static_cast<size_t>(CatalogOwnerRow::P01));
    auto family=full;family.supplied_families&=~(uint64_t{1}<<static_cast<size_t>(CatalogInputFamily::ManagedFiles));
    auto history=full;history.runtime_history_known=false;
    auto huge=full;huge.runtime_text_copies=SIZE_MAX;huge.registry_name_threads=SIZE_MAX;
    auto unsupported=full;unsupported.families[0].supported=false;
    auto hugeDomain=catalog_composition_fixture::OwnerCountProfile();hugeDomain.calling_threads=SIZE_MAX;
    const bool sourceTextOverflow=BoundCatalogRuntimeTextCopies(hugeDomain).overflow;
    const bool controls=valid.supported && !valid.overflow && !BindCatalogOwnerProfiles(counts,row,actual,0).supported &&
      !BindCatalogOwnerProfiles(counts,family,actual,0).supported && !BindCatalogOwnerProfiles(counts,history,actual,0).supported &&
      !BindCatalogOwnerProfiles(counts,unsupported,actual,0).supported && BindCatalogOwnerProfiles(counts,huge,actual,0).overflow &&
      sourceTextOverflow && full.runtime_text_copies==458 && full.registry_name_threads==63 &&
      with_weak.supported && !with_weak.overflow && with_weak.requested_heap_upper==valid.requested_heap_upper+weak.requested_upper &&
      !JoinCatalogPipelineWeakShells(valid,counts,false).supported;
    return {{"pass",controls},{"covered_owner_rows",full.covered_rows},{"supplied_input_families",full.supplied_families},
      {"root_count_upper",counts.catalog_root_slots},{"runtime_text_copies",full.runtime_text_copies},{"registry_name_threads",full.registry_name_threads},
      {"per_version_source_history_upper",versions.requested_heap_upper},{"retained_bound",valid.requested_heap_upper},
      {"pipeline_expired_weak_source_upper",weak.requested_upper},{"retained_plus_expired_weak_bound",with_weak.requested_heap_upper},
      {"missing_row_family_history_and_unsupported_model_rejected",controls},{"text_count_byte_overflow_rejected",BindCatalogOwnerProfiles(counts,huge,actual,0).overflow},{"runtime_source_copy_count_overflow_rejected",sourceTextOverflow}};
  }
};
} // namespace catalog_bound_profile_fixture
