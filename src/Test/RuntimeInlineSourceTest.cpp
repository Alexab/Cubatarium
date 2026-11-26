#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "WorldGen/Core/RuntimeCoreInlineLayout.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "Core/Memory/RuntimeSourceEnvelope.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

int main(int argc,char **argv)
{
  using namespace cutum;using Json=nlohmann::json;
  auto stbi=std::make_shared<UStbiAllocationLedger>();
  auto shared=std::make_shared<UConcurrentAllocationCountingResource>();
  auto budget=std::make_shared<UBufferAllocationBudget>(1024);auto job=std::make_shared<BufferBudgetJob>();
  UStbiAllocationScope stbi_scope(stbi);UScopedSharedAllocationResource shared_scope(shared);
  UBufferBudgetScope budget_scope({budget,job});
  WorldGenContentSnapshot content;UWorldGenContentPinScope pin_scope(content);
  const auto *pin=GetPinnedWorldGenContent();
  const auto before_stbi=detail::StbiProcessLedger.GetSnapshot();const auto before_json=GetCatalogJsonSourceSnapshot();
  const auto stbi_owners=stbi.use_count(),shared_owners=shared.use_count(),budget_owners=budget.use_count(),job_owners=job.use_count();
  RuntimeInlineLayout layout;PipelineRuntimeInlineLayout pipeline;
  {observation::Scope scope(0);layout=GetRuntimeCoreInlineLayout();pipeline=GetPipelineRuntimeInlineLayout();}
  const bool allocation_free=observation::State.attempts==0 && observation::State.live==0 && observation::State.blocks==0;
  const auto after_stbi=detail::StbiProcessLedger.GetSnapshot();const auto after_json=GetCatalogJsonSourceSnapshot();
  const bool unchanged=pin==GetPinnedWorldGenContent() && stbi.use_count()==stbi_owners && shared.use_count()==shared_owners &&
    budget.use_count()==budget_owners && job.use_count()==job_owners && before_stbi.allocation_attempts==after_stbi.allocation_attempts &&
    before_stbi.live_request_bytes==after_stbi.live_request_bytes && before_json.completed==after_json.completed && before_json.attempts==after_json.attempts;
  RuntimeInlineDomain domain;domain.os_threads=8;domain.thread_domain_known=domain.coverage_known=domain.base_known=true;
  const auto reader=static_cast<size_t>(RuntimeInlineCell::Readers),parent=static_cast<size_t>(RuntimeInlineCell::Pipeline);
  domain.already_counted[reader]=1; // caller reader shell already in supplied E11 census
  const auto valid=ComposeRuntimeInline(layout,domain,61403,243016); // W02 explicit8-thread source example
  auto no_coverage=domain;no_coverage.coverage_known=false;
  auto no_thread=domain;no_thread.thread_domain_known=false;
  auto no_base=domain;no_base.base_known=false;
  auto extra=domain;extra.already_counted[reader]=9;
  auto missing=layout;missing.present&=~(uint64_t{1}<<parent);
  auto zero=layout;zero.widths[parent]=0;
  auto unsupported=layout;unsupported.supported=false;
  auto huge=domain;huge.os_threads=SIZE_MAX;
  auto all_readers=domain;all_readers.already_counted[reader]=8;
  auto no_readers=domain;no_readers.already_counted[reader]=0;
  const auto eight=ComposeRuntimeInline(layout,all_readers,61403,243016),none=ComposeRuntimeInline(layout,no_readers,61403,243016);
  auto all_parent=domain;all_parent.already_counted[parent]=8;
  const auto parent_covered=ComposeRuntimeInline(layout,all_parent,61403,243016);
  size_t child_sum=0;for(auto n:pipeline.children)child_sum+=n;
  const bool whole_once=pipeline.whole==layout.widths[parent] && child_sum<=pipeline.whole && pipeline.children[0]==332 &&
    pipeline.children[1]==104 && pipeline.children[2]==8 && parent_covered.additional_tls+8*pipeline.whole==valid.additional_tls;
  const bool reader_once=layout.widths[reader]==160 && none.additional_tls==valid.additional_tls+160 &&
    eight.additional_tls+7*160==valid.additional_tls;
  const bool controls=!ComposeRuntimeInline(layout,no_coverage,61403,243016).supported && !ComposeRuntimeInline(layout,no_thread,61403,243016).supported &&
    !ComposeRuntimeInline(layout,no_base,0,0).supported && !ComposeRuntimeInline(layout,extra,61403,243016).supported &&
    !ComposeRuntimeInline(missing,domain,61403,243016).supported && !ComposeRuntimeInline(zero,domain,61403,243016).supported &&
    !ComposeRuntimeInline(unsupported,domain,61403,243016).supported && ComposeRuntimeInline(layout,huge,61403,243016).overflow &&
    ComposeRuntimeInline(layout,domain,SIZE_MAX,243016).overflow;
  const bool pass=allocation_free && unchanged && valid.supported && !valid.overflow && whole_once && reader_once && controls;
  Json report={{"schema","runtime-inline-source.v1"},{"status",pass?"PASS":"FAIL"},{"widths",layout.widths},{"present",layout.present},
    {"pipeline_whole",pipeline.whole},{"pipeline_children",pipeline.children},{"population_globals",pipeline.global},{"population_clocks",pipeline.clocks},
    {"allocation_free_under_persistent_refusal",allocation_free},{"no_owner_pin_counter_changes",unchanged},{"whole_parent_once",whole_once},
    {"e11_readers_once",reader_once},{"unknown_missing_zero_overcoverage_overflow_controls",controls},
    {"source_os_threads",8},{"e11_reader_threads",1},{"base_named_static",61403},{"base_named_tls",243016},
    {"additional_static",valid.additional_static},{"additional_tls",valid.additional_tls},{"joined_named_inline",valid.joined_named_inline},
    {"scope","Compiled20 core named fixed/TLS rows;whole pipeline parent once and explicit E11 overlap. No dynamic contents,compiler guards/TLS alignment/loader,renderer/window TLS,logger destinations/CRT/GPU/process or full S6 matrix verdict."}};
  std::ofstream out(argc>1?argv[1]:"runtime-inline-source.json");out<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return out&&pass?0:1;
}
