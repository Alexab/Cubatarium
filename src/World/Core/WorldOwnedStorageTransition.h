#pragma once
#include "World/Core/WorldOwnedStorage.h"
namespace cutum {
// Stable resolved resources and an actual supplied immutable content pin are
// required. Filesystem/catalog construction belongs to its E11 source profile.
inline PipelineStorageEnvelope BoundWorldOwnedStorageTransition(const WorldOwnedStorageSnapshot &before,const PipelineTransitionDomain &input,bool pinned_factory_history_known) noexcept {
  auto out=BoundPipelineStorageTransition(before.generation,input);
  out.supported=out.supported && before.supported && !before.overflow && before.borrowed_context_matches && pinned_factory_history_known && !input.new_world;
  if(out.overflow)return out;
  // The main-world path does not reset the scratch world's metadata tables.
  const auto scratch_reset=before.layout.generation.widths[8];
  if(out.additional_requests<scratch_reset){out.supported=false;return out;}
  out.additional_requests-=scratch_reset;
  // Its outer WorldGenContext remains live while the by-value factory context
  // and generator/sample copies (already in W10) are built.
  PipelineStorageAdd(out.additional_requests,input.fresh_settings_text_upper,out);
  PipelineStorageAdd(out.additional_requests,input.fresh_owner_text_upper,out);
  for(int i=0;i<4;++i)PipelineStorageAdd(out.additional_requests,before.layout.generation.stl_large_slack,out);
  PipelineStorageAdd(out.additional_requests,before.lighting_requests,out);
  const auto &light=before.layout.lighting;
  PipelineStorageAdd(out.additional_requests,std::max({light[0],light[1],light[2]}),out);
  for(size_t i=1;i<before.callbacks.size();++i)PipelineStorageAdd(out.additional_requests,before.callbacks[i].requests,out);
  return out;
}
} // namespace cutum
