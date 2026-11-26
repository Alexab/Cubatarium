#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cutum
{
enum class RuntimeInlineCell {LogAdapter,Population,StbiProcess,JsonProcess,StallFlag,
  Pipeline,PopulationClock,Readers,BlockProfiles,MergeName,ActiveBudget,ActiveShared,
  ActiveStbi,StbiFailure,JsonShape,Pin,WorkerRole,Features,StageClock,EnvironmentVector,ActiveBudgetSource,PopulationObserver,Count};
struct PipelineRuntimeInlineLayout
{
  size_t whole{},global{},clocks{};
  std::array<size_t,5> children{}; // key,owners,build counter,world,unique pipeline pointer
};
PipelineRuntimeInlineLayout GetPipelineRuntimeInlineLayout() noexcept;
size_t GetFeatureRuntimeInlineBytes() noexcept;
size_t GetLogAdapterRuntimeInlineBytes() noexcept;
size_t GetWorkerRoleRuntimeInlineBytes() noexcept;
size_t GetContentPinRuntimeInlineBytes() noexcept;
struct RuntimeInlineLayout
{
  static constexpr size_t Count=static_cast<size_t>(RuntimeInlineCell::Count);
  static constexpr uint64_t All=(uint64_t{1}<<Count)-1;
  std::array<size_t,Count> widths{};
  uint64_t present{};
  bool supported{true};
  void Set(RuntimeInlineCell cell,size_t bytes) noexcept {
    const auto i=static_cast<size_t>(cell);widths[i]=bytes;present|=uint64_t{1}<<i;
  }
};
inline bool RuntimeInlineCellIsTls(size_t i) noexcept {return i>=static_cast<size_t>(RuntimeInlineCell::Pipeline);}
struct RuntimeInlineDomain
{
  size_t os_threads{};
  // Number of row instances already included in retained E11/another parent.
  // Explicit0 is required for uncovered rows;unknown coverage is not0.
  std::array<size_t,RuntimeInlineLayout::Count> already_counted{};
  bool thread_domain_known{},coverage_known{},base_known{};
};
struct RuntimeInlineEnvelope
{
  size_t additional_static{},additional_tls{},joined_named_inline{};
  bool supported{},overflow{};
};
inline RuntimeInlineEnvelope ComposeRuntimeInline(const RuntimeInlineLayout &layout,
    const RuntimeInlineDomain &d,size_t base_named_static,size_t base_named_tls)
{
  RuntimeInlineEnvelope out;
  out.supported=layout.supported && layout.present==RuntimeInlineLayout::All && d.thread_domain_known && d.coverage_known && d.base_known;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){out.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  for(size_t i=0;i<layout.widths.size();++i){
    if(layout.widths[i]==0)out.supported=false;
    const auto instances=RuntimeInlineCellIsTls(i)?d.os_threads:size_t{1};
    if(d.already_counted[i]>instances){out.supported=false;continue;}
    const auto bytes=mul(instances-d.already_counted[i],layout.widths[i]);
    if(RuntimeInlineCellIsTls(i))out.additional_tls=add(out.additional_tls,bytes);
    else out.additional_static=add(out.additional_static,bytes);
  }
  out.joined_named_inline=add(add(base_named_static,base_named_tls),add(out.additional_static,out.additional_tls));return out;
}
} // namespace cutum
